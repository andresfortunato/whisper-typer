// Unit tests for check_dep_in_path() — PATH-based dependency checker
//
// Tests the access()-based PATH search that replaces the old shell-based check_dep.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>

// Duplicate the new check_dep_in_path function (static in typer.cpp)
static bool check_dep_in_path(const char * prog) {
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
}

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char * name, bool condition) {
    tests_run++;
    printf("  %s ... %s\n", name, condition ? "ok" : "FAILED");
    if (condition) tests_passed++;
    assert(condition);
}

void test_check_dep_finds_common_binaries() {
    // These exist on virtually every Linux system
    check("finds_sh",    check_dep_in_path("sh"));
    check("finds_ls",    check_dep_in_path("ls"));
    check("finds_cat",   check_dep_in_path("cat"));
}

void test_check_dep_rejects_missing() {
    check("rejects_nonexistent",           check_dep_in_path("this-binary-does-not-exist-xyz123") == false);
    check("rejects_empty",                 check_dep_in_path("") == false);
}

void test_check_dep_no_path_env() {
    const char * orig = getenv("PATH");
    unsetenv("PATH");

    check("no_path_returns_false", check_dep_in_path("sh") == false);

    // Restore
    if (orig) setenv("PATH", orig, 1);
}

void test_check_dep_custom_path() {
    const char * orig = getenv("PATH");

    // Set PATH to just /usr/bin — sh is typically in /usr/bin
    setenv("PATH", "/usr/bin", 1);
    check("custom_path_finds_in_dir", check_dep_in_path("env"));

    // Set PATH to empty directory — should find nothing
    setenv("PATH", "/nonexistent-dir-xyz", 1);
    check("custom_path_empty_dir", check_dep_in_path("sh") == false);

    // Restore
    if (orig) setenv("PATH", orig, 1);
    else unsetenv("PATH");
}

void test_check_dep_handles_edge_cases() {
    const char * orig = getenv("PATH");

    // PATH with leading/trailing colons (empty components)
    setenv("PATH", ":/usr/bin:", 1);
    check("leading_trailing_colons", check_dep_in_path("env"));

    // PATH with consecutive colons
    setenv("PATH", "/nonexistent::/usr/bin", 1);
    check("consecutive_colons", check_dep_in_path("env"));

    // Restore
    if (orig) setenv("PATH", orig, 1);
    else unsetenv("PATH");
}

int main() {
    printf("test_check_dep:\n");

    test_check_dep_finds_common_binaries();
    test_check_dep_rejects_missing();
    test_check_dep_no_path_env();
    test_check_dep_custom_path();
    test_check_dep_handles_edge_cases();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
