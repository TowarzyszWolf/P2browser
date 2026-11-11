/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "memory-policy.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define P2_MAX_PROCESSES 32768u

typedef struct {
    long pid, parent;
    uint64_t start;
    char state;
    int selected;
} Process;

static int process_path(char *path, size_t size, const char *root,
                        long pid, const char *file)
{
    int n = snprintf(path, size, "%s/%ld/%s", root, pid, file);
    return n > 0 && (size_t)n < size;
}

static int read_identity(const char *root, long pid, Process *process)
{
    char path[PATH_MAX], buffer[4096], *at, *end;
    FILE *file;
    size_t size;
    long recorded;
    int field;
    uint64_t value;
    if (!process_path(path, sizeof path, root, pid, "stat")) return 0;
    file = fopen(path, "r");
    if (!file) return 0;
    size = fread(buffer, 1, sizeof buffer - 1, file);
    if (ferror(file) || !feof(file) || size == 0) {
        fclose(file);
        return 0;
    }
    fclose(file);
    buffer[size] = '\0';
    errno = 0;
    recorded = strtol(buffer, &end, 10);
    if (errno || recorded != pid || *end != ' ' || end[1] != '(') return 0;
    /* comm permits spaces, parentheses and newlines. The final ')' closes it. */
    at = strrchr(end + 1, ')');
    if (!at || at[1] != ' ' || !at[2] || at[3] != ' ') return 0;
    process->pid = pid;
    process->state = at[2];
    if (process->state == 'Z' || process->state == 'X') return 0;
    at += 4;
    /* Original field 4 is PPID; field 22 is the process start time. */
    for (field = 4; field <= 22; field++) {
        while (isspace((unsigned char)*at)) at++;
        if (!*at) return 0;
        end = at;
        while (*end && !isspace((unsigned char)*end)) end++;
        if (field == 4 || field == 22) {
            char saved = *end, *number_end;
            uintmax_t parsed;
            *end = '\0';
            errno = 0;
            if (!isdigit((unsigned char)*at)) return 0;
            parsed = strtoumax(at, &number_end, 10);
            *end = saved;
            if (errno || number_end != end || parsed > UINT64_MAX) return 0;
            value = (uint64_t)parsed;
            if (field == 4) {
                if (value > INT_MAX) return 0;
                process->parent = (long)value;
            } else process->start = value;
        }
        at = end;
    }
    return 1;
}

static int same_identity(const Process *a, const Process *b)
{
    return a->pid == b->pid && a->start == b->start;
}

