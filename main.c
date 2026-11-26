#include "common.h"
#include "memory.h"
#include "process.h"
#include "policy.h"
#include "metrics.h"
#include <errno.h>

static int read_int(const char *prompt,int *value) {
    char line[128],*end; long v;
    fputs(prompt,stdout); if(!fgets(line,sizeof(line),stdin)) return 0;
    errno=0; v=strtol(line,&end,10);
    while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n') end++;
    if(errno || end==line || *end || v<INT_MIN || v>INT_MAX) return 0;
    *value=(int)v; return 1;
}
static int read_line(const char *prompt,char *out,size_t cap) {
    fputs(prompt,stdout); if(!fgets(out,(int)cap,stdin)) return 0;
    out[strcspn(out,"\r\n")]=0; return 1;
}
static void print_processes(const Proc *p,int n) {
    printf("\nPROCESS TABLE\n%-4s %-24s %-10s %-8s %-11s %-16s %-8s\n","ID","Name","Units KB","Duration","State","Address","Wait");
    for(int i=0;i<n;i++) {
        char addr[40]="-";
        if(p[i].start_tick>=0) snprintf(addr,sizeof(addr),"[%d,%d)",p[i].start_addr,p[i].end_addr);
        printf("%-4d %-24.24s %-10d %-8d %-11s %-16s %-8d\n",p[i].id,p[i].name,p[i].units,p[i].duration,state_name(p[i].state),addr,p[i].wait_ticks);
    }
}
static Strategy choose_strategy(void) {
    int x; if(!read_int("Strategy: 1 First Fit  2 Best Fit  3 Worst Fit  4 Smart: ",&x)) return FIRST_FIT;
    return x>=1&&x<=4?(Strategy)(x-1):FIRST_FIT;
}
static void run_workload(Proc *p,int n,int pool,Strategy strategy,int live) {
    Memory *m=mem_create(pool,KB); Metrics mt;
    if(!m){puts("Could not create memory pool.");return;}
    log_begin(LOG_FILE);
    if(live) sim_run_display(p,n,m,strategy,&mt); else sim_run(p,n,m,strategy,&mt);
    log_end();
    mem_print_map(m); print_processes(p,n); metrics_print(&mt);
    printf("Event log written to %s\n",LOG_FILE); mem_destroy(m);
}
static void storage_demo(void) {
    Memory *m=mem_create(64,32); unsigned char data[513],readback[513];
    if(!m)return;
    if(!read_line("Enter a short text (max 512 bytes): ",(char*)data,sizeof(data))){mem_destroy(m);return;}
    int len=(int)strlen((char*)data); if(len==0){puts("Empty data rejected.");mem_destroy(m);return;}
    int units=bytes_to_units(len,32); Block *b=mem_alloc(m,"stored_text",units,FIRST_FIT);
    if(!b||!mem_write_bytes(m,"stored_text",data,len)){puts("Storage allocation failed.");mem_destroy(m);return;}
    printf("Stored %d bytes in %d units; capacity %d bytes.\n",len,units,units*32);
    mem_compact(m); int actual=0;
    if(mem_read_bytes(m,"stored_text",readback,sizeof(readback),&actual)&&actual==len&&memcmp(data,readback,(size_t)len)==0) puts("After compaction: bytes are unchanged (verified).");
    else puts("Storage verification failed.");
    mem_print_map(m); mem_free(m,"stored_text"); mem_destroy(m);
}
static void real_files_menu(int pool) {
    Proc *p=calloc(MAX_PROCS,sizeof(*p)); int n=0,duration; char path[512]; if(!p)return;
    puts("Enter file paths one by one. Type DONE to finish.");
    while(n<MAX_PROCS) {
        if(!read_line("File path (or DONE): ",path,sizeof(path)))break;
        if(strcmp(path,"DONE")==0||path[0]=='\0')break;
        if(!read_int("Simulated run duration in ticks (e.g. 5): ",&duration)){puts("Invalid duration; try again.");continue;}
        if(duration<1) duration=1;
        add_real_file_process(p,&n,path,duration,pool);
    }
    if(n){Strategy s=choose_strategy();run_workload(p,n,pool,s,1);}else puts("No valid files were added.");
    free(p);
}
int main(void) {
    int choice,pool=2048;
    puts("======================================================\n INTELLIGENT MEMORY ALLOCATION & FRAGMENTATION SIMULATOR\n C11 | First Fit | Best Fit | Worst Fit | Smart\n======================================================");
    for(;;) {
        printf("\nCurrent pool: %d KB\n1. Add real files and simulate\n2. Generate file-profile workload\n3. Generate program-profile workload\n4. Compare all strategies + recommendation\n5. Run full experiments (30 seeds + timing)\n6. Optional data-storage demo\n7. Change memory pool\n0. Exit\n",pool);
        if(!read_int("Choice: ",&choice)){puts("Invalid input. Enter a menu number.");continue;}
        if(choice==0)break;
        if(choice==1)real_files_menu(pool);
        else if(choice>=2&&choice<=4) {
            int n,seed,pat;
            if(!read_int("Number of processes (1-10000): ",&n)||n<1||n>MAX_PROCS){puts("Invalid process count.");continue;}
            if(!read_int("Seed (integer): ",&seed)){puts("Invalid seed.");continue;}
            if(!read_int("Pattern 1 Light, 2 Balanced, 3 Heavy: ",&pat)||pat<1||pat>3)pat=2;
            Proc *p=calloc((size_t)n,sizeof(*p));if(!p){puts("Not enough memory for workload.");continue;}
            ProfileMode mode=choice==3?PROGRAM_PROFILES:FILE_PROFILES; generate_processes(p,n,(unsigned)seed,(Pattern)(pat-1),mode);
            if(choice==4)run_comparison(p,n,pool);else run_workload(p,n,pool,choose_strategy(),0);free(p);
        } else if(choice==5)run_experiments();
        else if(choice==6)storage_demo();
        else if(choice==7){int requested;if(read_int("Enter pool size in KB (1024 to 16384): ",&requested)&&requested>=1024&&requested<=16384)pool=requested;else puts("Out of range; keeping current pool.");}
        else puts("Unknown choice.");
    }
    puts("Goodbye.");return 0;
}
