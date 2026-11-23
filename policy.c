/* policy.c - M3 (existing code, as it will be after M4's split) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "policy.h"
#include "metrics.h"

const char *strategy_name(Strategy s) {
    switch (s) {
        case FIRST_FIT: return "First Fit";
        case BEST_FIT:  return "Best Fit";
        case WORST_FIT: return "Worst Fit";
        default:        return "Smart";
    }
}

Block *find_block(Memory *m, int units, Strategy s) {
    Block *best = NULL;
    for (Block *b = m->head; b; b = b->next) {
        if (!b->is_free || b->size < units) continue;
        if (s == FIRST_FIT) return b;                       /* first one */
        if (!best ||
            (s == BEST_FIT  && b->size < best->size) ||     /* smallest  */
            (s == WORST_FIT && b->size > best->size))       /* largest   */
            best = b;
    }
    return best;
}

Strategy engine_choose(const Memory *m, int units, int *compact_first) {
    Stats st = mem_stats(m);
    /* Rule 1: enough memory in total, but no single piece big enough. */
    *compact_first = (units > st.largest && units <= st.total_free);
    if (*compact_first) return BEST_FIT;
    /* Rule 2: memory mostly empty -> First Fit is fastest and safe.   */
    if (st.total_free * 100 >= m->total_units * 60) return FIRST_FIT;
    /* Rule 3: memory getting full -> Best Fit keeps big pieces whole. */
    return BEST_FIT;
}
