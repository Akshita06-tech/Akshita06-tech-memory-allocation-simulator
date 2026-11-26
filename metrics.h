#ifndef METRICS_H
#define METRICS_H
#include "common.h"
void log_begin(const char *path);
void log_end(void);
void log_event(int tick, int pid, const char *msg);
void log_snapshot(int tick, const Stats *stats);
void metrics_print(const Metrics *m);
void run_comparison(const Proc *workload, int n, int pool_units);
void run_experiments(void);
#endif
