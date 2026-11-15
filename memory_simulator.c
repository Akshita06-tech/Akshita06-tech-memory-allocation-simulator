/*
 * memory_simulator.c
 * Intelligent Memory Allocation & Fragmentation Simulator
 *
 * Data structures : Array (simulated memory)  +  Linked list (block table)
 * Strategies      : First Fit, Best Fit, Worst Fit, Smart (decision engine)
 * Features        : split, free + merge, compaction, two failure types,
 *                   interactive mode, automatic experiments, timing.
 *
 * Build : gcc -Wall -O2 -o memsim memory_simulator.c
 * Run   : ./memsim               (interactive mode)
 *         ./memsim --demo        (compaction before/after demo)
 *         ./memsim --experiments (comparison tables)
 *         ./memsim --trace       (fragmentation after every step, CSV)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */
typedef enum { FIRST_FIT, BEST_FIT, WORST_FIT, SMART } Strategy;

const char *strategy_name(Strategy s) {
    switch (s) {
        case FIRST_FIT: return "First Fit";
        case BEST_FIT:  return "Best Fit";
        case WORST_FIT: return "Worst Fit";
        default:        return "Smart";
    }
}

/* One entry of the block table (linked list node). Sizes are in units. */
typedef struct Block {
    int start;              /* first unit of this block              */
    int size;               /* number of units                       */
    int is_free;            /* 1 = free, 0 = used                    */
    char owner[48];         /* name of the data stored here          */
    int data_len;           /* real bytes stored (<= size*unit_bytes) */
    struct Block *next;
} Block;

typedef struct {
    Block *head;            /* linked list: who owns which units     */
    unsigned char *mem;     /* array: the simulated memory itself    */
    int total_units;
    int unit_bytes;         /* bytes in one unit                     */
    Strategy last_used;     /* strategy actually used last time      */
    long ok, frag_fail, oom_fail;
    long compactions, units_moved;
} Memory;

typedef struct {
    int total_free, used, frags, largest;
    double ext_frag;        /* external fragmentation, percent       */
} Stats;

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                  */
/* ------------------------------------------------------------------ */
Block *new_block(int start, int size, int is_free) {
    Block *b = calloc(1, sizeof(Block));
    if (!b) { fprintf(stderr, "host out of memory\n"); exit(1); }
    b->start = start; b->size = size; b->is_free = is_free;
    return b;
}

Memory *mem_create(int total_units, int unit_bytes) {
    Memory *m = calloc(1, sizeof(Memory));
    if (!m) { fprintf(stderr, "host out of memory\n"); exit(1); }
    m->mem = calloc((size_t)total_units * unit_bytes, 1);
    if (!m->mem) { fprintf(stderr, "host out of memory\n"); exit(1); }
    m->total_units = total_units;
    m->unit_bytes = unit_bytes;
    m->head = new_block(0, total_units, 1);      /* one big free block */
    m->last_used = FIRST_FIT;
    return m;
}

void mem_destroy(Memory *m) {
    Block *b = m->head;
    while (b) { Block *n = b->next; free(b); b = n; }
    free(m->mem);
    free(m);
}

/* ------------------------------------------------------------------ */
/*  Statistics                                                        */
/* ------------------------------------------------------------------ */
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

/* Sanity check used by the tests: blocks are contiguous, cover all of
 * memory, and no two free blocks are neighbours (they must be merged). */
int mem_check_invariants(const Memory *m) {
    int addr = 0;
    for (const Block *b = m->head; b; b = b->next) {
        if (b->start != addr || b->size <= 0) return 0;
        if (b->next && b->is_free && b->next->is_free) return 0;
        addr += b->size;
    }
    return addr == m->total_units;
}

/* ------------------------------------------------------------------ */
/*  Merge (coalescing) and compaction                                 */
/* ------------------------------------------------------------------ */
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

/* Slide every used block to the start of memory; all free space becomes
 * one block at the end. The stored data is moved as well. */
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

/* The decision engine: look at the memory state and the request, then
 * choose the strategy. Sets *compact_first when compaction is needed. */
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

