#include "../common.h"
#include "../memory.h"
#include "../policy.h"
#include "../process.h"

#include <ctype.h>

/* Reserved System area percentage (design assumption, not physical RAM). */
#define SYSTEM_RESERVE_PCT 12.5
#define SYSTEM_OWNER "System"
#define DEFAULT_POOL_KB 65536
#define MAX_INTERACTIVE_PROCS 4096

typedef struct {
    int id;
    char owner[OWNER_LEN];
    long size_bytes;
    int units_kb;
    int start_kb;
    int end_kb;
    Strategy strategy;
} ActiveProc;

static Memory *g_mem = NULL;
static int g_system_kb = 0;
static int g_next_id = 1;
static int g_frag_failures = 0;
static int g_oom_failures = 0;
static int g_wait_ticks_total = 0;
static int g_waiters = 0;
static ActiveProc g_procs[MAX_INTERACTIVE_PROCS];
static int g_proc_count = 0;

static int reserved_units_for(int pool_kb) {
    int r = (int)((pool_kb * SYSTEM_RESERVE_PCT) / 100.0 + 0.5);
    if (r < 1) r = 1;
    if (r >= pool_kb) r = pool_kb - 1;
    return r;
}

static void json_escape_print(const char *s) {
    putchar('"');
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            switch (*p) {
                case '"':  fputs("\\\"", stdout); break;
                case '\\': fputs("\\\\", stdout); break;
                case '\b': fputs("\\b", stdout); break;
                case '\f': fputs("\\f", stdout); break;
                case '\n': fputs("\\n", stdout); break;
                case '\r': fputs("\\r", stdout); break;
                case '\t': fputs("\\t", stdout); break;
                default:
                    if (*p < 0x20) printf("\\u%04x", (unsigned)*p);
                    else putchar((int)*p);
                    break;
            }
        }
    }
    putchar('"');
}

static const char *strategy_key(Strategy s) {
    switch (s) {
        case FIRST_FIT: return "first";
        case BEST_FIT:  return "best";
        case WORST_FIT: return "worst";
        case SMART_FIT: return "smart";
        default:        return "first";
    }
}

static int parse_strategy(const char *arg, Strategy *out) {
    if (!arg || !out) return 0;
    if (strcmp(arg, "0") == 0 || strcmp(arg, "first") == 0) { *out = FIRST_FIT; return 1; }
    if (strcmp(arg, "1") == 0 || strcmp(arg, "best") == 0)  { *out = BEST_FIT; return 1; }
    if (strcmp(arg, "2") == 0 || strcmp(arg, "worst") == 0) { *out = WORST_FIT; return 1; }
    if (strcmp(arg, "3") == 0 || strcmp(arg, "smart") == 0) { *out = SMART_FIT; return 1; }
    return 0;
}

static void print_stats_obj(const Stats *st, int pool_kb) {
    printf("{\"pool_kb\":%d,\"system_kb\":%d,\"total_free_kb\":%d,\"used_kb\":%d,"
           "\"free_fragments\":%d,\"largest_free_kb\":%d,\"fragmentation_pct\":%.4f}",
           pool_kb, g_system_kb, st->total_free, st->used_memory,
           st->free_fragments, st->largest_free, st->external_fragmentation);
}

static const ActiveProc *find_active_proc(const char *owner) {
    for (int i = 0; i < g_proc_count; i++) {
        if (strcmp(g_procs[i].owner, owner) == 0) return &g_procs[i];
    }
    return NULL;
}

static void refresh_active_procs(void) {
    for (int i = 0; i < g_proc_count; i++) {
        int s = 0, e = 0;
        if (mem_lookup(g_mem, g_procs[i].owner, &s, &e)) {
            g_procs[i].start_kb = s;
            g_procs[i].end_kb = e;
        }
    }
}

static void remove_active_proc(const char *owner) {
    for (int i = 0; i < g_proc_count; i++) {
        if (strcmp(g_procs[i].owner, owner) == 0) {
            for (int j = i; j + 1 < g_proc_count; j++) {
                g_procs[j] = g_procs[j + 1];
            }
            g_proc_count--;
            return;
        }
    }
}

