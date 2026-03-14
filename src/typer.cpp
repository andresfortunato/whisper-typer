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
#ifdef HAS_TRAY
#include "tray.h"
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

    // history
    bool        no_history        = false;
    std::string history_file;
    int32_t     max_history_mb    = 10;

    // tray
    bool        no_tray        = false;

    // daemon
    bool        daemonize      = false;
    bool        stop_daemon    = false;
    bool        print_energy   = false;
    bool        threads_explicit = false;
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

static bool load_config_file(typer_params & params) {
    std::string config_path;
    const char * xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        config_path = std::string(xdg_config) + "/whisper-typer/config";
    } else {
        const char * home = getenv("HOME");
        if (!home) return false;
        config_path = std::string(home) + "/.config/whisper-typer/config";
    }

    std::ifstream f(config_path);
    if (!f.is_open()) return false;

    fprintf(stderr, "whisper-typer: loading config from %s\n", config_path.c_str());

    std::string line;
    int line_num = 0;
    while (std::getline(f, line)) {
        line_num++;
        // Strip comments and whitespace
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) line.erase(comment_pos);
        while (!line.empty() && std::isspace((unsigned char)line.front())) line.erase(line.begin());
        while (!line.empty() && std::isspace((unsigned char)line.back()))  line.pop_back();
        if (line.empty()) continue;

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            fprintf(stderr, "config:%d: missing '=' in '%s'\n", line_num, line.c_str());
            continue;
        }

        std::string key = line.substr(0, eq_pos);
        std::string val = line.substr(eq_pos + 1);
        while (!key.empty() && std::isspace((unsigned char)key.back()))  key.pop_back();
        while (!val.empty() && std::isspace((unsigned char)val.front())) val.erase(val.begin());

        if      (key == "threads")        { parse_int(val.c_str(), params.n_threads); params.threads_explicit = true; }
        else if (key == "model")          { params.model = val; }
        else if (key == "language")       { params.language = val; }
        else if (key == "capture")        { parse_int(val.c_str(), params.capture_id); }
        else if (key == "no-gpu")         { params.use_gpu = (val != "true" && val != "1"); }
        else if (key == "flash-attn")     { params.flash_attn = (val == "true" || val == "1"); }
        else if (key == "translate")      { params.translate = (val == "true" || val == "1"); }
        else if (key == "audio-ctx")      { parse_int(val.c_str(), params.audio_ctx); }
        else if (key == "hotkey")         { params.hotkey = val; }
        else if (key == "push-to-talk")   { params.push_to_talk = (val == "true" || val == "1"); }
        else if (key == "silence-ms")     { parse_int(val.c_str(), params.silence_ms); }
        else if (key == "max-record-ms")  { parse_int(val.c_str(), params.max_record_ms); }
        else if (key == "vad-thold")      { parse_float(val.c_str(), params.vad_thold); }
        else if (key == "freq-thold")     { parse_float(val.c_str(), params.freq_thold); }
        else if (key == "vad-model")      { params.vad_model_path = val; }
        else if (key == "no-clipboard")   { params.use_clipboard = !(val == "true" || val == "1"); }
        else if (key == "type-delay-ms")  { parse_int(val.c_str(), params.type_delay_ms); }
        else if (key == "no-tray")        { params.no_tray = (val == "true" || val == "1"); }
        else if (key == "no-history")     { params.no_history = (val == "true" || val == "1"); }
        else if (key == "history-file")   { params.history_file = val; }
        else if (key == "max-history-mb") { parse_int(val.c_str(), params.max_history_mb); }
        else if (key == "daemon")         { params.daemonize = (val == "true" || val == "1"); }
        else {
            fprintf(stderr, "config:%d: unknown key '%s'\n", line_num, key.c_str());
        }
    }
    return true;
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
    fprintf(stderr, "  -ac N,    --audio-ctx N   [%-7d] audio context size (0 = full)\n",            params.audio_ctx);
    fprintf(stderr, "            --hotkey KEY    [%-7s] global hotkey\n",                            params.hotkey.c_str());
    fprintf(stderr, "            --push-to-talk       hold-to-record mode\n");
    fprintf(stderr, "            --silence-ms N  [%-7d] silence to auto-stop (ms)\n",               params.silence_ms);
    fprintf(stderr, "            --max-record-ms N[%-6d] max recording time (ms)\n",                params.max_record_ms);
    fprintf(stderr, "            --vad-thold N   [%-7.2f] VAD energy threshold\n",                  params.vad_thold);
    fprintf(stderr, "            --freq-thold N  [%-7.2f] high-pass filter cutoff Hz\n",            params.freq_thold);
    fprintf(stderr, "            --vad-model F        Silero VAD model path\n");
    fprintf(stderr, "            --no-clipboard       use keystroke simulation\n");
    fprintf(stderr, "            --type-delay-ms N[%-6d] keystroke delay (ms)\n",                   params.type_delay_ms);
    fprintf(stderr, "            --no-tray            disable system tray icon\n");
    fprintf(stderr, "            --no-history         disable transcript history\n");
    fprintf(stderr, "            --history-file F     custom history file path\n");
    fprintf(stderr, "            --max-history-mb N   max history file size (MB, default 10)\n");
    fprintf(stderr, "            --daemon             run as background daemon\n");
    fprintf(stderr, "            --stop               stop running daemon\n");
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
        else if (arg == "-t"   || arg == "--threads")        { auto v = next_arg(); if (!v || !parse_int(v, params.n_threads))      return false; params.threads_explicit = true; }
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
        else if (                 arg == "--no-tray")          { params.no_tray               = true; }
        else if (                 arg == "--no-history")      { params.no_history            = true; }
        else if (                 arg == "--history-file")   { auto v = next_arg(); if (!v) return false; params.history_file = v; }
        else if (                 arg == "--max-history-mb") { auto v = next_arg(); if (!v || !parse_int(v, params.max_history_mb)) return false; }
        else if (                 arg == "--daemon")         { params.daemonize           = true; }
        else if (                 arg == "--stop")           { params.stop_daemon         = true; }
        else if (arg == "-pe"  || arg == "--print-energy")   { params.print_energy        = true; }
        else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            typer_print_usage(argc, argv, params);
            exit(1);
        }
    }
    return true;
}

