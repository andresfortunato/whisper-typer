// Unit tests for fire-and-forget subprocess helpers (zombie prevention)
//
// Verifies that fire_and_forget_exec() and fire_and_forget_pipe() leave
// no zombie (defunct) child processes.

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// Count zombie children of the current process
static int count_zombies() {
    // Read /proc/self/task/*/children isn't reliable, use waitpid instead
    int zombies = 0;
    while (true) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            // This child was a zombie (we just reaped it), count it
            zombies++;
        } else {
            break;
        }
    }
    return zombies;
}

// ------------------------------------------------------------------
// Duplicate the fire-and-forget helpers as they will exist in the code
// BEFORE the fix: single-fork, no waitpid (produces zombies)
// ------------------------------------------------------------------
static void fire_and_forget_exec_OLD(const char * const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        // Safe: argv is controlled by test code, not user input
        execvp(argv[0], const_cast<char * const *>(argv));
        _exit(127);
    }
    // OLD: no waitpid — child becomes zombie when it exits
}

// ------------------------------------------------------------------
// NEW: double-fork pattern (should produce zero zombies)
// ------------------------------------------------------------------
static void fire_and_forget_exec(const char * const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        // Double-fork: grandchild is reparented to init, which auto-reaps it
        if (fork() != 0) _exit(0);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        // Safe: argv is controlled by test code, not user input
        execvp(argv[0], const_cast<char * const *>(argv));
        _exit(127);
    }
    // Reap the intermediate child (exits immediately after second fork)
    int status;
    waitpid(pid, &status, 0);
}

// NEW: double-fork with pipe (for clipboard)
static void fire_and_forget_pipe(const char * const argv[],
                                 const char * data, size_t len) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (pid == 0) {
        close(pipefd[1]);
        if (fork() != 0) {
            close(pipefd[0]);
            _exit(0);
        }
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        // Safe: argv is controlled by test code, not user input
        execvp(argv[0], const_cast<char * const *>(argv));
        _exit(127);
    }

    // Parent: write data, then reap intermediate child
    close(pipefd[0]);
    while (len > 0) {
        ssize_t n = write(pipefd[1], data, len);
        if (n > 0) { data += n; len -= n; }
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    close(pipefd[1]);
    int status;
    waitpid(pid, &status, 0);
}

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char * name, bool condition) {
    tests_run++;
    printf("  %s ... %s\n", name, condition ? "ok" : "FAILED");
    if (condition) tests_passed++;
    assert(condition);
}

void test_old_pattern_produces_zombies() {
    // Verify the OLD pattern actually leaves zombies (validates our test approach)
    const char * argv[] = {"true", nullptr};
    fire_and_forget_exec_OLD(argv);
    fire_and_forget_exec_OLD(argv);
    fire_and_forget_exec_OLD(argv);

    // Wait for children to finish running
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // The old pattern should leave zombies (reaped here by count_zombies)
    int z = count_zombies();
    check("old_pattern_leaves_zombies", z >= 1);
}

void test_double_fork_no_zombies() {
    // Fire several detached processes using the new pattern
    const char * argv[] = {"true", nullptr};
    fire_and_forget_exec(argv);
    fire_and_forget_exec(argv);
    fire_and_forget_exec(argv);

    // Wait for grandchildren to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Double-fork should leave zero zombies
    int z = count_zombies();
    check("double_fork_no_zombies", z == 0);
}

void test_double_fork_pipe_no_zombies() {
    // Use the pipe variant (simulates clipboard copy)
    const char * argv[] = {"cat", nullptr};  // cat reads stdin and exits
    const char * data = "hello world";
    fire_and_forget_pipe(argv, data, strlen(data));
    fire_and_forget_pipe(argv, data, strlen(data));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int z = count_zombies();
    check("double_fork_pipe_no_zombies", z == 0);
}

void test_double_fork_with_failing_command() {
    // Even if the command doesn't exist, no zombies should be left
    const char * argv[] = {"nonexistent-binary-xyz", nullptr};
    fire_and_forget_exec(argv);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int z = count_zombies();
    check("failing_command_no_zombies", z == 0);
}

int main() {
    printf("test_zombie:\n");

    test_old_pattern_produces_zombies();
    test_double_fork_no_zombies();
    test_double_fork_pipe_no_zombies();
    test_double_fork_with_failing_command();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
