// Voice-to-text typing tool for Linux
//
// Records speech via global hotkey, transcribes with whisper.cpp,
// and types the result into the focused window.
//
#include "common-sdl.h"
#include "common.h"
#include "common-whisper.h"
#include "whisper.h"
#include "hotkey.h"
#include "text-output.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

static_assert(std::atomic<bool>::is_always_lock_free,
              "std::atomic<bool> must be lock-free for signal handler safety");

// command-line parameters
struct typer_params {
    // whisper
    int32_t     n_threads      = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t     capture_id     = -1;
    int32_t     audio_ctx      = 0;
    bool        translate      = false;
    bool        use_gpu        = true;
    bool        flash_attn     = true;
    std::string language       = "en";
    std::string model          = "models/ggml-base.en.bin";

    // VAD
    float       vad_thold      = 0.6f;
    float       freq_thold     = 100.0f;
    int32_t     silence_ms     = 1500;
    int32_t     max_record_ms  = 30000;
    std::string vad_model_path;

    // hotkey
    std::string hotkey         = "ctrl+period";
    bool        push_to_talk   = false;

    // output
    bool        use_clipboard  = true;
    int32_t     type_delay_ms  = 12;

    // daemon
    bool        daemonize      = false;
    bool        print_energy   = false;
};

static bool parse_int(const char * s, int32_t & out) {
    try {
        size_t pos = 0;
        out = std::stoi(s, &pos);
        if (pos != strlen(s)) {
            fprintf(stderr, "error: invalid integer '%s'\n", s);
            return false;
        }
        return true;
    } catch (...) {
        fprintf(stderr, "error: invalid integer '%s'\n", s);
        return false;
    }
}

static bool parse_float(const char * s, float & out) {
    try {
        size_t pos = 0;
        out = std::stof(s, &pos);
        if (pos != strlen(s)) {
            fprintf(stderr, "error: invalid number '%s'\n", s);
            return false;
        }
        return true;
    } catch (...) {
        fprintf(stderr, "error: invalid number '%s'\n", s);
        return false;
    }
}

static void typer_print_usage(int /*argc*/, char ** argv, const typer_params & params) {
    fprintf(stderr, "\n");
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -h,       --help              show this help message and exit\n");
    fprintf(stderr, "  -t N,     --threads N     [%-7d] number of threads\n",                       params.n_threads);
    fprintf(stderr, "  -m FNAME, --model FNAME   [%-7s] model path\n",                              params.model.c_str());
    fprintf(stderr, "  -l LANG,  --language LANG [%-7s] spoken language\n",                         params.language.c_str());
    fprintf(stderr, "  -c ID,    --capture ID    [%-7d] capture device ID\n",                       params.capture_id);
    fprintf(stderr, "  -ng,      --no-gpu            disable GPU inference\n");
    fprintf(stderr, "  -fa,      --flash-attn        enable flash attention (default)\n");
    fprintf(stderr, "  -nfa,     --no-flash-attn     disable flash attention\n");
    fprintf(stderr, "  -tr,      --translate         translate to English\n");
    fprintf(stderr, "            --hotkey KEY    [%-7s] global hotkey\n",                            params.hotkey.c_str());
    fprintf(stderr, "            --push-to-talk       hold-to-record mode\n");
    fprintf(stderr, "            --silence-ms N  [%-7d] silence to auto-stop (ms)\n",               params.silence_ms);
    fprintf(stderr, "            --max-record-ms N[%-6d] max recording time (ms)\n",                params.max_record_ms);
    fprintf(stderr, "            --vad-thold N   [%-7.2f] VAD energy threshold\n",                  params.vad_thold);
    fprintf(stderr, "            --freq-thold N  [%-7.2f] high-pass filter cutoff Hz\n",            params.freq_thold);
    fprintf(stderr, "            --vad-model F        Silero VAD model path\n");
    fprintf(stderr, "            --no-clipboard       use keystroke simulation\n");
    fprintf(stderr, "            --type-delay-ms N[%-6d] keystroke delay (ms)\n",                   params.type_delay_ms);
    fprintf(stderr, "            --daemon             run as background daemon\n");
    fprintf(stderr, "  -pe,      --print-energy       print audio energy levels\n");
    fprintf(stderr, "\n");
}

