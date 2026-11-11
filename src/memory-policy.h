/* SPDX-License-Identifier: MIT */
#ifndef P2_MEMORY_POLICY_H
#define P2_MEMORY_POLICY_H

#include <stdint.h>

typedef struct {
    uint64_t pss_kib, rss_kib, swap_kib, available_kib, swap_free_kib;
    unsigned processes, pss_missing;
    int pss_complete, available_known, swap_free_known;
} P2MemorySample;

typedef enum {
    P2_MEMORY_NORMAL = 0,
    P2_MEMORY_PRESSURE = 1,
    P2_MEMORY_CRITICAL = 2
} P2MemoryLevel;

/* Best-effort sample of the discovered, identity-checked process tree. RSS and
 * VmSwap are diagnostic sums; shared pages are counted repeatedly by RSS.
 * PSS is usable as a total only when pss_complete is true. A false return
 * means the root/tree could not be sampled; out is cleared, not a zero-use
 * measurement. No command lines, names, or filesystem paths are returned. */
int p2_memory_sample(const char *proc_root, long root_pid, P2MemorySample *out);

/* A soft PSS budget is advisory. Zero disables the budget. Critical system
 * pressure takes precedence, but requires both available-RAM and swap data. */
P2MemoryLevel p2_memory_level(const P2MemorySample *sample, uint64_t budget_kib);

#endif