static std::atomic<bool> g_running(true);
static std::atomic<bool> g_sigusr1(false);

static bool whisper_abort_cb(void * /*user_data*/) {
    return !g_running;
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

    wparams.abort_callback           = whisper_abort_cb;
    wparams.abort_callback_user_data = nullptr;

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

static void signal_handler(int /*sig*/) {
    g_running = false;
}

#ifdef __linux__
static void sigusr1_handler(int /*sig*/) {
    g_sigusr1 = true;
}
#endif

// Escape a string for safe inclusion in a JSON value
static std::string json_escape_string(const std::string & s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Create directories recursively (like mkdir -p)
static void mkdir_p(const std::string & path) {
    std::string accum;
    for (size_t i = 0; i < path.size(); i++) {
        accum += path[i];
        if (path[i] == '/' && i > 0) {
            mkdir(accum.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
}

// Append a transcript entry to the history file (JSONL format)
static void history_append(const std::string & path, const std::string & text,
                           int duration_ms, int32_t max_mb) {
    if (path.empty() || text.empty()) return;

    // Create parent directory
    auto slash = path.rfind('/');
    if (slash != std::string::npos) mkdir_p(path.substr(0, slash));

    // Rotation: if file exceeds max_mb, keep newest half of entries
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > (int64_t)max_mb * 1024 * 1024) {
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) lines.push_back(std::move(line));
        in.close();

        size_t keep = lines.size() / 2;
        std::ofstream out(path, std::ios::trunc);
        for (size_t i = lines.size() - keep; i < lines.size(); i++) {
            out << lines[i] << "\n";
        }
        fprintf(stderr, "history: rotated (kept %zu of %zu entries)\n", keep, lines.size());
    }

    // ISO 8601 UTC timestamp
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm utc; gmtime_r(&tt, &utc);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &utc);

    // Append JSONL line (create with 0600 to protect transcript privacy regardless of umask)
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) { fprintf(stderr, "warning: cannot open history: %s\n", path.c_str()); return; }
    FILE * f = fdopen(fd, "a");
    if (!f) { close(fd); fprintf(stderr, "warning: cannot open history: %s\n", path.c_str()); return; }
    fprintf(f, "{\"ts\":\"%s\",\"text\":\"%s\",\"duration_ms\":%d}\n",
            ts, json_escape_string(text).c_str(), duration_ms);
    fclose(f);
}

int main(int argc, char ** argv) {
    ggml_backend_load_all();

    typer_params params;
    load_config_file(params);  // config file first; CLI overrides

    if (!typer_params_parse(argc, argv, params)) {
        return 1;
    }

    // Cap threads to 2 in daemon mode unless explicitly set
    if (params.daemonize && !params.threads_explicit && params.n_threads > 2) {
        params.n_threads = 2;
    }

    // Resolve history file path
    std::string history_path;
    if (!params.no_history) {
        if (!params.history_file.empty()) {
            history_path = params.history_file;
        } else {
            const char * xdg_data = getenv("XDG_DATA_HOME");
            if (xdg_data && xdg_data[0] != '\0') {
                history_path = std::string(xdg_data) + "/whisper-typer/history.jsonl";
            } else {
                const char * home = getenv("HOME");
                if (home) history_path = std::string(home) + "/.local/share/whisper-typer/history.jsonl";
            }
        }
    }

    // Handle --stop: send SIGTERM to running daemon and exit
#ifdef __linux__
    if (params.stop_daemon) {
        std::string stop_lock_path;
        const char * stop_xdg = getenv("XDG_RUNTIME_DIR");
        if (stop_xdg && stop_xdg[0] != '\0') {
            stop_lock_path = std::string(stop_xdg) + "/whisper-typer.lock";
        } else {
            stop_lock_path = "/tmp/whisper-typer.lock";
        }
        int stop_fd = open(stop_lock_path.c_str(), O_RDONLY);
        if (stop_fd < 0) {
            fprintf(stderr, "error: no running daemon found (no lock file)\n");
            return 1;
        }
        // Try to acquire lock — if we can, no daemon is running
        if (flock(stop_fd, LOCK_EX | LOCK_NB) == 0) {
            flock(stop_fd, LOCK_UN);
            close(stop_fd);
            fprintf(stderr, "error: no running daemon found\n");
            return 1;
        }
        // Read PID from lock file
        char pid_buf[32] = {};
        ssize_t n = read(stop_fd, pid_buf, sizeof(pid_buf) - 1);
        close(stop_fd);
        if (n <= 0) {
            fprintf(stderr, "error: could not read PID from lock file\n");
            return 1;
        }
        pid_t daemon_pid = atoi(pid_buf);
        if (daemon_pid <= 0) {
            fprintf(stderr, "error: invalid PID in lock file\n");
            return 1;
        }
        if (kill(daemon_pid, SIGTERM) != 0) {
            fprintf(stderr, "error: failed to send SIGTERM to PID %d: %s\n", daemon_pid, strerror(errno));
            return 1;
        }
        fprintf(stderr, "whisper-typer: sent SIGTERM to PID %d\n", daemon_pid);
        return 0;
    }
#endif

    // Fire-and-forget notification via notify-send (if available)
    // Uses double-fork so the grandchild is reparented to init (no zombies).
    auto notify = [](const char * summary, int timeout_ms) {
        pid_t pid = fork();
        if (pid < 0) return;
        if (pid == 0) {
            if (fork() != 0) _exit(0);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
            std::string t = std::to_string(timeout_ms);
            execlp("notify-send", "notify-send", "-t", t.c_str(), "whisper-typer", summary, nullptr);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
    };

    // Check if a program exists in PATH (no shell, no fork — pure access() search)
    auto check_dep = [](const char * prog) -> bool {
        if (!prog || prog[0] == '\0') return false;
        const char * path_env = getenv("PATH");
        if (!path_env) return false;
        std::string path_str(path_env);
        size_t start = 0;
        while (start <= path_str.size()) {
            size_t end = path_str.find(':', start);
            if (end == std::string::npos) end = path_str.size();
            std::string dir = path_str.substr(start, end - start);
            if (!dir.empty()) {
                std::string full = dir + "/" + prog;
                if (access(full.c_str(), X_OK) == 0) return true;
            }
            start = end + 1;
        }
        return false;
    };
    // Detect display backend
    DisplayBackend display = detect_display_backend();
    if (display == DisplayBackend::UNKNOWN) {
        fprintf(stderr, "error: no display server detected (need WAYLAND_DISPLAY or DISPLAY)\n");
        return 1;
    }

    if (display == DisplayBackend::WAYLAND) {
        if (!check_dep("wtype")) {
            fprintf(stderr, "error: wtype not found. Install with: sudo apt install wtype\n");
            return 1;
        }
    } else {
        if (!check_dep("xdotool")) {
            fprintf(stderr, "error: xdotool not found. Install with: sudo apt install xdotool\n");
            return 1;
        }
        if (params.use_clipboard && !check_dep("xclip")) {
            fprintf(stderr, "error: xclip not found. Install with: sudo apt install xclip\n");
            return 1;
        }
    }
    bool has_notify = check_dep("notify-send");

    // Single-instance lock
#ifdef __linux__
    std::string lock_path;
    const char * xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && xdg_runtime[0] != '\0') {
        lock_path = std::string(xdg_runtime) + "/whisper-typer.lock";
    } else {
        lock_path = "/tmp/whisper-typer.lock";
    }
    int lock_fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd >= 0) {
        if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
            fprintf(stderr, "error: another whisper-typer instance is already running\n");
            close(lock_fd);
            return 1;
        }
        // Write PID to lock file for --stop support
        if (ftruncate(lock_fd, 0) == 0) {
            std::string pid_str = std::to_string(getpid());
            if (write(lock_fd, pid_str.c_str(), pid_str.size()) < 0) {
                // Best-effort PID write for --stop support; not critical
            }
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
        if (nice(10) == -1 && errno != 0) {
            perror("nice");
        }
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
    signal(SIGTSTP, SIG_IGN);  // Prevent job-control stop (Ctrl+Z) — a stopped process can't respond to SIGTERM
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
    output.set_backend(display);
    output.set_use_clipboard(params.use_clipboard);
    output.set_type_delay_ms(params.type_delay_ms);

    // Init system tray icon
#ifdef HAS_TRAY
    TrayIcon tray;
    bool tray_ok = false;
    std::string last_transcript;
    if (!params.no_tray) {
        TrayCallbacks cb;
        cb.on_toggle = [&]() { g_sigusr1 = true; };
        cb.on_quit   = [&]() { g_running = false; };
        cb.get_last_transcript = [&]() { return last_transcript; };
        cb.get_history_path    = [&]() { return history_path; };
        tray_ok = tray.init(cb);
    }
#endif

    // Print info
    fprintf(stderr, "\n");
    fprintf(stderr, "whisper-typer:\n");
    fprintf(stderr, "  model     = %s\n", params.model.c_str());
    fprintf(stderr, "  language  = %s\n", params.language.c_str());
    fprintf(stderr, "  threads   = %d\n", params.n_threads);
    fprintf(stderr, "  hotkey    = %s%s\n", params.hotkey.c_str(), hotkey_ok ? "" : " (UNAVAILABLE)");
    fprintf(stderr, "  pid       = %d\n", (int)getpid());
    fprintf(stderr, "  mode      = %s\n", params.push_to_talk ? "push-to-talk" : "toggle");
    fprintf(stderr, "  display   = %s\n", display == DisplayBackend::WAYLAND ? "wayland" : "x11");
    fprintf(stderr, "  clipboard = %s\n", params.use_clipboard ? "yes" : "no");
    if (!params.vad_model_path.empty()) {
        fprintf(stderr, "  vad-model = %s\n", params.vad_model_path.c_str());
    }
    if (!history_path.empty()) {
        fprintf(stderr, "  history   = %s\n", history_path.c_str());
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

#ifdef HAS_TRAY
        if (tray_ok) tray.poll();
#endif

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
#ifdef HAS_TRAY
                    if (tray_ok) tray.set_state(TrayState::RECORDING);
#endif
                    fprintf(stderr, "[recording...]\n");
                    if (has_notify) notify("Recording...", 1000);
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
#ifdef HAS_TRAY
                    if (tray_ok) tray.set_state(TrayState::IDLE);
#endif
                    fprintf(stderr, "[ready]\n");
                    break;
                }

                // Skip transcription if no speech was detected (prevent hallucinations)
                if (!speech_detected) {
                    fprintf(stderr, "[no speech detected, skipping]\n");
                    state = State::IDLE;
#ifdef HAS_TRAY
                    if (tray_ok) tray.set_state(TrayState::IDLE);
#endif
                    fprintf(stderr, "[ready]\n");
                    break;
                }

#ifdef HAS_TRAY
                if (tray_ok) tray.set_state(TrayState::TRANSCRIBING);
#endif
                fprintf(stderr, "[transcribing %d ms of audio...]\n", (int)(pcmf32.size() * 1000.0f / WHISPER_SAMPLE_RATE));
                if (has_notify) notify("Transcribing...", 2000);

                std::string text = transcribe(ctx, params, pcmf32);

                // Trim whitespace (whisper often prepends a space)
                text = ::trim(text);

                if (!text.empty()) {
                    fprintf(stderr, "[result: \"%s\"]\n", text.c_str());
                    output.type(text);
                    if (!history_path.empty()) {
                        int dur = (int)(pcmf32.size() * 1000.0f / WHISPER_SAMPLE_RATE);
                        history_append(history_path, text, dur, params.max_history_mb);
                    }
#ifdef HAS_TRAY
                    if (tray_ok) {
                        last_transcript = text;
                        tray.set_last_transcript(text);
                    }
#endif
                } else {
                    fprintf(stderr, "[empty transcription]\n");
                }

                state = State::IDLE;
#ifdef HAS_TRAY
                if (tray_ok) tray.set_state(TrayState::IDLE);
#endif
                fprintf(stderr, "[ready]\n");
                break;
            }
        }
    }

    // Cleanup
#ifdef HAS_TRAY
    if (tray_ok) tray.shutdown();
#endif
    hotkey.stop();
    audio.pause();
    whisper_print_timings(ctx);
    whisper_free(ctx);

    fprintf(stderr, "\nwhisper-typer: exiting\n");

    return 0;
}
