/* test_memory.c - M2's own tests for memory.c
 *
 * Build and run (from the project folder):
 *   gcc -Wall -o test_memory test_memory.c memory.c policy.c metrics.c
 *   .\test_memory.exe
 *
 * Covers the tests the guide gives to M2: N9 (addresses), N11 (owner check),
 * N12 (failed allocation changes nothing), plus the size-only mode,
 * mem_lookup, and the safety rules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "policy.h"
#include "metrics.h"

static int run = 0, pass = 0;
#define CHECK(label, cond) do { run++; if (cond) { pass++; printf("PASS  %s\n", label); } \
                                else printf("FAIL  %s (line %d)\n", label, __LINE__); } while (0)

/* Layout (100 units): A 0-10, B 10-30, C 30-40, D 40-70, E 70-85, tail 85-100.
 * After freeing B and D the free blocks are 20@10, 30@40, 15@85. */
static Memory *layout(void) {
    Memory *m = mem_create(100, 1);
    mem_alloc(m, "A", 10, FIRST_FIT); mem_alloc(m, "B", 20, FIRST_FIT);
    mem_alloc(m, "C", 10, FIRST_FIT); mem_alloc(m, "D", 30, FIRST_FIT);
    mem_alloc(m, "E", 15, FIRST_FIT);
    mem_free(m, "B"); mem_free(m, "D");
    return m;
}

/* a copy of the block table, to prove that a failed request changes nothing */
typedef struct { int start, size, is_free; char owner[48]; } Snap;
static int snapshot(const Memory *m, Snap *out, int max) {
    int n = 0;
    for (const Block *b = m->head; b && n < max; b = b->next, n++) {
        out[n].start = b->start; out[n].size = b->size; out[n].is_free = b->is_free;
        snprintf(out[n].owner, sizeof out[n].owner, "%s", b->owner);
    }
    return n;
}
static int same(const Snap *a, int na, const Snap *b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++)
        if (a[i].start != b[i].start || a[i].size != b[i].size ||
            a[i].is_free != b[i].is_free || strcmp(a[i].owner, b[i].owner) != 0) return 0;
    return 1;
}

/* N9: every used block has end - start == size, lies inside the pool, and no two used blocks overlap */
static int check_addresses(const Memory *m) {
    for (const Block *b = m->head; b; b = b->next) {
        if (b->is_free) continue;
        int s, e;
        if (!mem_lookup(m, b->owner, &s, &e)) return 0;
        if (s != b->start || e - s != b->size) return 0;
        if (s < 0 || e > m->total_units) return 0;
        for (const Block *c = b->next; c; c = c->next) {
            if (c->is_free) continue;
            if (b->start < c->start + c->size && c->start < b->start + b->size) return 0;
        }
    }
    return 1;
}

static unsigned rng_state;
static unsigned rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return (rng_state >> 16) & 0x7fff; }