static void print_blocks_array(const Memory *m) {
    putchar('[');
    int first = 1;
    for (const Block *b = m ? m->head : NULL; b; b = b->next) {
        if (!first) putchar(',');
        first = 0;
        int is_system = (!b->is_free && strcmp(b->owner, SYSTEM_OWNER) == 0);
        const ActiveProc *ap = (!b->is_free && !is_system) ? find_active_proc(b->owner) : NULL;
        printf("{\"start_kb\":%d,\"end_kb\":%d,\"size_kb\":%d,\"is_free\":%s,\"is_system\":%s,\"owner\":",
               b->start, b->start + b->size, b->size,
               b->is_free ? "true" : "false",
               is_system ? "true" : "false");
        json_escape_print(b->is_free ? "" : b->owner);
        printf(",\"pid\":%d}", is_system ? 0 : (ap ? ap->id : -1));
    }
    putchar(']');
}

static void print_procs_array(void) {
    putchar('[');
    for (int i = 0; i < g_proc_count; i++) {
        if (i > 0) putchar(',');
        const ActiveProc *p = &g_procs[i];
        printf("{\"pid\":%d,\"owner\":", p->id);
        json_escape_print(p->owner);
        printf(",\"size_bytes\":%ld,\"units_kb\":%d,\"start_kb\":%d,\"end_kb\":%d,\"strategy\":",
               p->size_bytes, p->units_kb, p->start_kb, p->end_kb);
        json_escape_print(strategy_name(p->strategy));
        putchar('}');
    }
    putchar(']');
}

static void print_state_fields(void) {
    Stats st = mem_stats(g_mem);
    int inv = mem_check_invariants(g_mem);
    double avg_wait = g_waiters ? (double)g_wait_ticks_total / g_waiters : 0.0;

    printf("\"pool_kb\":%d,\"system_kb\":%d,\"system_reserve_pct\":%.1f,"
           "\"unit_bytes\":%d,\"invariant_ok\":%s,"
           "\"stats\":{\"total_free_kb\":%d,\"used_kb\":%d,\"largest_free_kb\":%d,"
           "\"free_fragments\":%d,\"fragmentation_pct\":%.4f,"
           "\"frag_failures\":%d,\"oom_failures\":%d,"
           "\"avg_wait_ticks\":%.2f,\"compactions\":%d,\"units_moved_kb\":%ld},"
           "\"blocks\":",
           g_mem->total_units, g_system_kb, SYSTEM_RESERVE_PCT,
           g_mem->unit_bytes, inv == 0 ? "true" : "false",
           st.total_free, st.used_memory, st.largest_free,
           st.free_fragments, st.external_fragmentation,
           g_frag_failures, g_oom_failures,
           avg_wait, g_mem->compactions, g_mem->units_moved);
    print_blocks_array(g_mem);
    fputs(",\"processes\":", stdout);
    print_procs_array();
}

static int init_pool(int pool_kb) {
    if (pool_kb <= 0) return 0;
    Memory *next = mem_create(pool_kb, KB);
    if (!next) return 0;
    int sys_kb = reserved_units_for(pool_kb);
    if (!mem_alloc(next, SYSTEM_OWNER, sys_kb, FIRST_FIT)) {
        mem_destroy(next);
        return 0;
    }
    if (g_mem) mem_destroy(g_mem);
    g_mem = next;
    g_system_kb = sys_kb;
    g_next_id = 1;
    g_frag_failures = 0;
    g_oom_failures = 0;
    g_wait_ticks_total = 0;
    g_waiters = 0;
    g_proc_count = 0;
    return 1;
}

typedef struct {
    int start_kb;
    int end_kb;
    int size_kb;
    int is_free;
    int is_system;
    char owner[OWNER_LEN];
    int pid;
} BlockSnap;

static int capture_blocks(const Memory *m, BlockSnap **out_arr) {
    int count = 0;
    for (const Block *b = m->head; b; b = b->next) count++;
    BlockSnap *arr = calloc((size_t)(count > 0 ? count : 1), sizeof(BlockSnap));
    if (!arr) return -1;
    int idx = 0;
    for (const Block *b = m->head; b; b = b->next) {
        arr[idx].start_kb = b->start;
        arr[idx].end_kb = b->start + b->size;
        arr[idx].size_kb = b->size;
        arr[idx].is_free = b->is_free;
        arr[idx].is_system = (!b->is_free && strcmp(b->owner, SYSTEM_OWNER) == 0);
        if (!b->is_free) snprintf(arr[idx].owner, OWNER_LEN, "%s", b->owner);
        const ActiveProc *ap = (!b->is_free && !arr[idx].is_system) ? find_active_proc(b->owner) : NULL;
        arr[idx].pid = arr[idx].is_system ? 0 : (ap ? ap->id : -1);
        idx++;
    }
    *out_arr = arr;
    return count;
}

