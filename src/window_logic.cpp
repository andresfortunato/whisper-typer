// Pure logic functions for the window module.
// Separated from window.cpp so tests can link without SDL/ImGui.

#include "window.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// Minimal JSON string value extractor: find "key":"value" and return value.
// Handles \" escapes inside the value. Returns empty string if not found.
static std::string json_get_string(const std::string & json, const char * key) {
    // Build search pattern: "key":"
    std::string pattern = std::string("\"") + key + "\":\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();

    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            if (next == '"')       { result += '"';  pos += 2; continue; }
            else if (next == '\\') { result += '\\'; pos += 2; continue; }
            else if (next == 'n')  { result += '\n'; pos += 2; continue; }
            else if (next == 'r')  { result += '\r'; pos += 2; continue; }
            else if (next == 't')  { result += '\t'; pos += 2; continue; }
            else                   { result += next; pos += 2; continue; }
        }
        if (c == '"') break;
        result += c;
        pos++;
    }
    return result;
}

// Minimal JSON integer extractor: find "key":N and return N.
// Returns -1 if not found.
static int json_get_int(const std::string & json, const char * key) {
    std::string pattern = std::string("\"") + key + "\":";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return -1;
    pos += pattern.size();
    // Skip whitespace
    while (pos < json.size() && json[pos] == ' ') pos++;
    if (pos >= json.size()) return -1;

    int val = 0;
    bool found_digit = false;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        val = val * 10 + (json[pos] - '0');
        found_digit = true;
        pos++;
    }
    return found_digit ? val : -1;
}

bool parse_history_line(const std::string & line, HistoryEntry & out) {
    if (line.empty() || line[0] != '{') return false;

    std::string ts   = json_get_string(line, "ts");
    std::string text = json_get_string(line, "text");
    int duration     = json_get_int(line, "duration_ms");

    if (ts.empty() || text.empty() || duration < 0) return false;

    out.timestamp   = ts;
    out.text        = text;
    out.duration_ms = duration;
    return true;
}

std::vector<HistoryEntry> parse_history(const std::string & content) {
    std::vector<HistoryEntry> entries;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        HistoryEntry e;
        if (parse_history_line(line, e)) {
            entries.push_back(std::move(e));
        }
    }
    // Reverse so newest is first
    std::reverse(entries.begin(), entries.end());
    return entries;
}

std::string format_duration(int duration_ms) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1fs", duration_ms / 1000.0);
    return buf;
}

std::string autostart_desktop_path() {
    const char * xdg_config = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg_config && xdg_config[0] != '\0') {
        base = xdg_config;
    } else {
        const char * home = getenv("HOME");
        if (!home) return "";
        base = std::string(home) + "/.config";
    }
    return base + "/autostart/whisper-typer.desktop";
}