Block *mem_alloc(Memory *m, const char *owner, int units, Strategy s) {
    if (units <= 0) return NULL;
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
        if (st.total_free >= units) m->frag_fail++;   /* scattered    */
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

/* Copy real data into the block's part of the memory array. */
void mem_store(Memory *m, Block *b, const unsigned char *data, int len) {
    memcpy(m->mem + (size_t)b->start * m->unit_bytes, data, (size_t)len);
    b->data_len = len;
}

/* ------------------------------------------------------------------ */
/*  Release                                                           */
/* ------------------------------------------------------------------ */
Block *find_owner(const Memory *m, const char *owner) {
    for (Block *b = m->head; b; b = b->next)
        if (!b->is_free && strcmp(b->owner, owner) == 0) return b;
    return NULL;
}

/* Returns 1 on success, 0 if no such data (so a double free is safe). */
int mem_free(Memory *m, const char *owner) {
    Block *b = find_owner(m, owner);
    if (!b) return 0;
    memset(m->mem + (size_t)b->start * m->unit_bytes, 0,
           (size_t)b->size * m->unit_bytes);
    b->is_free = 1;
    b->owner[0] = '\0';
    b->data_len = 0;
    merge_free(m);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Display                                                           */
/* ------------------------------------------------------------------ */
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

void print_map(const Memory *m) {
    printf("  Start  Size  Status  Owner (bytes stored)\n");
    for (const Block *b = m->head; b; b = b->next) {
        if (b->is_free)
            printf("  %5d  %4d  FREE    -\n", b->start, b->size);
        else
            printf("  %5d  %4d  USED    %s (%d bytes)\n",
                   b->start, b->size, b->owner, b->data_len);
    }
    printf("  [");
    for (const Block *b = m->head; b; b = b->next)
        for (int i = 0; i < b->size; i++) putchar(b->is_free ? '.' : '#');
    printf("]   # = used, . = free\n");
}

/* ------------------------------------------------------------------ */
/*  Interactive mode: the user gives data, the program measures it    */
/* ------------------------------------------------------------------ */
int read_line(const char *prompt, char *buf, size_t n) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

int store_new(Memory *m, Strategy s, const char *name,
              const unsigned char *data, int len) {
    if (len <= 0) { printf("  Rejected: there is no data.\n"); return 0; }
    if (find_owner(m, name)) {
        printf("  Rejected: '%s' is already stored.\n", name);
        return 0;
    }
    int units = (len + m->unit_bytes - 1) / m->unit_bytes;   /* round up */
    Stats before = mem_stats(m);
    long comp_before = m->compactions;
    Block *b = mem_alloc(m, name, units, s);
    if (!b) {
        if (before.total_free >= units)
            printf("  FRAGMENTATION FAILURE: needs %d units, %d are free in total,\n"
                   "  but the largest free piece is only %d. Try Compact.\n",
                   units, before.total_free, before.largest);
        else
            printf("  OUT OF MEMORY: needs %d units, only %d are free.\n",
                   units, before.total_free);
        return 0;
    }
    mem_store(m, b, data, len);
    printf("  '%s': %d bytes -> %d units, placed at address %d using %s.\n",
           name, len, units, b->start, strategy_name(m->last_used));
    if (m->compactions > comp_before)
        printf("  (The decision engine compacted memory first.)\n");
    return 1;
}

void print_data(const Memory *m, const char *name) {
    const Block *b = find_owner(m, name);
    if (!b) { printf("  '%s' is not stored.\n", name); return; }
    const unsigned char *p = m->mem + (size_t)b->start * m->unit_bytes;
    printf("  '%s' at address %d: ", name, b->start);
    int shown = b->data_len > 300 ? 300 : b->data_len;
    for (int i = 0; i < shown; i++) putchar(isprint(p[i]) || p[i] == ' ' ? p[i] : '.');
    if (shown < b->data_len) printf("...");
    printf("\n");
}

void run_experiments(void);
void run_demo(void);

void print_menu(Strategy s) {
    printf("\n--- Menu (strategy: %s) ---\n", strategy_name(s));
    printf(" 1 Add text        2 Add file        3 Delete data\n");
    printf(" 4 Read data back  5 Show memory     6 Show statistics\n");
    printf(" 7 Compact memory  8 Change strategy 9 Run experiments\n");
    printf(" 0 Exit\n");
}

void interactive(void) {
    Memory *m = mem_create(64, 32);          /* 64 units x 32 bytes = 2 KB */
    Strategy strat = SMART;
    char line[1100], name[64];
    printf("Memory Allocation Simulator: %d units, 1 unit = %d bytes\n",
           m->total_units, m->unit_bytes);
    for (;;) {
        print_menu(strat);
        if (!read_line("Choice: ", line, sizeof line)) break;
        int c = atoi(line);
        if (c == 0 && line[0] != '0') { printf("  Please type a number.\n"); continue; }
        if (c == 0) break;
        switch (c) {
        case 1:
            if (!read_line("  Name: ", name, sizeof name)) goto done;
            if (!read_line("  Text: ", line, sizeof line)) goto done;
            store_new(m, strat, name, (unsigned char *)line, (int)strlen(line));
            break;
        case 2: {
            if (!read_line("  File path: ", line, sizeof line)) goto done;
            FILE *f = fopen(line, "rb");
            if (!f) { printf("  Cannot open '%s'.\n", line); break; }
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            long cap = (long)m->total_units * m->unit_bytes;
            const char *base = strrchr(line, '/');
            snprintf(name, sizeof name, "%.63s", base ? base + 1 : line);
            if (sz > cap) {
                printf("  OUT OF MEMORY: file is %ld bytes, whole memory holds %ld.\n", sz, cap);
                m->oom_fail++;
            } else if (sz > 0) {
                unsigned char *buf = malloc((size_t)sz);
                if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz)
                    store_new(m, strat, name, buf, (int)sz);
                free(buf);
            } else printf("  Rejected: the file is empty.\n");
            fclose(f);
            break;
        }
        case 3:
            if (!read_line("  Name to delete: ", name, sizeof name)) goto done;
            if (mem_free(m, name)) printf("  '%s' deleted, free neighbours merged.\n", name);
            else printf("  '%s' is not stored (nothing to delete).\n", name);
            break;
        case 4:
            if (!read_line("  Name to read: ", name, sizeof name)) goto done;
            print_data(m, name);
            break;
        case 5: print_map(m); break;
        case 6: print_stats(m); break;
        case 7: {
            Stats b4 = mem_stats(m);
            mem_compact(m);
            Stats af = mem_stats(m);
            printf("  Before: %d fragments, largest %d, external fragmentation %.1f %%\n",
                   b4.frags, b4.largest, b4.ext_frag);
            printf("  After : %d fragments, largest %d, external fragmentation %.1f %%\n",
                   af.frags, af.largest, af.ext_frag);
            break;
        }
        case 8:
            printf("  1 First Fit  2 Best Fit  3 Worst Fit  4 Smart\n");
            if (!read_line("  Choose: ", line, sizeof line)) goto done;
            c = atoi(line);
            if (c >= 1 && c <= 4) strat = (Strategy)(c - 1);
            else printf("  Unchanged.\n");
            break;
        case 9: run_experiments(); break;
        default: printf("  Unknown option.\n");
        }
    }
done:
    mem_destroy(m);
    printf("\nBye.\n");
}

/* ------------------------------------------------------------------ */
/*  Automatic mode: fair experiments                                  */
/* ------------------------------------------------------------------ */
typedef struct { uint64_t s; } Rng;
void rng_seed(Rng *r, unsigned seed) { r->s = seed * 2654435761ULL + 88172645463325252ULL; }
uint32_t rng_next(Rng *r) {
    r->s = r->s * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(r->s >> 33);
}

typedef struct { int is_alloc, id, size; } Op;
typedef enum { PAT_SMALL, PAT_LARGE, PAT_MIXED } Pattern;
const char *pattern_name(Pattern p) {
    return p == PAT_SMALL ? "mostly small" : p == PAT_LARGE ? "mostly large" : "mixed";
}

int pick_size(Rng *r, Pattern p) {
    int small = 1 + (int)(rng_next(r) % 4);          /* 1..4   */
    int large = 8 + (int)(rng_next(r) % 17);         /* 8..24  */
    if (p == PAT_SMALL) return small;
    if (p == PAT_LARGE) return large;
    return (rng_next(r) % 100 < 70) ? small : large; /* 70% small */
}

/* Same seed + pattern = same list of operations, for every strategy. */
void make_workload(Op *ops, int n, Pattern p, unsigned seed) {
    Rng r; rng_seed(&r, seed);
    int *live = malloc((size_t)n * sizeof(int));
    int nlive = 0, next_id = 1;
    for (int i = 0; i < n; i++) {
        if (nlive > 0 && rng_next(&r) % 100 < 40) {        /* 40% free */
            int k = (int)(rng_next(&r) % nlive);
            ops[i].is_alloc = 0; ops[i].id = live[k]; ops[i].size = 0;
            live[k] = live[--nlive];
        } else {                                           /* 60% alloc */
            ops[i].is_alloc = 1; ops[i].id = next_id;
            ops[i].size = pick_size(&r, p);
            live[nlive++] = next_id++;
        }
    }
    free(live);
}

typedef struct {
    long ok, frag_fail, oom_fail, compactions, units_moved;
    double avg_ext, final_ext;
    int final_frags, final_largest;
} Result;

/* trace may be NULL; otherwise it receives the fragmentation after each op */
Result run_workload(const Op *ops, int n, Strategy s, int mem_units, double *trace) {
    Result r; memset(&r, 0, sizeof r);
    Memory *m = mem_create(mem_units, 1);
    char *live = calloc((size_t)n + 1, 1);
    char name[24];
    double sum = 0;
    for (int i = 0; i < n; i++) {
        snprintf(name, sizeof name, "P%d", ops[i].id);
        if (ops[i].is_alloc) {
            if (mem_alloc(m, name, ops[i].size, s)) live[ops[i].id] = 1;
        } else if (live[ops[i].id]) {
            mem_free(m, name);
            live[ops[i].id] = 0;
        }
        if (trace) { trace[i] = mem_stats(m).ext_frag; sum += trace[i]; }
    }
    Stats st = mem_stats(m);
    r.ok = m->ok; r.frag_fail = m->frag_fail; r.oom_fail = m->oom_fail;
    r.compactions = m->compactions; r.units_moved = m->units_moved;
    r.avg_ext = (trace && n) ? sum / n : 0;
    r.final_ext = st.ext_frag; r.final_frags = st.frags; r.final_largest = st.largest;
    free(live); mem_destroy(m);
    return r;
}

double time_run_ms(const Op *ops, int n, Strategy s, int mem_units, int reps) {
    clock_t t0 = clock();
    for (int i = 0; i < reps; i++) run_workload(ops, n, s, mem_units, NULL);
    return 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / reps;
}

#define EXP_MEM 100          /* units of memory in the comparison runs */

void run_experiments(void) {
    Strategy all[4] = { FIRST_FIT, BEST_FIT, WORST_FIT, SMART };
    int sizes[3] = { 10, 50, 200 };

    printf("\nTABLE 1: one workload (mixed, seed 42), memory = %d units\n", EXP_MEM);
    printf("%-6s %-10s %4s %6s %5s %9s %9s %8s %6s\n", "Reqs", "Strategy",
           "OK", "FragF", "OOM", "AvgFrag%", "FinalFr%", "Largest", "Compct");
    for (int a = 0; a < 3; a++) {
        int n = sizes[a];
        Op *ops = malloc((size_t)n * sizeof(Op));
        double *tr = malloc((size_t)n * sizeof(double));
        make_workload(ops, n, PAT_MIXED, 42);
        for (int k = 0; k < 4; k++) {
            Result r = run_workload(ops, n, all[k], EXP_MEM, tr);
            printf("%-6d %-10s %4ld %6ld %5ld %9.1f %9.1f %8d %6ld\n", n,
                   strategy_name(all[k]), r.ok, r.frag_fail, r.oom_fail,
                   r.avg_ext, r.final_ext, r.final_largest, r.compactions);
        }
        free(ops); free(tr);
    }

    printf("\nTABLE 2: average over 30 seeds, 200 requests, memory = %d units\n", EXP_MEM);
    printf("%-13s %-10s %7s %7s %7s %9s %7s\n", "Pattern", "Strategy",
           "OK", "FragF", "OOM", "AvgFrag%", "Compct");
    Pattern pats[3] = { PAT_SMALL, PAT_LARGE, PAT_MIXED };
    for (int p = 0; p < 3; p++) {
        double sum[4][5]; memset(sum, 0, sizeof sum);
        for (unsigned seed = 1; seed <= 30; seed++) {
            Op ops[200]; double tr[200];
            make_workload(ops, 200, pats[p], seed);
            for (int k = 0; k < 4; k++) {
                Result r = run_workload(ops, 200, all[k], EXP_MEM, tr);
                sum[k][0] += (double)r.ok;   sum[k][1] += (double)r.frag_fail;
                sum[k][2] += (double)r.oom_fail; sum[k][3] += r.avg_ext;
                sum[k][4] += (double)r.compactions;
            }
        }
        for (int k = 0; k < 4; k++)
            printf("%-13s %-10s %7.1f %7.1f %7.1f %9.1f %7.1f\n", pattern_name(pats[p]),
                   strategy_name(all[k]), sum[k][0] / 30, sum[k][1] / 30,
                   sum[k][2] / 30, sum[k][3] / 30, sum[k][4] / 30);
    }

    printf("\nTABLE 3: running time per full workload (ms), memory = 1000 units\n");
    printf("%-9s %10s %10s %10s %10s\n", "Requests", "First Fit", "Best Fit", "Worst Fit", "Smart");
    int big[3] = { 1000, 5000, 10000 };
    for (int a = 0; a < 3; a++) {
        int n = big[a];
        Op *ops = malloc((size_t)n * sizeof(Op));
        make_workload(ops, n, PAT_MIXED, 42);
        printf("%-9d", n);
        for (int k = 0; k < 4; k++)
            printf(" %10.3f", time_run_ms(ops, n, all[k], 1000, 20));
        printf("\n");
        free(ops);
    }
}

/* Prints CSV: fragmentation after every step, for the line chart. */
void run_trace(void) {
    Strategy all[4] = { FIRST_FIT, BEST_FIT, WORST_FIT, SMART };
    int n = 200;
    Op ops[200]; double tr[4][200];
    make_workload(ops, n, PAT_MIXED, 42);
    for (int k = 0; k < 4; k++) run_workload(ops, n, all[k], EXP_MEM, tr[k]);
    printf("step,First Fit,Best Fit,Worst Fit,Smart\n");
    for (int i = 0; i < n; i++)
        printf("%d,%.2f,%.2f,%.2f,%.2f\n", i + 1, tr[0][i], tr[1][i], tr[2][i], tr[3][i]);
}

/* A small story that shows a fragmentation failure fixed by compaction. */
void run_demo(void) {
    Memory *m = mem_create(100, 1);
    char name[8];
    printf("Step 1: eight processes of 10 units each (First Fit)\n");
    for (int i = 1; i <= 8; i++) { snprintf(name, sizeof name, "P%d", i); mem_alloc(m, name, 10, FIRST_FIT); }
    print_map(m);
    printf("\nStep 2: P2, P4, P6, P8 finish (their blocks are freed)\n");
    mem_free(m, "P2"); mem_free(m, "P4"); mem_free(m, "P6"); mem_free(m, "P8");
    print_map(m); print_stats(m);
    printf("\nStep 3: a new process needs 40 units\n");
    if (!mem_alloc(m, "P9", 40, FIRST_FIT)) {
        Stats s = mem_stats(m);
        printf("  FAILED although %d units are free: largest free piece is only %d\n",
               s.total_free, s.largest);
    }
    printf("\nStep 4: compact memory\n");
    mem_compact(m);
    print_map(m); print_stats(m);
    printf("\nStep 5: ask for 40 units again\n");
    Block *b = mem_alloc(m, "P9", 40, FIRST_FIT);
    printf("  %s\n", b ? "SUCCESS: P9 placed" : "failed");
    print_map(m);
    mem_destroy(m);
}

#ifndef MEMSIM_TEST
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) run_demo();
    else if (argc > 1 && strcmp(argv[1], "--experiments") == 0) run_experiments();
    else if (argc > 1 && strcmp(argv[1], "--trace") == 0) run_trace();
    else interactive();
    return 0;
}
#endif