static void print_snap_array(const BlockSnap *arr, int count) {
    putchar('[');
    for (int i = 0; i < count; i++) {
        if (i > 0) putchar(',');
        printf("{\"start_kb\":%d,\"end_kb\":%d,\"size_kb\":%d,\"is_free\":%s,\"is_system\":%s,\"owner\":",
               arr[i].start_kb, arr[i].end_kb, arr[i].size_kb,
               arr[i].is_free ? "true" : "false",
               arr[i].is_system ? "true" : "false");
        json_escape_print(arr[i].is_free ? "" : arr[i].owner);
        printf(",\"pid\":%d}", arr[i].pid);
    }
    putchar(']');
}

static int check_owner_valid(const char *owner, char *err, size_t err_cap) {
    if (!owner || !*owner) {
        snprintf(err, err_cap, "File name cannot be empty.");
        return 0;
    }
    if (strlen(owner) >= OWNER_LEN) {
        snprintf(err, err_cap, "File name exceeds %d characters.", OWNER_LEN - 1);
        return 0;
    }
    if (strcmp(owner, SYSTEM_OWNER) == 0) {
        snprintf(err, err_cap, "Name \"System\" is reserved for the device system area.");
        return 0;
    }
    if (mem_lookup(g_mem, owner, NULL, NULL)) {
        snprintf(err, err_cap, "A file named \"%s\" is already stored in device memory.", owner);
        return 0;
    }
    return 1;
}

static void handle_precheck(const char *strat_arg, const char *name, const char *path) {
    Strategy strat = FIRST_FIT;
    if (!parse_strategy(strat_arg, &strat)) {
        fputs("{\"ok\":false,\"error\":\"Invalid allocation strategy.\"}\n", stdout);
        return;
    }

    long size_bytes = measure_file_size(path);
    if (size_bytes < 0) {
        fputs("{\"ok\":false,\"reason\":\"unreadable\",\"error\":\"File could not be read.\"}\n", stdout);
        return;
    }
    if (size_bytes == 0) {
        fputs("{\"ok\":false,\"reason\":\"empty\",\"size_bytes\":0,\"units_kb\":0,"
              "\"error\":\"Empty file (0 bytes) cannot be stored in device memory.\"}\n", stdout);
        return;
    }

    int units_kb = bytes_to_units(size_bytes, KB);
    Stats st = mem_stats(g_mem);

    char name_err[160] = "";
    int name_ok = check_owner_valid(name, name_err, sizeof(name_err));

    int compact_first = 0;
    Strategy effective = (strat == SMART_FIT) ? engine_choose(g_mem, units_kb, &compact_first) : strat;
    int examined = 0;
    Block *cand = NULL;
    if (!compact_first && units_kb <= g_mem->total_units) {
        cand = find_block(g_mem, units_kb, effective, &examined);
    }

    int fk = 0;
    int fits = 0;
    if (units_kb > g_mem->total_units) {
        fk = 2;
        fits = 0;
    } else if (cand != NULL) {
        fits = 1;
    } else if (compact_first) {
        /* engine_choose only sets compact_first when units <= total_free. */
        fits = 1;
    } else {
        fk = failure_kind(g_mem, units_kb);
        fits = 0;
    }

    printf("{\"ok\":true,\"name\":");
    json_escape_print(name);
    printf(",\"name_valid\":%s,\"name_error\":", name_ok ? "true" : "false");
    json_escape_print(name_err);
    printf(",\"size_bytes\":%ld,\"units_kb\":%d,\"pool_kb\":%d,"
           "\"total_free_kb\":%d,\"largest_free_kb\":%d,\"free_fragments\":%d,"
           "\"strategy\":",
           size_bytes, units_kb, g_mem->total_units,
           st.total_free, st.largest_free, st.free_fragments);
    json_escape_print(strategy_key(strat));
    printf(",\"strategy_label\":");
    json_escape_print(strategy_name(strat));
    printf(",\"effective_strategy\":");
    json_escape_print(strategy_name(effective));
    printf(",\"smart_compact_first\":%s,\"will_fit\":%s,\"failure_kind\":%d,"
           "\"blocks_searched\":%d,\"candidate\":",
           compact_first ? "true" : "false",
           (fits && name_ok) ? "true" : "false",
           fk, examined);

    if (cand) {
        printf("{\"start_kb\":%d,\"end_kb\":%d,\"size_kb\":%d,\"leftover_kb\":%d}",
               cand->start, cand->start + cand->size, cand->size, cand->size - units_kb);
    } else {
        fputs("null", stdout);
    }
    fputs("}\n", stdout);
}

