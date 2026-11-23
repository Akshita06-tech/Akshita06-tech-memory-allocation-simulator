#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define MAX_PROCS 512
#define NAME_LEN 64
#define TYPE_LEN 16
#define OWNER_LEN 64
#define LOG_FILE "events.log"
#define KB 1024

typedef enum { P_NEW, P_READY, P_RUNNING, P_WAITING, P_COMPLETED, P_REJECTED } PState;
typedef enum { FIRST_FIT, BEST_FIT, WORST_FIT, SMART_FIT } Strategy;
typedef enum { FILE_PROFILES, PROGRAM_PROFILES } ProfileMode;
typedef enum { LIGHT_PATTERN, BALANCED_PATTERN, HEAVY_PATTERN } Pattern;

typedef struct {
    int id;
    char name[NAME_LEN];
    char type[TYPE_LEN];
    int from_real_file;
    long size_bytes;
    int units;
    int duration;
    int arrive_tick, start_tick, finish_tick;
    int start_addr, end_addr;
    PState state;
    int first_try_failure; /* 0 none, 1 fragmentation, 2 out-of-memory */
    int wait_ticks;
    int queued;
} Proc;

typedef struct Profile {
    const char *type, *label;
    int min_kb, max_kb, min_ticks, max_ticks, weight;
} Profile;

typedef struct Block {
    int start, size, is_free;
    char owner[OWNER_LEN];
    int data_len;
    struct Block *next;
} Block;

typedef struct {
    int total_units, unit_bytes;
    unsigned char *pool; /* allocated only for optional storage mode */
    Block *head;
    long units_moved;
    int compactions;
} Memory;

typedef struct {
    int total_free, used_memory, free_fragments, largest_free;
    double external_fragmentation;
} Stats;

typedef struct {
    int process_count, accepted, first_try_success, fragmentation_failures;
    int oom_failures, waiting_processes, total_wait_ticks, waiters;
    int makespan, total_searches, allocation_attempts, compactions;
    long units_moved;
    double avg_wait, success_rate, avg_search_length;
    double avg_fragmentation;
} Metrics;

typedef struct {
    int items[MAX_PROCS];
    int front, rear, count;
} ProcQueue;

const char *state_name(PState s);
const char *strategy_name(Strategy s);

#endif
