/* metrics.c - M3 (existing code, as it will be after M4's split) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "metrics.h"

Stats mem_stats(const Memory *m) {
    Stats s = {0, 0, 0, 0, 0.0};
    for (const Block *b = m->head; b; b = b->next) {
        if (b->is_free) {
            s.total_free += b->size;
            s.frags++;
            if (b->size > s.largest) s.largest = b->size;
        } else {
            s.used += b->size;
        }
    }
    if (s.total_free > 0)
        s.ext_frag = 100.0 * (1.0 - (double)s.largest / s.total_free);
    return s;
}

void print_stats(const Memory *m) {
    Stats s = mem_stats(m);
    printf("  Free memory        : %d of %d units\n", s.total_free, m->total_units);
    printf("  Used memory        : %d units\n", s.used);
    printf("  Free fragments     : %d\n", s.frags);
    printf("  Largest free block : %d units\n", s.largest);
    printf("  External fragment. : %.1f %%\n", s.ext_frag);
    printf("  Requests           : %ld ok, %ld fragmentation failures, %ld out-of-memory\n",
           m->ok, m->frag_fail, m->oom_fail);
    printf("  Compactions        : %ld (%ld units moved)\n", m->compactions, m->units_moved);
}
