#ifndef POLICY_H
#define POLICY_H
#include "common.h"
#include "memory.h"

Block *find_block(const Memory *m, int units, Strategy strategy, int *examined);
Strategy engine_choose(const Memory *m, int units, int *compact_first);
int failure_kind(const Memory *m, int units);
void calculate_rank_sums(const Metrics results[4], int rank_sum[4]);
Strategy recommend(const Metrics results[], int n, char *why, size_t why_size);
#endif
