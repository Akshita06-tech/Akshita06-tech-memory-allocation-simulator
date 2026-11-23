/* memory.h - M2: memory manager (block list + simulated memory array)
 *
 * The shared record types live here (Strategy, Block, Memory, Stats).
 * Everyone includes this file, so change it ONLY after telling all members.
 *
 * Tags used in the comments:
 *   [EXISTING] moved unchanged from memory_simulator.c
 *   [CHANGED]  existed before, changed for the combined design
 *   [NEW]      new for the combined design
 */
#ifndef MEMORY_H
#define MEMORY_H

/* [NEW] sizes: 1 unit = 1 KB in the experiments and the demo */
#define UNIT_KB      1
#define POOL_DEMO    2048     /* units for the live demo               */
#define POOL_SMALL   1024     /* experiment sizes from the guide       */
#define POOL_MEDIUM  4096
#define POOL_LARGE   16384
#define SIZE_ONLY    0        /* unit_bytes = 0: remember sizes only, store no data */

typedef enum { FIRST_FIT, BEST_FIT, WORST_FIT, SMART } Strategy;   /* [EXISTING] */

/* One entry of the block table (linked list node). Sizes are in units. */
typedef struct Block {                                              /* [EXISTING] */
    int start;              /* first unit of this block                  */
    int size;               /* number of units                           */
    int is_free;            /* 1 = free, 0 = used                        */
    char owner[48];         /* name of the owner; empty when free        */
    int data_len;           /* real bytes of the file/data (kept even in size-only mode) */
    struct Block *next;
} Block;

typedef struct {                                                    /* [CHANGED] one new field */
    Block *head;            /* linked list: who owns which units         */
    unsigned char *mem;     /* array: the data. NULL in size-only mode   */
    int total_units;
    int unit_bytes;         /* bytes in one unit; 0 = size-only mode     */
    Strategy last_used;     /* strategy actually used last time          */
    long ok, frag_fail, oom_fail;   /* counted per ATTEMPT (a retry counts again) */
    long compactions, units_moved;
    long search_steps;      /* [NEW] blocks examined; M3 adds to it in find_block */
} Memory;

typedef struct {                                                    /* [EXISTING] */
    int total_free, used, frags, largest;
    double ext_frag;        /* external fragmentation, percent           */
} Stats;

/* ---- create / destroy ---- */
Block  *new_block(int start, int size, int is_free);                /* [EXISTING] */
Memory *mem_create(int total_units, int unit_bytes);                /* [CHANGED] */
void    mem_destroy(Memory *m);                                     /* [CHANGED] */

/* ---- allocate, store, free, compact ---- */
Block  *mem_alloc(Memory *m, const char *owner, int units, Strategy s);   /* [CHANGED] */
int     mem_store(Memory *m, Block *b, const unsigned char *data, int len); /* [CHANGED] returns 1/0 */
int     mem_free(Memory *m, const char *owner);                     /* [CHANGED] */
void    merge_free(Memory *m);                                      /* [EXISTING] */
void    mem_compact(Memory *m);                                     /* [CHANGED] */

/* ---- look up ---- */
Block  *find_owner(const Memory *m, const char *owner);            /* [EXISTING] */
int     mem_lookup(const Memory *m, const char *owner, int *start, int *end);  /* [NEW] */

/* ---- checks and display ---- */
int     mem_check_invariants(const Memory *m);                      /* [CHANGED] */
void    print_map(const Memory *m);                                 /* [CHANGED] */

#endif