static bool typer_params_parse(int argc, char ** argv, typer_params & params) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        // Helper: check that a value argument exists
        auto next_arg = [&]() -> const char * {
            if (++i >= argc) {
                fprintf(stderr, "error: missing value for %s\n", arg.c_str());
                return nullptr;
            }
            return argv[i];
        };

        if (arg == "-h" || arg == "--help") {
            typer_print_usage(argc, argv, params);
            exit(0);
        }
        else if (arg == "-t"   || arg == "--threads")        { auto v = next_arg(); if (!v || !parse_int(v, params.n_threads))      return false; }
        else if (arg == "-m"   || arg == "--model")          { auto v = next_arg(); if (!v) return false; params.model           = v; }
        else if (arg == "-l"   || arg == "--language")       { auto v = next_arg(); if (!v) return false; params.language         = v; }
        else if (arg == "-c"   || arg == "--capture")        { auto v = next_arg(); if (!v || !parse_int(v, params.capture_id))    return false; }
        else if (arg == "-ng"  || arg == "--no-gpu")         { params.use_gpu          = false; }
        else if (arg == "-fa"  || arg == "--flash-attn")     { params.flash_attn       = true; }
        else if (arg == "-nfa" || arg == "--no-flash-attn")  { params.flash_attn       = false; }
        else if (arg == "-tr"  || arg == "--translate")      { params.translate         = true; }
        else if (arg == "-ac"  || arg == "--audio-ctx")      { auto v = next_arg(); if (!v || !parse_int(v, params.audio_ctx))     return false; }
        else if (                 arg == "--hotkey")          { auto v = next_arg(); if (!v) return false; params.hotkey            = v; }
        else if (                 arg == "--push-to-talk")   { params.push_to_talk      = true; }
        else if (                 arg == "--silence-ms")     { auto v = next_arg(); if (!v || !parse_int(v, params.silence_ms))    return false; }
        else if (                 arg == "--max-record-ms")  { auto v = next_arg(); if (!v || !parse_int(v, params.max_record_ms)) return false; }
        else if (                 arg == "--vad-thold")      { auto v = next_arg(); if (!v || !parse_float(v, params.vad_thold))   return false; }
        else if (                 arg == "--freq-thold")     { auto v = next_arg(); if (!v || !parse_float(v, params.freq_thold))  return false; }
        else if (                 arg == "--vad-model")      { auto v = next_arg(); if (!v) return false; params.vad_model_path     = v; }
        else if (                 arg == "--no-clipboard")   { params.use_clipboard      = false; }
        else if (                 arg == "--type-delay-ms")  { auto v = next_arg(); if (!v || !parse_int(v, params.type_delay_ms)) return false; }
        else if (                 arg == "--daemon")         { params.daemonize           = true; }
        else if (arg == "-pe"  || arg == "--print-energy")   { params.print_energy        = true; }
        else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            typer_print_usage(argc, argv, params);
            exit(1);
        }
    }
    return true;
}

// Transcribe audio buffer and return concatenated text
static std::string transcribe(
        struct whisper_context * ctx,
        const typer_params & params,
        const std::vector<float> & pcmf32) {

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    wparams.print_progress   = false;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.translate        = params.translate;
    wparams.single_segment   = false;
    wparams.max_tokens       = 0;
    wparams.language         = params.language.c_str();
    wparams.n_threads        = params.n_threads;
    wparams.audio_ctx        = params.audio_ctx;
    wparams.no_context       = true;
    wparams.no_timestamps    = true;
    wparams.suppress_blank   = true;

    // Silero VAD integration
    if (!params.vad_model_path.empty()) {
        wparams.vad            = true;
        wparams.vad_model_path = params.vad_model_path.c_str();
    }

    if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        fprintf(stderr, "error: whisper_full() failed\n");
        return "";
    }

    std::string result;
    const int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char * text = whisper_full_get_segment_text(ctx, i);
        result += text;
    }

    return result;
}

static std::atomic<bool> g_running(true);
static std::atomic<bool> g_sigusr1(false);

static void signal_handler(int /*sig*/) {
    g_running = false;
}

#ifdef __linux__
static void sigusr1_handler(int /*sig*/) {
    g_sigusr1 = true;
}
#endif

int main(int argc, char ** argv) {
    ggml_backend_load_all();

    typer_params params;

    if (!typer_params_parse(argc, argv, params)) {
        return 1;
    }

    // Check runtime dependencies (using 'command -v' which is POSIX-standard)
    auto check_dep = [](const char * prog) -> bool {
        pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
            execlp("sh", "sh", "-c", (std::string("command -v ") + prog).c_str(), nullptr);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    };
    if (!check_dep("xdotool")) {
        fprintf(stderr, "error: xdotool not found. Install with: sudo apt install xdotool\n");
        return 1;
    }
    if (params.use_clipboard && !check_dep("xclip")) {
        fprintf(stderr, "error: xclip not found. Install with: sudo apt install xclip\n");
        return 1;
    }

    // Single-instance lock
#ifdef __linux__
    int lock_fd = open("/tmp/whisper-typer.lock", O_CREAT | O_RDWR, 0600);
    if (lock_fd >= 0) {
        if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
            fprintf(stderr, "error: another whisper-typer instance is already running\n");
            close(lock_fd);
            return 1;
        }
        // Keep lock_fd open for the lifetime of the process
    }