/* -1 means another field, 0 a malformed value, 1 a valid KiB field. */
static int kib_field(const char *line, const char *key, uint64_t *value)
{
    size_t key_size = strlen(key);
    const char *at;
    char *end;
    uintmax_t parsed;
    if (strncmp(line, key, key_size)) return -1;
    at = line + key_size;
    while (*at == ' ' || *at == '\t') at++;
    if (!isdigit((unsigned char)*at)) return 0;
    errno = 0;
    parsed = strtoumax(at, &end, 10);
    if (errno || parsed > UINT64_MAX || end == at) return 0;
    if (*end != ' ' && *end != '\t') return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (end[0] != 'k' || end[1] != 'B') return 0;
    end += 2;
    while (isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int add_value(uint64_t *sum, uint64_t value)
{
    if (UINT64_MAX - *sum < value) {
        *sum = UINT64_MAX;
        return 0;
    }
    *sum += value;
    return 1;
}

static int read_pss_file(const char *path, uint64_t *value)
{
    FILE *file = fopen(path, "r");
    char line[512];
    uint64_t total = 0, part;
    int found = 0, valid = 1, at_start = 1;
    if (!file) return 0;
    while (fgets(line, sizeof line, file)) {
        int field = at_start ? kib_field(line, "Pss:", &part) : -1;
        at_start = strchr(line, '\n') != NULL;
        if (field == 0) valid = 0;
        else if (field == 1) {
            found = 1;
            if (!at_start || !add_value(&total, part)) valid = 0;
        }
    }
    if (ferror(file)) valid = 0;
    fclose(file);
    if (!valid || !found) return 0;
    *value = total;
    return 1;
}

static int read_process_pss(const char *root, long pid, uint64_t *value)
{
    char path[PATH_MAX];
    if (process_path(path, sizeof path, root, pid, "smaps_rollup") &&
        read_pss_file(path, value)) return 1;
    return process_path(path, sizeof path, root, pid, "smaps") &&
           read_pss_file(path, value);
}

static void read_process_status(const char *root, long pid,
                                 uint64_t *rss, uint64_t *swap)
{
    char path[PATH_MAX], line[512];
    FILE *file;
    int at_start = 1;
    uint64_t value;
    *rss = *swap = 0;
    if (!process_path(path, sizeof path, root, pid, "status")) return;
    file = fopen(path, "r");
    if (!file) return;
    while (fgets(line, sizeof line, file)) {
        if (at_start) {
            if (kib_field(line, "VmRSS:", &value) == 1) *rss = value;
            if (kib_field(line, "VmSwap:", &value) == 1) *swap = value;
        }
        at_start = strchr(line, '\n') != NULL;
    }
    fclose(file);
}

static void read_system_memory(const char *root, P2MemorySample *sample)
{
    char path[PATH_MAX], line[512];
    FILE *file;
    uint64_t value;
    int n = snprintf(path, sizeof path, "%s/meminfo", root);
    int available_seen = 0, swap_seen = 0, at_start = 1;
    if (n < 0 || (size_t)n >= sizeof path) return;
    file = fopen(path, "r");
    if (!file) return;
    while (fgets(line, sizeof line, file)) {
        if (at_start) {
            int available = kib_field(line, "MemAvailable:", &value);
            if (available >= 0) {
                sample->available_known = available && !available_seen;
                if (sample->available_known) sample->available_kib = value;
                available_seen++;
            }
            {
                int swap = kib_field(line, "SwapFree:", &value);
                if (swap >= 0) {
                    sample->swap_free_known = swap && !swap_seen;
                    if (sample->swap_free_known) sample->swap_free_kib = value;
                    swap_seen++;
                }
            }
        }
        at_start = strchr(line, '\n') != NULL;
    }
    if (ferror(file)) sample->available_known = sample->swap_free_known = 0;
    fclose(file);
}

int p2_memory_sample(const char *proc_root, long root_pid, P2MemorySample *out)
{
    DIR *directory;
    struct dirent *entry;
    Process *processes = NULL, root, check;
    size_t length = 0, capacity = 0, i, j;
    P2MemorySample sample;
    int changed;
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    memset(&sample, 0, sizeof sample);
    if (!proc_root) proc_root = "/proc";
    if (!*proc_root || root_pid < 1 || root_pid > INT_MAX ||
        !read_identity(proc_root, root_pid, &root)) return 0;
    directory = opendir(proc_root);
    if (!directory) return 0;
    for (;;) {
        long pid;
        char *end;
        Process process;
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno) goto failed;
            break;
        }
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        errno = 0;
        pid = strtol(entry->d_name, &end, 10);
        if (errno || *end || pid < 1 || pid > INT_MAX) continue;
        memset(&process, 0, sizeof process);
        if (!read_identity(proc_root, pid, &process)) continue;
        if (length == capacity) {
            Process *larger;
            size_t new_capacity = capacity ? capacity * 2 : 64;
            if (new_capacity > P2_MAX_PROCESSES) goto failed;
            larger = realloc(processes, new_capacity * sizeof *processes);
            if (!larger) goto failed;
            processes = larger;
            capacity = new_capacity;
        }
        process.selected = same_identity(&process, &root);
        processes[length++] = process;
    }
    closedir(directory);
    directory = NULL;
    if (!read_identity(proc_root, root_pid, &check) || !same_identity(&root, &check)) goto failed;
    do {
        changed = 0;
        for (i = 0; i < length; i++) {
            if (processes[i].selected) continue;
            for (j = 0; j < length; j++) {
                if (processes[j].selected && processes[i].parent == processes[j].pid) {
                    processes[i].selected = 1;
                    changed = 1;
                    break;
                }
            }
        }
    } while (changed);
    sample.pss_complete = 1;
    for (i = 0; i < length; i++) {
        uint64_t pss, rss, swap;
        int known;
        if (!processes[i].selected ||
            !read_identity(proc_root, processes[i].pid, &check) ||
            !same_identity(&processes[i], &check)) continue;
        read_process_status(proc_root, processes[i].pid, &rss, &swap);
        known = read_process_pss(proc_root, processes[i].pid, &pss);
        if (!read_identity(proc_root, processes[i].pid, &check) ||
            !same_identity(&processes[i], &check)) continue;
        sample.processes++;
        add_value(&sample.rss_kib, rss);
        add_value(&sample.swap_kib, swap);
        if (!known || !add_value(&sample.pss_kib, pss)) {
            sample.pss_complete = 0;
            sample.pss_missing++;
        }
    }
    if (!sample.processes || !read_identity(proc_root, root_pid, &check) ||
        !same_identity(&root, &check)) goto failed;
    read_system_memory(proc_root, &sample);
    free(processes);
    *out = sample;
    return 1;
failed:
    if (directory) closedir(directory);
    free(processes);
    return 0;
}

P2MemoryLevel p2_memory_level(const P2MemorySample *sample, uint64_t budget_kib)
{
    if (!sample) return P2_MEMORY_NORMAL;
    if (sample->available_known && sample->swap_free_known &&
        sample->available_kib < 12 * UINT64_C(1024) &&
        sample->swap_free_kib < 16 * UINT64_C(1024)) return P2_MEMORY_CRITICAL;
    if (budget_kib && sample->pss_complete && sample->pss_kib > budget_kib)
        return P2_MEMORY_PRESSURE;
    return P2_MEMORY_NORMAL;
}
