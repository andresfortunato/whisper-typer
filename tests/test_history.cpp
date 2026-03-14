// Unit tests for json_escape_string() and history_append() from typer.cpp

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Duplicate json_escape_string (static in typer.cpp)
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

// Duplicate mkdir_p (static in typer.cpp)
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

// Duplicate history_append (static in typer.cpp)
static void history_append(const std::string & path, const std::string & text,
                           int duration_ms, int32_t max_mb) {
    if (path.empty() || text.empty()) return;

    auto slash = path.rfind('/');
    if (slash != std::string::npos) mkdir_p(path.substr(0, slash));

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
    }

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm utc; gmtime_r(&tt, &utc);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &utc);

    // Create with 0600 to protect transcript privacy regardless of umask
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    FILE * f = fdopen(fd, "a");
    if (!f) { close(fd); return; }
    fprintf(f, "{\"ts\":\"%s\",\"text\":\"%s\",\"duration_ms\":%d}\n",
            ts, json_escape_string(text).c_str(), duration_ms);
    fclose(f);
}

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char * name, bool condition) {
    tests_run++;
    printf("  %s ... %s\n", name, condition ? "ok" : "FAILED");
    if (condition) tests_passed++;
    assert(condition);
}

// Count lines in a file
static int count_lines(const std::string & path) {
    std::ifstream f(path);
    int n = 0;
    std::string line;
    while (std::getline(f, line)) n++;
    return n;
}

// Read all lines from a file
static std::vector<std::string> read_lines(const std::string & path) {
    std::ifstream f(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(std::move(line));
    return lines;
}

// Get a unique temp file path
static std::string temp_path(const char * suffix) {
    static int counter = 0;
    char buf[256];
    snprintf(buf, sizeof(buf), "/tmp/whisper-typer-test-%d-%d-%s",
             (int)getpid(), counter++, suffix);
    return buf;
}

void test_json_escape() {
    check("json_escape_basic",
          json_escape_string("hello world") == "hello world");

    check("json_escape_quotes",
          json_escape_string("say \"hello\"") == "say \\\"hello\\\"");

    check("json_escape_backslash",
          json_escape_string("path\\to") == "path\\\\to");

    check("json_escape_newline",
          json_escape_string("line1\nline2") == "line1\\nline2");

    check("json_escape_tab",
          json_escape_string("col1\tcol2") == "col1\\tcol2");

    check("json_escape_control",
          json_escape_string(std::string(1, '\x01')) == "\\u0001");

    check("json_escape_mixed",
          json_escape_string("He said \"hi\"\nand\tleft\\")
          == "He said \\\"hi\\\"\\nand\\tleft\\\\");
}

void test_history_creates_file() {
    std::string path = temp_path("creates.jsonl");
    unlink(path.c_str());

    history_append(path, "test transcript", 1500, 10);

    struct stat st;
    check("history_creates_file", stat(path.c_str(), &st) == 0 && st.st_size > 0);
    unlink(path.c_str());
}

void test_history_valid_jsonl() {
    std::string path = temp_path("valid.jsonl");
    unlink(path.c_str());

    history_append(path, "hello world", 2000, 10);

    auto lines = read_lines(path);
    check("history_valid_jsonl_one_line", lines.size() == 1);

    // Check that it contains expected JSON keys
    const auto & line = lines[0];
    check("history_valid_jsonl_has_ts",    line.find("\"ts\":\"") != std::string::npos);
    check("history_valid_jsonl_has_text",  line.find("\"text\":\"hello world\"") != std::string::npos);
    check("history_valid_jsonl_has_dur",   line.find("\"duration_ms\":2000") != std::string::npos);

    unlink(path.c_str());
}

void test_history_multiple_appends() {
    std::string path = temp_path("multi.jsonl");
    unlink(path.c_str());

    history_append(path, "first", 100, 10);
    history_append(path, "second", 200, 10);
    history_append(path, "third", 300, 10);

    check("history_multiple_appends", count_lines(path) == 3);
    unlink(path.c_str());
}

void test_history_rotation() {
    std::string path = temp_path("rotate.jsonl");
    unlink(path.c_str());

    // Write enough data to exceed 1 byte max (effectively always rotate)
    // Use max_mb=0 which means threshold is 0 bytes — any existing file triggers rotation
    // Actually max_mb=0 means threshold 0, so stat > 0 is always true for non-empty files.
    // Write 10 entries, then append one more with max_mb that triggers rotation.

    // First write 10 entries with large max
    for (int i = 0; i < 10; i++) {
        history_append(path, "entry " + std::to_string(i), 100, 100);
    }
    check("history_rotation_pre", count_lines(path) == 10);

    // Now append with a very small max_mb (0 = threshold of 0 bytes, always rotates)
    // The file is non-empty, so 0 * 1024*1024 = 0, and st.st_size > 0 is true
    history_append(path, "trigger rotation", 100, 0);

    int after = count_lines(path);
    // Should have kept 5 (half of 10) + 1 new = 6
    check("history_rotation_trimmed", after == 6);

    // Verify the newest entries survived
    auto lines = read_lines(path);
    check("history_rotation_kept_newest",
          lines[0].find("entry 5") != std::string::npos);
    check("history_rotation_has_new",
          lines[5].find("trigger rotation") != std::string::npos);

    unlink(path.c_str());
}

void test_history_empty_text_ignored() {
    std::string path = temp_path("empty.jsonl");
    unlink(path.c_str());

    history_append(path, "", 100, 10);

    struct stat st;
    check("history_empty_text_ignored", stat(path.c_str(), &st) != 0);
}

void test_history_file_permissions() {
    std::string path = temp_path("perms.jsonl");
    unlink(path.c_str());

    // Set permissive umask to verify history_append overrides it
    mode_t old_umask = umask(0);
    history_append(path, "secret transcript", 1000, 10);
    umask(old_umask);

    struct stat st;
    check("history_file_permissions",
          stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    unlink(path.c_str());
}

int main() {
    printf("test_history:\n");

    test_json_escape();
    test_history_creates_file();
    test_history_valid_jsonl();
    test_history_multiple_appends();
    test_history_rotation();
    test_history_empty_text_ignored();
    test_history_file_permissions();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
