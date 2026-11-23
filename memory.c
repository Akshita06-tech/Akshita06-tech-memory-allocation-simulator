/* memory.c - M2: the memory manager
 *
 * Owner: M2. Edit only memory.c and memory.h.
 * Only this file changes the block list (split, free, merge, compact).
 * M3 may search the list but never edits it. M1 only calls these functions.
 *
 * Tags: [EXISTING] moved unchanged   [CHANGED] changed   [NEW] new
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "policy.h"      /* find_block, engine_choose   (M3) */
#include "metrics.h"     /* mem_stats                   (M3) */

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                  */
/* ------------------------------------------------------------------ */

/* [EXISTING] */
Block *new_block(int start, int size, int is_free) {
    Block *b = calloc(1, sizeof(Block));
    if (!b) { fprintf(stderr, "host out of memory\n"); exit(1); }
    b->start = start; b->size = size; b->is_free = is_free;
    return b;
}

/* [CHANGED] checks its arguments, and supports size-only mode (unit_bytes = 0):
 * the block list works as usual but no data array is created. */
Memory *mem_create(int total_units, int unit_bytes) {
    if (total_units <= 0 || unit_bytes < 0) return NULL;
    Memory *m = calloc(1, sizeof(Memory));
    if (!m) { fprintf(stderr, "host out of memory\n"); exit(1); }
    if (unit_bytes > 0) {
        m->mem = calloc((size_t)total_units * unit_bytes, 1);
        if (!m->mem) { fprintf(stderr, "host out of memory\n"); exit(1); }
    }
    m->total_units = total_units;
    m->unit_bytes = unit_bytes;
    m->head = new_block(0, total_units, 1);      /* one big free block */
    m->last_used = FIRST_FIT;
    return m;
}

/* [CHANGED] safe to call with NULL; free(NULL) is fine for the data array */
void mem_destroy(Memory *m) {
    if (!m) return;
    Block *b = m->head;
    while (b) { Block *n = b->next; free(b); b = n; }
    free(m->mem);
    free(m);
}

/* ------------------------------------------------------------------ */
/*  Merge (coalescing) and compaction                                 */
/* ------------------------------------------------------------------ */

/* [EXISTING] join every pair of neighbouring free blocks */
void merge_free(Memory *m) {
    Block *b = m->head;
    while (b && b->next) {
        if (b->is_free && b->next->is_free) {
            Block *n = b->next;
            b->size += n->size;
            b->next = n->next;
            free(n);
        } else {
            b = b->next;
        }
    }
}

/* [CHANGED] same as before, but the data is only moved when there is a data
 * array. In size-only mode just the block table is rebuilt.
 * After this call every used block has a new start address: anyone who
 * remembered an address must look it up again with mem_lookup. */
void mem_compact(Memory *m) {
    int addr = 0, freed = 0;
    Block *prev = NULL, *cur = m->head;
    while (cur) {
        Block *nx = cur->next;
        if (cur->is_free) {
            freed += cur->size;
            if (prev) prev->next = nx; else m->head = nx;
            free(cur);
        } else {
            if (cur->start != addr) {
                if (m->mem)
                    memmove(m->mem + (size_t)addr * m->unit_bytes,
                            m->mem + (size_t)cur->start * m->unit_bytes,
                            (size_t)cur->size * m->unit_bytes);
                m->units_moved += cur->size;
            }
            cur->start = addr;
            addr += cur->size;
            prev = cur;
        }
        cur = nx;
    }
    if (freed > 0) {
        Block *f = new_block(addr, freed, 1);
        if (prev) prev->next = f; else m->head = f;
    }
    m->compactions++;
}

/* ------------------------------------------------------------------ */
/*  Allocation                                                        */
/* ------------------------------------------------------------------ */

/* [EXISTING] */
Block *find_owner(const Memory *m, const char *owner) {
    for (Block *b = m->head; b; b = b->next)
        if (!b->is_free && strcmp(b->owner, owner) == 0) return b;
    return NULL;
}

/* [CHANGED] M3's find_block / engine_choose decide WHICH block; this function
 * does the split. New safety rules: the owner name must not be empty and must
 * not already be in use (a block is found and freed by its name).
 * The counters ok / frag_fail / oom_fail count attempts, not processes. */