static void handle_alloc(const char *strat_arg, const char *name, const char *path) {
    Strategy strat = FIRST_FIT;
    if (!parse_strategy(strat_arg, &strat)) {
        fputs("{\"ok\":false,\"error\":\"Invalid allocation strategy.\"}\n", stdout);
        return;
    }

    char name_err[160] = "";
    if (!check_owner_valid(name, name_err, sizeof(name_err))) {
        fputs("{\"ok\":false,\"reason\":\"invalid_name\",\"error\":", stdout);
        json_escape_print(name_err);
        fputs("}\n", stdout);
        return;
    }

    long size_bytes = measure_file_size(path);
    if (size_bytes <= 0) {
        fputs("{\"ok\":false,\"reason\":\"empty\",\"error\":\"Empty or unreadable file rejected (0 bytes).\"}\n", stdout);
        return;
    }

    int units_kb = bytes_to_units(size_bytes, KB);
    if (units_kb <= 0) {
        fputs("{\"ok\":false,\"reason\":\"invalid_size\",\"error\":\"Invalid file size.\"}\n", stdout);
        return;
    }

    if (g_proc_count >= MAX_INTERACTIVE_PROCS) {
        fputs("{\"ok\":false,\"reason\":\"limit\",\"error\":\"Maximum active allocation count reached.\"}\n", stdout);
        return;
    }

    Stats pre_stats = mem_stats(g_mem);

    if (units_kb > g_mem->total_units) {
        g_oom_failures++;
        printf("{\"ok\":false,\"failure_kind\":2,\"reason\":\"exceeds_pool\","
               "\"requested_kb\":%d,\"pool_kb\":%d,\"total_free_kb\":%d,\"largest_free_kb\":%d,"
               "\"blocks_searched\":0,\"can_compact_and_retry\":false,",
               units_kb, g_mem->total_units, pre_stats.total_free, pre_stats.largest_free);
        print_state_fields();
        fputs("}\n", stdout);
        return;
    }

    int compact_first = 0;
    Strategy effective = (strat == SMART_FIT) ? engine_choose(g_mem, units_kb, &compact_first) : strat;
    long moved_before = g_mem->units_moved;
    int smart_compacted = 0;
    long smart_units_moved = 0;

    if (compact_first) {
        mem_compact(g_mem);
        refresh_active_procs();
        smart_compacted = 1;
        smart_units_moved = g_mem->units_moved - moved_before;
    }

    int examined = 0;
    Block *cand = find_block(g_mem, units_kb, effective, &examined);
    if (!cand) {
        int fk = failure_kind(g_mem, units_kb);
        if (fk == 1) g_frag_failures++;
        else g_oom_failures++;
        Stats now = mem_stats(g_mem);
        printf("{\"ok\":false,\"failure_kind\":%d,\"reason\":%s,"
               "\"requested_kb\":%d,\"pool_kb\":%d,\"total_free_kb\":%d,\"largest_free_kb\":%d,"
               "\"free_fragments\":%d,\"blocks_searched\":%d,\"can_compact_and_retry\":%s,",
               fk, fk == 1 ? "\"fragmentation\"" : "\"oom\"",
               units_kb, g_mem->total_units, now.total_free, now.largest_free,
               now.free_fragments, examined, fk == 1 ? "true" : "false");
        print_state_fields();
        fputs("}\n", stdout);
        return;
    }

    int split_from_start = cand->start;
    int split_from_size = cand->size;
    Block *placed = mem_alloc_selected(g_mem, name, units_kb, cand);
    if (!placed) {
        fputs("{\"ok\":false,\"reason\":\"alloc_failed\",\"error\":\"Allocation failed in memory manager.\"}\n", stdout);
        return;
    }

    int pid = g_next_id++;
    ActiveProc *ap = &g_procs[g_proc_count++];
    ap->id = pid;
    snprintf(ap->owner, OWNER_LEN, "%s", name);
    ap->size_bytes = size_bytes;
    ap->units_kb = units_kb;
    ap->start_kb = placed->start;
    ap->end_kb = placed->start + placed->size;
    ap->strategy = strat;

    printf("{\"ok\":true,\"receipt\":{\"pid\":%d,\"owner\":", pid);
    json_escape_print(name);
    printf(",\"size_bytes\":%ld,\"units_kb\":%d,\"start_kb\":%d,\"end_kb\":%d,"
           "\"strategy\":",
           size_bytes, units_kb, placed->start, placed->start + placed->size);
    json_escape_print(strategy_name(strat));
    printf(",\"effective_strategy\":");
    json_escape_print(strategy_name(effective));
    printf(",\"smart_compacted\":%s,\"smart_units_moved_kb\":%ld,"
           "\"pre_total_free_kb\":%d,\"pool_kb\":%d,"
           "\"split_from_start_kb\":%d,\"split_from_end_kb\":%d,\"split_from_size_kb\":%d,"
           "\"leftover_kb\":%d,\"blocks_searched\":%d},",
           smart_compacted ? "true" : "false", smart_units_moved,
           pre_stats.total_free, g_mem->total_units,
           split_from_start, split_from_start + split_from_size, split_from_size,
           split_from_size - units_kb, examined);
    print_state_fields();
    fputs("}\n", stdout);
}

