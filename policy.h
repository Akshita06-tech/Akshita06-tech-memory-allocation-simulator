/* policy.h - M3: which block to choose (this header is shown only so M2 can compile) */
#ifndef POLICY_H
#define POLICY_H
#include "memory.h"

const char *strategy_name(Strategy s);
Block *find_block(Memory *m, int units, Strategy s);
Strategy engine_choose(const Memory *m, int units, int *compact_first);

#endif
