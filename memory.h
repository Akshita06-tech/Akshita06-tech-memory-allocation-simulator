#ifndef MEMORY_H
#define MEMORY_H
#include "common.h"

Memory *mem_create(int units, int unit_bytes);
void mem_destroy(Memory *m);
Block *mem_alloc(Memory *m, const char *owner, int units, Strategy strategy);
Block *mem_alloc_selected(Memory *m, const char *owner, int units, Block *candidate);
int mem_free(Memory *m, const char *owner);
void mem_compact(Memory *m);
int mem_lookup(const Memory *m, const char *owner, int *start, int *end);
/* Returns 0 when valid; nonzero when an invariant is broken. */
int mem_check_invariants(const Memory *m);
Stats mem_stats(const Memory *m);
void mem_print_map(const Memory *m);
int mem_write_bytes(Memory *m, const char *owner, const unsigned char *data, int len);
int mem_read_bytes(const Memory *m, const char *owner, unsigned char *out, int capacity, int *actual_len);
#endif
