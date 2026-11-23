#include "common.h"
#include "memory.h"
#include "process.h"
#include "policy.h"
#include "metrics.h"

static void print_processes(const Proc *p,int n) {
    printf("\nPROCESS TABLE\n%-4s %-22s %-10s %-8s %-10s %-10s %-10s\n","ID","Name","Units KB","Duration","State","Address","Wait");
    for(int i=0;i<n;i++) {
        char addr[32]="-";
        if(p[i].state==P_RUNNING || p[i].state==P_COMPLETED)
            snprintf(addr,sizeof(addr),"[%d,%d)",p[i].start_addr,p[i].end_addr);
        printf("%-4d %-22.22s %-10d %-8d %-10s %-10s %-10d\n",p[i].id,p[i].name,p[i].units,
               p[i].duration,state_name(p[i].state),addr,p[i].wait_ticks);
    }
}
static int choose_strategy(void) {
    int x;
    printf("Strategy: 1 First Fit  2 Best Fit  3 Worst Fit  4 Smart: ");
    if(scanf("%d",&x)!=1) { while(getchar()!='\n'); return FIRST_FIT; }
    return x>=1&&x<=4?x-1:FIRST_FIT;
}
static void run_workload(Proc *p,int n,int pool,Strategy s) {
    Memory *m=mem_create(pool,KB); Metrics mt;
    if(!m) { puts("Could not create memory pool."); return; }
    FILE *f=fopen(LOG_FILE,"w"); if(f) fclose(f);
    sim_run(p,n,m,s,&mt);
    mem_print_map(m); print_processes(p,n); metrics_print(&mt);
    printf("Event log written to %s\n",LOG_FILE);
    mem_destroy(m);
}
static void storage_demo(void) {
    Memory *m=mem_create(64,32);
    unsigned char data[512],readback[512];
    printf("Enter a short text (max 512 bytes): ");
    int c=getchar(); if(c!='\n') ungetc(c,stdin);
    if(!fgets((char*)data,sizeof(data),stdin)) { mem_destroy(m); return; }
    int len=(int)strlen((char*)data); if(len<=1) { puts("Empty data rejected."); mem_destroy(m); return; }
    int units=bytes_to_units(len,32);
    Block *b=mem_alloc(m,"stored_text",units,FIRST_FIT);
    if(!b || !mem_write_bytes(m,"stored_text",data,len)) { puts("Storage allocation failed."); mem_destroy(m); return; }
    printf("Stored %d bytes in %d units; capacity %d bytes.\n",len,units,units*32);
    mem_compact(m);
    int actual=0;
    if(mem_read_bytes(m,"stored_text",readback,sizeof(readback),&actual) &&
       actual==len && memcmp(data,readback,(size_t)len)==0) puts("After compaction: bytes are unchanged (verified).");
    else puts("Storage verification failed.");
    mem_print_map(m); mem_free(m,"stored_text"); mem_destroy(m);
}
static void real_files_menu(int pool) {
    Proc p[MAX_PROCS]; int n=0; char path[512]; int duration;
    printf("Enter file paths one by one. Type DONE to finish.\n");
    while(n<MAX_PROCS) {
        printf("File path (or DONE): ");
        int c=getchar(); if(c!='\n') ungetc(c,stdin);
        if(!fgets(path,sizeof(path),stdin)) break;
        path[strcspn(path,"\r\n")]=0;
        if(strcmp(path,"DONE")==0 || !path[0]) break;
        printf("Simulated run duration in ticks (e.g. 5): ");
        if(scanf("%d",&duration)!=1) { puts("Invalid duration."); break; }
        if(duration<1) duration=1;
        add_real_file_process(p,&n,path,duration,pool);
    }
    if(n) run_workload(p,n,pool,(Strategy)choose_strategy());
    else puts("No valid files were added.");
}
int main(void) {
    int choice,pool=2048;
    puts("======================================================");
    puts(" INTELLIGENT MEMORY ALLOCATION & FRAGMENTATION SIMULATOR");
    puts(" C11 | First Fit | Best Fit | Worst Fit | Smart");
    puts("======================================================");
    for(;;) {
        printf("\nCurrent pool: %d KB\n1. Add real files and simulate\n2. Generate file-profile workload\n3. Generate program-profile workload\n4. Compare all strategies + recommendation\n5. Show experiment runner (30 seeds)\n6. Optional data-storage demo\n7. Change memory pool\n0. Exit\nChoice: ",pool);
        if(scanf("%d",&choice)!=1) { puts("Invalid input."); break; }
        if(choice==0) break;
        if(choice==1) real_files_menu(pool);
        else if(choice>=2 && choice<=4) {
            int n,seed,pat;
            printf("Number of processes (1-%d): ",MAX_PROCS); scanf("%d",&n);
            if(n<1||n>MAX_PROCS) { puts("Invalid count."); continue; }
            printf("Seed (integer): "); scanf("%d",&seed);
            printf("Pattern 1 Light, 2 Balanced, 3 Heavy: "); scanf("%d",&pat);
            if(pat<1||pat>3) pat=2;
            Proc p[MAX_PROCS];
            ProfileMode mode=choice==3?PROGRAM_PROFILES:FILE_PROFILES;
            generate_processes(p,n,(unsigned)seed,(Pattern)(pat-1),mode);
            if(choice==4) run_comparison(p,n,pool);
            else run_workload(p,n,pool,(Strategy)choose_strategy());
        } else if(choice==5) run_experiments();
        else if(choice==6) storage_demo();
        else if(choice==7) {
            printf("Enter pool size in KB (1024 to 16384): "); scanf("%d",&pool);
            if(pool<1024||pool>16384) { puts("Out of range; keeping 2048 KB."); pool=2048; }
        } else puts("Unknown choice.");
    }
    puts("Goodbye.");
    return 0;
}