#endif

    // Daemonize before any SDL/X11 init
#ifdef __linux__
    if (params.daemonize) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid > 0) {
            // Parent: print child PID and exit
            printf("%d\n", pid);
            return 0;
        }
        // Child: new session, detach from terminal
        setsid();
        if (chdir("/") != 0) {
            perror("chdir");
        }
        if (!freopen("/dev/null", "r", stdin)) {
            perror("freopen stdin");
        }
        if (!freopen("/dev/null", "w", stdout)) {
            perror("freopen stdout");
        }
        // Keep stderr for logging
    }
#endif

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef __linux__
    signal(SIGUSR1, sigusr1_handler);
#endif

    // Validate language
    if (params.language != "auto" && whisper_lang_id(params.language.c_str()) == -1) {
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        return 1;
    }

    // Init whisper
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu    = params.use_gpu;
    cparams.flash_attn = params.flash_attn;

    struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);
    if (!ctx) {
        fprintf(stderr, "error: failed to initialize whisper context\n");
        return 2;
    }

    // Init audio capture with buffer large enough for max recording
    audio_async audio(params.max_record_ms);
    if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE)) {
        fprintf(stderr, "error: audio.init() failed\n");
        whisper_free(ctx);
        return 3;
    }

    // Start audio immediately and keep it running.
    // On PipeWire, SDL audio callbacks fail if the device is resumed after
    // the evdev hotkey listener thread has started. Keeping audio always
    // running avoids this — we just clear() the buffer when recording starts.
    audio.resume();

    // Init hotkey listener
    HotkeyListener hotkey;
    bool hotkey_ok = false;
    if (hotkey.init(params.hotkey)) {
        if (hotkey.start(nullptr)) {
            hotkey_ok = true;
        } else {
            fprintf(stderr, "warning: failed to start hotkey listener\n");
        }
    } else {
        fprintf(stderr, "warning: hotkey unavailable (see above for details)\n");
    }

    if (!hotkey_ok) {
        fprintf(stderr, "\n");
        fprintf(stderr, "  Hotkey disabled. You can still toggle recording with:\n");
        fprintf(stderr, "    kill -USR1 %d\n", (int)getpid());
        fprintf(stderr, "\n");
        fprintf(stderr, "  To enable the hotkey, add yourself to the 'input' group:\n");
        fprintf(stderr, "    sudo usermod -aG input $USER\n");
        fprintf(stderr, "  Then log out and back in.\n");
    }

    // Init text output
    TextOutput output;
    output.set_use_clipboard(params.use_clipboard);
    output.set_type_delay_ms(params.type_delay_ms);

    // Print info
    fprintf(stderr, "\n");
    fprintf(stderr, "whisper-typer:\n");
    fprintf(stderr, "  model     = %s\n", params.model.c_str());
    fprintf(stderr, "  language  = %s\n", params.language.c_str());
    fprintf(stderr, "  threads   = %d\n", params.n_threads);
    fprintf(stderr, "  hotkey    = %s%s\n", params.hotkey.c_str(), hotkey_ok ? "" : " (UNAVAILABLE)");
    fprintf(stderr, "  pid       = %d\n", (int)getpid());
    fprintf(stderr, "  mode      = %s\n", params.push_to_talk ? "push-to-talk" : "toggle");
    fprintf(stderr, "  clipboard = %s\n", params.use_clipboard ? "yes" : "no");
    if (!params.vad_model_path.empty()) {
        fprintf(stderr, "  vad-model = %s\n", params.vad_model_path.c_str());
    }
    fprintf(stderr, "\n");

    // Minimum samples needed for vad_simple to work correctly.
    // vad_simple(buf, sample_rate, last_ms=1000, ...) requires buf.size() > sample_rate * last_ms / 1000
    // With 2000ms request and 1000ms last_ms, we need at least 2 * WHISPER_SAMPLE_RATE samples
    const size_t vad_min_samples = (size_t)(WHISPER_SAMPLE_RATE * 2) + 1;

    // State machine
    enum class State { IDLE, RECORDING, TRANSCRIBING };
    State state = State::IDLE;

    std::vector<float> pcmf32;
    auto record_start  = std::chrono::steady_clock::now();
    auto silence_start = std::chrono::steady_clock::now();
    bool speech_detected = false;

    if (hotkey_ok) {
        fprintf(stderr, "[ready] press %s to record (or kill -USR1 %d)\n", params.hotkey.c_str(), (int)getpid());
    } else {
        fprintf(stderr, "[ready] send: kill -USR1 %d\n", (int)getpid());
    }

    while (g_running) {
        // Handle SDL events (for Ctrl+C via SDL)
        if (!sdl_poll_events()) {
            break;
        }

        switch (state) {
            case State::IDLE: {
                // Check for hotkey or SIGUSR1 toggle
                bool triggered = hotkey.poll_pressed() || g_sigusr1.exchange(false);

                if (triggered) {
                    // Start recording (audio is already running, just clear buffer)
                    audio.clear();
                    pcmf32.clear();
                    speech_detected = false;
                    record_start  = std::chrono::steady_clock::now();
                    silence_start = std::chrono::steady_clock::now();

                    // Drain any pending hotkey events from the triggering keypress
                    hotkey.poll_pressed();
                    hotkey.poll_released();

                    state = State::RECORDING;
                    fprintf(stderr, "[recording...]\n");
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                break;
            }

            case State::RECORDING: {
                // Check for manual stop
                bool stop_triggered = false;

                if (params.push_to_talk) {
                    stop_triggered = hotkey.poll_released();
                } else {
                    stop_triggered = hotkey.poll_pressed() || g_sigusr1.exchange(false);
                }

                if (stop_triggered) {
                    // Debounce: ignore stop events within 300ms of recording start
                    // to prevent the triggering keypress from immediately stopping
                    auto since_start = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - record_start).count();
                    if (since_start < 300) {
                        break;
                    }

                    // Manual stop: force speech_detected so we always transcribe
                    speech_detected = true;
                    state = State::TRANSCRIBING;
                    break;
                }

                // Check max recording time
                auto now = std::chrono::steady_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - record_start).count();

                if (elapsed_ms >= params.max_record_ms) {
                    fprintf(stderr, "[max recording time reached]\n");
                    state = State::TRANSCRIBING;
                    break;
                }

                // VAD check: get last 2 seconds for energy analysis
                {
                    std::vector<float> vad_buf;
                    audio.get(2000, vad_buf);

                    // Only run VAD when we have enough samples for vad_simple to work correctly.
                    // vad_simple returns false ("speech") when buffer is too small, which would
                    // cause a false positive.
                    if (vad_buf.size() >= vad_min_samples) {
                        // vad_simple returns true when the last portion is silent
                        bool is_silent = ::vad_simple(vad_buf, WHISPER_SAMPLE_RATE,
                            1000, params.vad_thold, params.freq_thold, params.print_energy);

                        if (!is_silent) {
                            // Speech is active
                            speech_detected = true;
                            silence_start = now;
                        }

                        // Auto-stop: speech was detected, now silence for N ms
                        if (speech_detected) {
                            auto silence_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - silence_start).count();

                            if (silence_duration >= params.silence_ms) {
                                fprintf(stderr, "[auto-stop: silence detected]\n");
                                state = State::TRANSCRIBING;
                                break;
                            }
                        }
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }

            case State::TRANSCRIBING: {
                // Get all recorded audio from the buffer before pausing
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - record_start).count();

                int get_ms = std::min((int)elapsed_ms, params.max_record_ms);
                audio.get(get_ms, pcmf32);

                if (pcmf32.empty()) {
                    fprintf(stderr, "[no audio captured]\n");
                    state = State::IDLE;
                    fprintf(stderr, "[ready]\n");
                    break;
                }

                // Skip transcription if no speech was detected (prevent hallucinations)
                if (!speech_detected) {
                    fprintf(stderr, "[no speech detected, skipping]\n");
                    state = State::IDLE;
                    fprintf(stderr, "[ready]\n");
                    break;
                }

                fprintf(stderr, "[transcribing %d ms of audio...]\n", (int)(pcmf32.size() * 1000.0f / WHISPER_SAMPLE_RATE));

                std::string text = transcribe(ctx, params, pcmf32);

                // Trim whitespace (whisper often prepends a space)
                text = ::trim(text);

                if (!text.empty()) {
                    fprintf(stderr, "[result: \"%s\"]\n", text.c_str());
                    output.type(text);
                } else {
                    fprintf(stderr, "[empty transcription]\n");
                }

                state = State::IDLE;
                fprintf(stderr, "[ready]\n");
                break;
            }
        }
    }

    // Cleanup
    hotkey.stop();
    audio.pause();
    whisper_print_timings(ctx);
    whisper_free(ctx);

    fprintf(stderr, "\nwhisper-typer: exiting\n");

    return 0;
}