static void handle_free(const char *owner) {
    if (!owner || !*owner) {
        fputs("{\"ok\":false,\"error\":\"Missing process owner name.\"}\n", stdout);
        return;
    }
    if (strcmp(owner, SYSTEM_OWNER) == 0) {
        fputs("{\"ok\":false,\"error\":\"The reserved System area cannot be deleted.\"}\n", stdout);
        return;
    }

    const Block *prev = NULL;
    const Block *target = NULL;
    for (const Block *b = g_mem->head; b; b = b->next) {
        if (!b->is_free && strcmp(b->owner, owner) == 0) {
            target = b;
            break;
        }
        prev = b;
    }
    if (!target) {
        fputs("{\"ok\":false,\"error\":\"Allocation not found in device memory.\"}\n", stdout);
        return;
    }

    int freed_start = target->start;
    int freed_size = target->size;
    int freed_end = freed_start + freed_size;
    int merged_left = (prev != NULL && prev->is_free);
    int left_start = merged_left ? prev->start : -1;
    int left_size = merged_left ? prev->size : 0;
    const Block *nxt = target->next;
    int merged_right = (nxt != NULL && nxt->is_free);
    int right_start = merged_right ? nxt->start : -1;
    int right_size = merged_right ? nxt->size : 0;

    Stats before_stats = mem_stats(g_mem);
    BlockSnap *before_blocks = NULL;
    int before_count = capture_blocks(g_mem, &before_blocks);

    if (!mem_free(g_mem, owner)) {
        free(before_blocks);
        fputs("{\"ok\":false,\"error\":\"Failed to release allocation.\"}\n", stdout);
        return;
    }
    remove_active_proc(owner);

    Stats after_stats = mem_stats(g_mem);
    BlockSnap *after_blocks = NULL;
    int after_count = capture_blocks(g_mem, &after_blocks);

    int coalesced_start = freed_start, coalesced_end = freed_end;
    for (const Block *b = g_mem->head; b; b = b->next) {
        if (b->is_free && b->start <= freed_start && (b->start + b->size) >= freed_end) {
            coalesced_start = b->start;
            coalesced_end = b->start + b->size;
            break;
        }
    }

    fputs("{\"ok\":true,\"delete_diff\":{\"owner\":", stdout);
    json_escape_print(owner);
    printf(",\"freed_kb\":%d,\"freed_start_kb\":%d,\"freed_end_kb\":%d,"
           "\"merged_left\":%s,\"left_start_kb\":%d,\"left_size_kb\":%d,"
           "\"merged_right\":%s,\"right_start_kb\":%d,\"right_size_kb\":%d,"
           "\"coalesced_start_kb\":%d,\"coalesced_end_kb\":%d,\"coalesced_size_kb\":%d,"
           "\"before_stats\":",
           freed_size, freed_start, freed_end,
           merged_left ? "true" : "false", left_start, left_size,
           merged_right ? "true" : "false", right_start, right_size,
           coalesced_start, coalesced_end, coalesced_end - coalesced_start);
    print_stats_obj(&before_stats, g_mem->total_units);
    fputs(",\"after_stats\":", stdout);
    print_stats_obj(&after_stats, g_mem->total_units);
    fputs(",\"before_blocks\":", stdout);
    print_snap_array(before_blocks, before_count);
    fputs(",\"after_blocks\":", stdout);
    print_snap_array(after_blocks, after_count);
    fputs("},", stdout);
    print_state_fields();
    fputs("}\n", stdout);

    free(before_blocks);
    free(after_blocks);
}

