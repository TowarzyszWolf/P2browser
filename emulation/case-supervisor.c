/* Keep daemonized browser helpers in the sampled subtree. Project MIT license. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop_requested(int signal_number) { stopping = signal_number; }

static void clean_children(void)
{
    char path[96];
    struct timespec delay = {0, 20000000};
    int attempt;
    snprintf(path, sizeof path, "/proc/self/task/%ld/children", (long)getpid());
    for (attempt = 0; attempt < 30; attempt++) {
        FILE *children = fopen(path, "r");
        long pid;
        int found = 0;
        if (children) {
            while (fscanf(children, "%ld", &pid) == 1) {
                if (pid > 1) {
                    kill((pid_t)pid, attempt < 10 ? SIGTERM : SIGKILL);
                    found = 1;
                }
            }
            fclose(children);
        } else {
            /* /task/TID/children requires optional kernel configuration. */
            DIR *proc = opendir("/proc");
            struct dirent *entry;
            while (proc && (entry = readdir(proc)) != NULL) {
                char *end;
                char status_path[96], line[128];
                long parent = -1;
                FILE *status_file;
                pid = strtol(entry->d_name, &end, 10);
                if (*end || pid <= 1) continue;
                snprintf(status_path, sizeof status_path, "/proc/%ld/status", pid);
                status_file = fopen(status_path, "r");
                if (!status_file) continue;
                while (fgets(line, sizeof line, status_file)) {
                    if (sscanf(line, "PPid:%ld", &parent) == 1) break;
                }
                fclose(status_file);
                if (parent == (long)getpid()) {
                    kill((pid_t)pid, attempt < 10 ? SIGTERM : SIGKILL);
                    found = 1;
                }
            }
            if (proc) closedir(proc);
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
        if (!found) break;
        nanosleep(&delay, NULL);
    }
}

int main(int argc, char **argv)
{
    struct sigaction action = {0};
    pid_t child, waited;
    FILE *pid_file;
    int status = 0, result;
    if (argc < 3) {
        fprintf(stderr, "Usage: case-supervisor NEW_PID_FILE COMMAND [ARG...]\n");
        return 64;
    }
    if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0) {
        perror("PR_SET_CHILD_SUBREAPER");
        return 1;
    }
    action.sa_handler = stop_requested;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    pid_file = fopen(argv[1], "wx");
    if (!pid_file) {
        perror("new PID file");
        return 1;
    }
    fprintf(pid_file, "%ld\n", (long)getpid());
    if (fclose(pid_file) != 0) {
        return 1;
    }
    child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        execvp(argv[2], argv + 2);
        perror("browser exec");
        _exit(127);
    }
    do {
        if (stopping) kill(child, (int)stopping);
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) result = 1;
    else if (WIFEXITED(status)) result = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result = 128 + WTERMSIG(status);
    else result = 1;
    clean_children();
    return result;
}