int main(void) {
    Memory *m;

    /* ---------------- creating a pool ---------------- */
    m = mem_create(100, 1);
    CHECK("new pool is one free block of the full size", m && m->head && m->head->is_free && m->head->size == 100 && !m->head->next);
    CHECK("a normal pool has a data array", m->mem != NULL);
    mem_destroy(m);
    CHECK("pool of 0 units is refused", mem_create(0, 1) == NULL);
    CHECK("pool of negative size is refused", mem_create(-5, 1) == NULL);
    CHECK("negative unit size is refused", mem_create(10, -1) == NULL);
    m = mem_create(POOL_LARGE, SIZE_ONLY);
    CHECK("size-only pool of 16384 units has no data array", m && m->mem == NULL && m->head->size == POOL_LARGE);
    CHECK("size-only pool passes the invariants", mem_check_invariants(m));
    mem_destroy(m);
    mem_destroy(NULL);
    CHECK("mem_destroy(NULL) is safe", 1);

    /* ---------------- the worked example from the guide (2048 units, size-only) ---------------- */
    m = mem_create(POOL_DEMO, SIZE_ONLY);
    int s, e;
    mem_alloc(m, "report.pdf", 1200, FIRST_FIT);
    mem_alloc(m, "budget.xlsx", 300, FIRST_FIT);
    mem_alloc(m, "notes.docx", 200, FIRST_FIT);
    CHECK("report.pdf is at [0, 1200)", mem_lookup(m, "report.pdf", &s, &e) && s == 0 && e == 1200);
    CHECK("budget.xlsx is at [1200, 1500)", mem_lookup(m, "budget.xlsx", &s, &e) && s == 1200 && e == 1500);
    CHECK("notes.docx is at [1500, 1700)", mem_lookup(m, "notes.docx", &s, &e) && s == 1500 && e == 1700);
    mem_free(m, "budget.xlsx");
    Stats st = mem_stats(m);
    CHECK("after the xlsx completes: free 648, largest 348, 2 pieces", st.total_free == 648 && st.largest == 348 && st.frags == 2);
    CHECK("a 500-unit request fails (fragmentation failure)", mem_alloc(m, "new.pdf", 500, FIRST_FIT) == NULL && m->frag_fail == 1 && m->oom_fail == 0);
    mem_compact(m);
    st = mem_stats(m);
    CHECK("after compaction: one free block of 648", st.frags == 1 && st.largest == 648 && st.ext_frag == 0.0);
    CHECK("notes.docx moved to [1200, 1400)", mem_lookup(m, "notes.docx", &s, &e) && s == 1200 && e == 1400);
    CHECK("report.pdf did not move", mem_lookup(m, "report.pdf", &s, &e) && s == 0 && e == 1200);
    CHECK("compaction moved 200 units", m->units_moved == 200);
    CHECK("the 500-unit request now fits at [1400, 1900)", mem_alloc(m, "new.pdf", 500, FIRST_FIT) != NULL && mem_lookup(m, "new.pdf", &s, &e) && s == 1400 && e == 1900);
    CHECK("invariants and addresses hold", mem_check_invariants(m) && check_addresses(m));
    mem_destroy(m);

    /* ---------------- mem_lookup ---------------- */
    m = layout();
    CHECK("lookup of an unknown name returns 0", mem_lookup(m, "ZZ", &s, &e) == 0);
    CHECK("lookup accepts NULL output pointers", mem_lookup(m, "A", NULL, NULL) == 1);
    CHECK("lookup gives [start, end) with end = start + size", mem_lookup(m, "E", &s, &e) && s == 70 && e == 85);
    mem_free(m, "E");
    CHECK("lookup of a freed name returns 0", mem_lookup(m, "E", &s, &e) == 0);
    mem_destroy(m);

    m = layout();
    mem_compact(m);
    CHECK("after compaction lookup finds the new addresses (A, C, E)",
          mem_lookup(m, "A", &s, &e) && s == 0 && e == 10 &&
          mem_lookup(m, "C", &s, &e) && s == 10 && e == 20 &&
          mem_lookup(m, "E", &s, &e) && s == 20 && e == 35);
    mem_destroy(m);

    /* ---------------- N9: address safety ---------------- */
    m = layout();
    CHECK("N9: addresses are consistent in the layout", check_addresses(m));
    mem_destroy(m);

    /* ---------------- N11: the owner rule in the checker ---------------- */
    m = layout();
    CHECK("N11: a normal pool passes", mem_check_invariants(m));
    Block *f = m->head; while (f && !f->is_free) f = f->next;
    snprintf(f->owner, sizeof f->owner, "ghost");
    CHECK("N11: a free block with an owner is caught", !mem_check_invariants(m));
    f->owner[0] = '\0';
    CHECK("N11: ... and fixed again", mem_check_invariants(m));
    Block *u = m->head; while (u && u->is_free) u = u->next;
    char saved[48]; snprintf(saved, sizeof saved, "%s", u->owner);
    u->owner[0] = '\0';
    CHECK("N11: a used block without an owner is caught", !mem_check_invariants(m));
    snprintf(u->owner, sizeof u->owner, "%s", saved);
    CHECK("N11: ... and fixed again (2)", mem_check_invariants(m));
    mem_destroy(m);

    /* ---------------- owner safety rules ---------------- */
    m = mem_create(100, 1);
    CHECK("empty owner name is rejected", mem_alloc(m, "", 10, FIRST_FIT) == NULL);
    CHECK("NULL owner name is rejected", mem_alloc(m, NULL, 10, FIRST_FIT) == NULL);
    CHECK("rejected requests are not counted", m->ok == 0 && m->frag_fail == 0 && m->oom_fail == 0);
    mem_alloc(m, "A", 10, FIRST_FIT);
    Snap a1[16], a2[16];
    int n1 = snapshot(m, a1, 16);
    CHECK("a second request with the same owner is rejected", mem_alloc(m, "A", 10, FIRST_FIT) == NULL);
    int n2 = snapshot(m, a2, 16);
    CHECK("... and the map is unchanged, one success counted", same(a1, n1, a2, n2) && m->ok == 1);
    mem_destroy(m);

    /* ---------------- N12: a failed request changes nothing ---------------- */
    Strategy all[4] = { FIRST_FIT, BEST_FIT, WORST_FIT, SMART };
    int ok_all = 1, count_ok = 1;
    for (int k = 0; k < 4; k++) {
        m = layout();
        n1 = snapshot(m, a1, 16);
        if (mem_alloc(m, "X", 70, all[k]) != NULL) ok_all = 0;      /* more than the 65 free units */
        n2 = snapshot(m, a2, 16);
        if (!same(a1, n1, a2, n2)) ok_all = 0;
        if (m->oom_fail != 1 || m->frag_fail != 0) count_ok = 0;
        mem_destroy(m);
    }
    CHECK("N12: too-big request fails and leaves the map unchanged (all 4 strategies)", ok_all);
    CHECK("N12: ... and is counted as out-of-memory", count_ok);
    ok_all = 1; count_ok = 1;
    for (int k = 0; k < 3; k++) {                                     /* fixed strategies only */
        m = layout();
        n1 = snapshot(m, a1, 16);
        if (mem_alloc(m, "X", 40, all[k]) != NULL) ok_all = 0;      /* 65 free, but largest piece is 30 */
        n2 = snapshot(m, a2, 16);
        if (!same(a1, n1, a2, n2)) ok_all = 0;
        if (m->frag_fail != 1 || m->oom_fail != 0) count_ok = 0;
        mem_destroy(m);
    }
    CHECK("N12: scattered request fails and leaves the map unchanged (First, Best, Worst)", ok_all);
    CHECK("N12: ... and is counted as a fragmentation failure", count_ok);
    m = layout();
    Block *sm = mem_alloc(m, "X", 40, SMART);
    CHECK("Smart compacts and the 40-unit request succeeds", sm != NULL && m->compactions == 1 && m->frag_fail == 0);
    CHECK("... and mem_lookup shows the new address [35, 75)", mem_lookup(m, "X", &s, &e) && s == 35 && e == 75);
    mem_destroy(m);

    /* ---------------- size-only mode ---------------- */
    m = mem_create(100, SIZE_ONLY);
    Block *b = mem_alloc(m, "file", 10, FIRST_FIT);
    CHECK("size-only: store remembers the byte length", mem_store(m, b, NULL, 1234) == 1 && b->data_len == 1234);
    CHECK("size-only: store of a negative length is refused", mem_store(m, b, NULL, -1) == 0);
    CHECK("size-only: store into a NULL block is refused", mem_store(m, NULL, NULL, 5) == 0);
    mem_alloc(m, "g", 10, FIRST_FIT);
    mem_free(m, "file");
    mem_compact(m);                                                    /* must not crash */
    CHECK("size-only: free and compaction work without a data array", mem_check_invariants(m) && mem_lookup(m, "g", &s, &e) && s == 0);
    mem_destroy(m);

    /* ---------------- real data (not size-only) ---------------- */
    m = mem_create(10, 4);
    b = mem_alloc(m, "a", 1, FIRST_FIT);
    CHECK("store refuses data bigger than the block", mem_store(m, b, (const unsigned char *)"12345", 5) == 0 && b->data_len == 0);
    CHECK("store accepts data that fits exactly", mem_store(m, b, (const unsigned char *)"1234", 4) == 1);
    CHECK("... and the bytes are in the array", memcmp(m->mem, "1234", 4) == 0);
    mem_destroy(m);

    m = mem_create(10, 4);
    mem_store(m, mem_alloc(m, "a", 1, FIRST_FIT), (const unsigned char *)"aaaa", 4);
    mem_store(m, mem_alloc(m, "b", 1, FIRST_FIT), (const unsigned char *)"bbbb", 4);
    mem_store(m, mem_alloc(m, "c", 1, FIRST_FIT), (const unsigned char *)"cccc", 4);
    mem_free(m, "a");
    mem_compact(m);
    CHECK("compaction moves the data with its block", memcmp(m->mem, "bbbbcccc", 8) == 0 && m->units_moved == 2);
    CHECK("... and the lookup matches", mem_lookup(m, "b", &s, &e) && s == 0 && mem_lookup(m, "c", &s, &e) && s == 1);
    mem_free(m, "b");
    CHECK("free clears the freed bytes", memcmp(m->mem, "\0\0\0\0", 4) == 0);
    mem_destroy(m);

    /* ---------------- merging ---------------- */
    m = mem_create(100, SIZE_ONLY);
    mem_alloc(m, "A", 20, FIRST_FIT); mem_alloc(m, "B", 20, FIRST_FIT); mem_alloc(m, "C", 20, FIRST_FIT);
    mem_free(m, "A"); mem_free(m, "C");
    mem_free(m, "B");
    CHECK("freeing the middle block merges with both neighbours", m->head->is_free && m->head->size == 100 && !m->head->next);
    CHECK("double free is rejected", mem_free(m, "B") == 0 && mem_check_invariants(m));
    mem_destroy(m);

    /* ---------------- random stress, all four strategies ---------------- */
    int stress_ok = 1;
    for (int k = 0; k < 4; k++)
        for (unsigned seed = 1; seed <= 5; seed++) {
            rng_state = seed * 7919u;
            m = mem_create(1024, SIZE_ONLY);
            int live[2000], nlive = 0, next_id = 1;
            char nm[24];
            for (int i = 0; i < 2000; i++) {
                if (nlive > 0 && rnd() % 100 < 45) {
                    int j = (int)(rnd() % (unsigned)nlive);
                    snprintf(nm, sizeof nm, "P%d", live[j]);
                    if (!mem_free(m, nm)) stress_ok = 0;
                    live[j] = live[--nlive];
                } else {
                    int sz = 1 + (int)(rnd() % 60);
                    snprintf(nm, sizeof nm, "P%d", next_id);
                    if (mem_alloc(m, nm, sz, all[k])) live[nlive++] = next_id;
                    next_id++;
                }
                if (!mem_check_invariants(m) || !check_addresses(m)) stress_ok = 0;
                Stats z = mem_stats(m);
                if (z.used + z.total_free != 1024) stress_ok = 0;
            }
            for (int j = 0; j < nlive; j++) { snprintf(nm, sizeof nm, "P%d", live[j]); mem_free(m, nm); }
            if (!(m->head->is_free && m->head->size == 1024 && !m->head->next)) stress_ok = 0;
            mem_destroy(m);
        }
    CHECK("stress: 20 runs x 2000 steps, 4 strategies: invariants and addresses always hold, everything frees back to one block", stress_ok);

    printf("\n%d of %d tests passed\n", pass, run);
    return pass == run ? 0 : 1;
}
