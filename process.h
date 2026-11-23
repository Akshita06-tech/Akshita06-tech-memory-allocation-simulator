#ifndef PROCESS_H
#define PROCESS_H
#include "common.h"
#include "memory.h"
#include "policy.h"

long measure_file_size(const char *path);
int bytes_to_units(long bytes, int unit);
int generate_processes(Proc *out, int n, unsigned seed, int pattern, ProfileMode mode);
void queue_init(ProcQueue *q);
int queue_push(ProcQueue *q, int pid);
int queue_pop(ProcQueue *q, int *pid);
int queue_size(const ProcQueue *q);
void sim_run(Proc *p, int n, Memory *m, Strategy strategy, Metrics *out);
int add_real_file_process(Proc *p, int *count, const char *path, int duration, int pool_units);
void refresh_process_addresses(Proc *p, int n, const Memory *m);
#endif