static void handle_compact(void) {
    Stats before_stats = mem_stats(g_mem);
    BlockSnap *before_blocks = NULL;
    int before_count = capture_blocks(g_mem, &before_blocks);
    long moved_before = g_mem->units_moved;

    mem_compact(g_mem);
    refresh_active_procs();

    long moved_now = g_mem->units_moved - moved_before;
    Stats after_stats = mem_stats(g_mem);
    BlockSnap *after_blocks = NULL;
    int after_count = capture_blocks(g_mem, &after_blocks);

    printf("{\"ok\":true,\"compact_diff\":{\"units_moved_kb\":%ld,\"before_stats\":", moved_now);
    print_stats_obj(&before_stats, g_mem->total_units);
    fputs(",\"after_stats\":", stdout);
    print_stats_obj(&after_stats, g_mem->total_units);
    fputs(",\"before_blocks\":", stdout);
    print_snap_array(before_blocks, before_count);
    fputs(",\"after_blocks\":", stdout);
    print_snap_array(after_blocks, after_count);
    fputs("},", stdout);
    print_state_fields();
    fputs("}\n", stdout);

    free(before_blocks);
    free(after_blocks);
}

static int split_tabs(char *line, char *fields[], int max_fields) {
    int n = 0;
    char *p = line;
    while (n < max_fields) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

int main(void) {
    if (!init_pool(DEFAULT_POOL_KB)) {
        fputs("{\"ok\":false,\"error\":\"Failed to initialize device memory pool.\"}\n", stdout);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        char *fields[8] = {0};
        int nf = split_tabs(line, fields, 8);
        const char *cmd = fields[0];

        if (strcmp(cmd, "STATE") == 0) {
            putchar('{');
            fputs("\"ok\":true,", stdout);
            print_state_fields();
            fputs("}\n", stdout);
        } else if (strcmp(cmd, "INIT") == 0 && nf >= 2) {
            int pool_kb = atoi(fields[1]);
            if (pool_kb != 16384 && pool_kb != 65536 && pool_kb != 262144) {
                fputs("{\"ok\":false,\"error\":\"Capacity must be 16 MB, 64 MB, or 256 MB.\"}\n", stdout);
            } else if (!init_pool(pool_kb)) {
                fputs("{\"ok\":false,\"error\":\"Could not resize device memory pool.\"}\n", stdout);
            } else {
                putchar('{');
                fputs("\"ok\":true,", stdout);
                print_state_fields();
                fputs("}\n", stdout);
            }
        } else if (strcmp(cmd, "PRECHECK") == 0 && nf >= 4) {
            handle_precheck(fields[1], fields[2], fields[3]);
        } else if (strcmp(cmd, "ALLOC") == 0 && nf >= 4) {
            handle_alloc(fields[1], fields[2], fields[3]);
        } else if (strcmp(cmd, "FREE") == 0 && nf >= 2) {
            handle_free(fields[1]);
        } else if (strcmp(cmd, "COMPACT") == 0) {
            handle_compact();
        } else if (strcmp(cmd, "QUIT") == 0) {
            fputs("{\"ok\":true,\"bye\":true}\n", stdout);
            fflush(stdout);
            break;
        } else {
            fputs("{\"ok\":false,\"error\":\"Unknown or malformed command.\"}\n", stdout);
        }
        fflush(stdout);
    }

    if (g_mem) mem_destroy(g_mem);
    return 0;
}
