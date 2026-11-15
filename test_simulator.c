/* test_simulator.c - build: gcc -Wall -o tests test_simulator.c && ./tests
 * Keep this file in the same folder as memory_simulator.c */
#define MEMSIM_TEST
#include "memory_simulator.c"

static int run = 0, pass = 0;
#define CHECK(label, cond) do { run++; if (cond) { pass++; printf("PASS  %s\n", label); } \
                                else printf("FAIL  %s (line %d)\n", label, __LINE__); } while (0)

/* Layout (100 units): A0-10 B10-30 C30-40 D40-70 E70-85 tail85-100.
 * After freeing B and D the free blocks are 20@10, 30@40, 15@85.      */
static Memory *layout(void) {
    Memory *m = mem_create(100, 1);
    mem_alloc(m, "A", 10, FIRST_FIT); mem_alloc(m, "B", 20, FIRST_FIT);
    mem_alloc(m, "C", 10, FIRST_FIT); mem_alloc(m, "D", 30, FIRST_FIT);
    mem_alloc(m, "E", 15, FIRST_FIT);
    mem_free(m, "B"); mem_free(m, "D");
    return m;
}

int main(void) {
    Memory *m;

    m = mem_create(100, 1);
    CHECK("new memory is one free block of full size",
          m->head && m->head->is_free && m->head->size == 100 && !m->head->next);
    mem_destroy(m);

    m = layout();
    CHECK("First Fit picks the first block that fits (address 10)",
          mem_alloc(m, "X", 12, FIRST_FIT)->start == 10);
    mem_destroy(m);
    m = layout();
    CHECK("Best Fit picks the smallest block that fits (address 85)",
          mem_alloc(m, "X", 12, BEST_FIT)->start == 85);
    mem_destroy(m);
    m = layout();
    CHECK("Worst Fit picks the largest block (address 40)",
          mem_alloc(m, "X", 12, WORST_FIT)->start == 40);
    mem_destroy(m);

    m = mem_create(100, 1);
    Block *b = mem_alloc(m, "A", 30, FIRST_FIT);
    CHECK("split: used part has requested size", b && b->size == 30 && !b->is_free);
    CHECK("split: leftover is a free block of the remainder",
          b->next && b->next->is_free && b->next->start == 30 && b->next->size == 70);
    CHECK("split: invariants hold", mem_check_invariants(m));
    mem_destroy(m);

    m = mem_create(100, 1);
    mem_alloc(m, "A", 100, FIRST_FIT);
    CHECK("exact fit leaves no leftover block", m->head->next == NULL && m->ok == 1);
    mem_destroy(m);

    m = mem_create(100, 1);
    mem_alloc(m, "A", 20, FIRST_FIT); mem_alloc(m, "B", 20, FIRST_FIT);
    mem_alloc(m, "C", 20, FIRST_FIT); mem_alloc(m, "D", 20, FIRST_FIT);
    mem_free(m, "A"); mem_free(m, "C");
    mem_free(m, "B");                         /* neighbours on both sides are free */
    CHECK("free merges with previous and next free block",
          m->head->is_free && m->head->size == 60 && mem_check_invariants(m));
    mem_free(m, "D");
    CHECK("freeing everything gives one block of full size",
          m->head->size == 100 && m->head->next == NULL);
    mem_destroy(m);

    m = mem_create(100, 1);
    mem_alloc(m, "A", 10, FIRST_FIT);
    CHECK("free of unknown name is rejected", mem_free(m, "ZZ") == 0);
    mem_free(m, "A");
    CHECK("double free is rejected safely", mem_free(m, "A") == 0 && mem_check_invariants(m));
    mem_destroy(m);

    m = mem_create(50, 1);
    CHECK("request bigger than whole memory fails", mem_alloc(m, "X", 51, BEST_FIT) == NULL);
    CHECK("... and counts as out-of-memory", m->oom_fail == 1 && m->frag_fail == 0);
    CHECK("zero-size request is rejected without counting",
          mem_alloc(m, "Z", 0, FIRST_FIT) == NULL && m->oom_fail == 1 && m->frag_fail == 0);
    CHECK("negative-size request is rejected", mem_alloc(m, "Z", -5, FIRST_FIT) == NULL);
    mem_destroy(m);

    m = layout();                               /* free: 20, 30, 15 = 65 */
    CHECK("scattered memory: 40 units fails", mem_alloc(m, "X", 40, FIRST_FIT) == NULL);
    CHECK("... and counts as fragmentation failure", m->frag_fail == 1 && m->oom_fail == 0);
    mem_destroy(m);

    m = layout();
    Stats before = mem_stats(m);
    mem_compact(m);
    Stats after = mem_stats(m);
    CHECK("compaction leaves exactly one free block", after.frags == 1);
    CHECK("compaction keeps total free memory", after.total_free == before.total_free);
    CHECK("compaction makes largest block = total free", after.largest == after.total_free);
    CHECK("compaction gives 0% external fragmentation", after.ext_frag == 0.0);
    CHECK("compaction keeps invariants", mem_check_invariants(m));
    CHECK("after compaction the 40-unit request succeeds", mem_alloc(m, "X", 40, FIRST_FIT) != NULL);
    mem_destroy(m);

    m = mem_create(64, 32);
    store_new(m, FIRST_FIT, "a", (const unsigned char *)"hello", 5);
    store_new(m, FIRST_FIT, "b", (const unsigned char *)"WORLD", 5);
    mem_free(m, "a");
    mem_compact(m);
    Block *bb = find_owner(m, "b");
    CHECK("data survives compaction (moved with its block)",
          bb && bb->start == 0 && memcmp(m->mem, "WORLD", 5) == 0);
    mem_destroy(m);

    m = mem_create(64, 1024);
    Block *r = mem_alloc(m, "f", (2500 + 1023) / 1024, FIRST_FIT);
    CHECK("2500 bytes with 1024-byte units needs 3 units", r && r->size == 3);
    mem_destroy(m);
    m = mem_create(10, 32);
    store_new(m, FIRST_FIT, "one", (const unsigned char *)"x", 1);
    store_new(m, FIRST_FIT, "full", (const unsigned char *)"0123456789012345678901234567890", 32);
    CHECK("1 byte takes 1 unit; exactly 32 bytes also takes 1 unit",
          find_owner(m, "one")->size == 1 && find_owner(m, "full")->size == 1);
    CHECK("duplicate name is rejected", store_new(m, FIRST_FIT, "one", (const unsigned char *)"y", 1) == 0);
    CHECK("empty data is rejected", store_new(m, FIRST_FIT, "e", (const unsigned char *)"", 0) == 0);
    mem_destroy(m);

    m = mem_create(64, 32);
    store_new(m, FIRST_FIT, "note", (const unsigned char *)"stored text", 11);
    CHECK("stored data can be read back from the memory array",
          memcmp(m->mem + find_owner(m, "note")->start * 32, "stored text", 11) == 0);
    mem_destroy(m);

    m = layout();                               /* free: 20, 30, 15 */
    Block *sm = mem_alloc(m, "X", 40, SMART);
    CHECK("Smart: compacts and succeeds where First Fit failed",
          sm != NULL && m->compactions == 1 && m->frag_fail == 0);
    mem_destroy(m);
    m = mem_create(100, 1);
    mem_alloc(m, "X", 10, SMART);
    CHECK("Smart: empty memory -> First Fit, no compaction",
          m->last_used == FIRST_FIT && m->compactions == 0);
    mem_destroy(m);

    Op a[100], c[100];
    make_workload(a, 100, PAT_MIXED, 7); make_workload(c, 100, PAT_MIXED, 7);
    CHECK("same seed gives the same workload", memcmp(a, c, sizeof a) == 0);
    make_workload(c, 100, PAT_MIXED, 8);
    CHECK("different seed gives a different workload", memcmp(a, c, sizeof a) != 0);

    int ok = 1;
    Strategy all[4] = { FIRST_FIT, BEST_FIT, WORST_FIT, SMART };
    Pattern pats[3] = { PAT_SMALL, PAT_LARGE, PAT_MIXED };
    for (int p = 0; p < 3; p++)
        for (unsigned seed = 1; seed <= 10; seed++)
            for (int k = 0; k < 4; k++) {
                Op ops[300]; make_workload(ops, 300, pats[p], seed);
                Memory *mm = mem_create(100, 1);
                char nm[24]; char live[301] = {0};
                for (int i = 0; i < 300; i++) {
                    snprintf(nm, sizeof nm, "P%d", ops[i].id);
                    if (ops[i].is_alloc) { if (mem_alloc(mm, nm, ops[i].size, all[k])) live[ops[i].id] = 1; }
                    else if (live[ops[i].id]) { mem_free(mm, nm); live[ops[i].id] = 0; }
                    if (!mem_check_invariants(mm)) ok = 0;
                }
                Stats s = mem_stats(mm);
                if (s.used + s.total_free != 100) ok = 0;
                mem_destroy(mm);
            }
    CHECK("120 random runs: blocks always contiguous, merged, sizes add up to 100", ok);

    printf("\n%d of %d tests passed\n", pass, run);
    return pass == run ? 0 : 1;
}