Block *mem_alloc(Memory *m, const char *owner, int units, Strategy s) {
    if (units <= 0) return NULL;
    if (!owner || owner[0] == '\0') return NULL;      /* [NEW] */
    if (find_owner(m, owner)) return NULL;            /* [NEW] */

    Strategy use = s;
    if (s == SMART) {
        int compact_first;
        use = engine_choose(m, units, &compact_first);
        if (compact_first) mem_compact(m);
    }
    m->last_used = use;

    Block *b = find_block(m, units, use);
    if (!b) {                                   /* classify the failure */
        Stats st = mem_stats(m);
        if (st.total_free >= units) m->frag_fail++;   /* scattered     */
        else                        m->oom_fail++;    /* truly no room */
        return NULL;
    }
    if (b->size > units) {                      /* split the block      */
        Block *rest = new_block(b->start + units, b->size - units, 1);
        rest->next = b->next;
        b->next = rest;
        b->size = units;
    }
    b->is_free = 0;
    snprintf(b->owner, sizeof b->owner, "%s", owner);
    m->ok++;
    return b;
}

/* [CHANGED] copies real data into the block's part of the array.
 * Returns 1 on success, 0 if the data does not fit in the block.
 * In size-only mode nothing is copied, but the length is remembered. */
int mem_store(Memory *m, Block *b, const unsigned char *data, int len) {
    if (!b || len < 0) return 0;
    if (!m->mem) { b->data_len = len; return 1; }          /* size-only */
    if ((long)len > (long)b->size * m->unit_bytes) return 0;
    memcpy(m->mem + (size_t)b->start * m->unit_bytes, data, (size_t)len);
    b->data_len = len;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Release                                                           */
/* ------------------------------------------------------------------ */

/* [CHANGED] Returns 1 on success, 0 if no such owner (so a double free is
 * safe). Clears the data only when there is a data array. */
int mem_free(Memory *m, const char *owner) {
    Block *b = find_owner(m, owner);
    if (!b) return 0;
    if (m->mem)
        memset(m->mem + (size_t)b->start * m->unit_bytes, 0,
               (size_t)b->size * m->unit_bytes);
    b->is_free = 1;
    b->owner[0] = '\0';
    b->data_len = 0;
    merge_free(m);
    return 1;
}

/* [NEW] The block list is the source of truth for addresses. Reports the
 * half-open range [start, end) of the block owned by `owner`.
 * Returns 1 if found, 0 if not. start and end may be NULL. */
int mem_lookup(const Memory *m, const char *owner, int *start, int *end) {
    const Block *b = find_owner(m, owner);
    if (!b) return 0;
    if (start) *start = b->start;
    if (end)   *end   = b->start + b->size;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Checks and display                                                */
/* ------------------------------------------------------------------ */

/* [CHANGED] blocks are contiguous and cover all of memory, no two free blocks
 * are neighbours, and (new) used blocks have an owner while free blocks have none. */
int mem_check_invariants(const Memory *m) {
    int addr = 0;
    for (const Block *b = m->head; b; b = b->next) {
        if (b->start != addr || b->size <= 0) return 0;
        if (b->next && b->is_free && b->next->is_free) return 0;
        if (b->is_free && b->owner[0] != '\0') return 0;      /* [NEW] */
        if (!b->is_free && b->owner[0] == '\0') return 0;     /* [NEW] */
        addr += b->size;
    }
    return addr == m->total_units;
}

/* [CHANGED] shows [start, end) for every block, and draws a bar of at most
 * 64 characters (a big pool is scaled down; '#' means mostly used). */
void print_map(const Memory *m) {
    printf("  Address range         Size  Status  Owner (bytes)\n");
    for (const Block *b = m->head; b; b = b->next) {
        char range[40];
        snprintf(range, sizeof range, "[%d, %d)", b->start, b->start + b->size);
        if (b->is_free)
            printf("  %-20s %5d  FREE    -\n", range, b->size);
        else
            printf("  %-20s %5d  USED    %s (%d bytes)\n",
                   range, b->size, b->owner, b->data_len);
    }
    int cells = m->total_units < 64 ? m->total_units : 64;
    printf("  [");
    for (int c = 0; c < cells; c++) {
        long lo = (long)c * m->total_units / cells;
        long hi = (long)(c + 1) * m->total_units / cells;
        long used = 0;
        for (const Block *b = m->head; b; b = b->next) {
            if (b->is_free) continue;
            long s = b->start > lo ? b->start : lo;
            long e = (b->start + b->size) < hi ? (b->start + b->size) : hi;
            if (e > s) used += e - s;
        }
        putchar(used * 2 >= hi - lo ? '#' : '.');
    }
    printf("]  # = used, . = free");
    if (cells < m->total_units)
        printf("  (1 character = about %d units)", m->total_units / cells);
    printf("\n");
}
