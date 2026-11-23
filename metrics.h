/* metrics.h - M3: statistics (this header is shown only so M2 can compile) */
#ifndef METRICS_H
#define METRICS_H
#include "memory.h"

Stats mem_stats(const Memory *m);
void  print_stats(const Memory *m);

#endif
