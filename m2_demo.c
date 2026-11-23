/* m2_demo.c - shows M2's work on its own: split, free, merge, fragmentation, compaction, lookup.
 *   gcc -Wall -o m2_demo m2_demo.c memory.c policy.c metrics.c
 *   .\m2_demo.exe
 * The file sizes are typed in here; in the real project M1 measures them. */
#include <stdio.h>
#include "memory.h"
#include "policy.h"
#include "metrics.h"

static void show(const char *title, const Memory *m) {
    printf("\n%s\n", title);
    print_map(m);
    Stats s = mem_stats(m);
    printf("  free %d | fragments %d | largest %d | external fragmentation %.1f %%\n",
           s.total_free, s.frags, s.largest, s.ext_frag);
}

int main(void) {
    Memory *m = mem_create(POOL_DEMO, SIZE_ONLY);          /* 2048 units, 1 unit = 1 KB */
    int s, e;

    Block *b;
    b = mem_alloc(m, "report.pdf", 1200, FIRST_FIT);  mem_store(m, b, NULL, 1228800);
    b = mem_alloc(m, "budget.xlsx", 300, FIRST_FIT);  mem_store(m, b, NULL, 307200);
    b = mem_alloc(m, "notes.docx", 200, FIRST_FIT);   mem_store(m, b, NULL, 204800);
    show("1. Three files are placed", m);

    mem_free(m, "budget.xlsx");
    show("2. budget.xlsx completes and its block is freed", m);

    printf("\n3. A 500-unit request arrives\n");
    if (!mem_alloc(m, "new.pdf", 500, FIRST_FIT)) {
        Stats st = mem_stats(m);
        printf("  FAILED: %d units are free in total, but the largest piece is only %d\n",
               st.total_free, st.largest);
        printf("  (counted as a fragmentation failure: frag_fail = %ld, oom_fail = %ld)\n",
               m->frag_fail, m->oom_fail);
    }

    mem_compact(m);
    show("4. Compaction", m);
    printf("  units moved: %ld\n", m->units_moved);

    printf("\n5. The addresses changed, so M1 looks them up again with mem_lookup:\n");
    const char *names[] = { "report.pdf", "notes.docx" };
    for (int i = 0; i < 2; i++)
        if (mem_lookup(m, names[i], &s, &e)) printf("  %-12s [%d, %d)\n", names[i], s, e);

    b = mem_alloc(m, "new.pdf", 500, FIRST_FIT);
    printf("\n6. The 500-unit request again: %s\n", b ? "SUCCESS" : "failed");
    show("   Final map", m);
    printf("\nInvariants hold: %s\n", mem_check_invariants(m) ? "yes" : "NO");

    mem_destroy(m);
    return 0;
}
