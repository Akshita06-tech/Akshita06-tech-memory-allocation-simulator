#include "common.h"
#include "memory.h"
#include "process.h"
#include "policy.h"
#include "metrics.h"
#include <sys/stat.h>

static int passed=0,failed=0;
#define CHECK(c,m) do{if(c){passed++;printf("[PASS] %s\n",m);}else{failed++;printf("[FAIL] %s\n",m);}}while(0)
static int profile_range(const Proc *p,int program) {
    if(program) {
        if(!strcmp(p->type,"calc"))return p->units>=80&&p->units<=150;
        if(!strcmp(p->type,"textproc"))return p->units>=150&&p->units<=300;
        if(!strcmp(p->type,"analyzer"))return p->units>=100&&p->units<=250;
        if(!strcmp(p->type,"media"))return p->units>=200&&p->units<=400;
        if(!strcmp(p->type,"browser"))return p->units>=250&&p->units<=500;
    } else {
        if(!strcmp(p->type,"txt"))return p->units>=1&&p->units<=30;
        if(!strcmp(p->type,"docx"))return p->units>=50&&p->units<=400;
        if(!strcmp(p->type,"xlsx"))return p->units>=100&&p->units<=800;
        if(!strcmp(p->type,"pdf"))return p->units>=200&&p->units<=2000;
        if(!strcmp(p->type,"pptx"))return p->units>=500&&p->units<=3000;
    }
    return 0;
}
static void init_proc(Proc *p,int id,const char *name,int units,int duration,int arrival) {
    memset(p,0,sizeof(*p));p->id=id;snprintf(p->name,sizeof(p->name),"%s",name);p->units=units;p->duration=duration;p->arrive_tick=arrival;p->start_tick=-1;p->finish_tick=-1;p->wait_start_tick=-1;p->state=P_NEW;
}
static int same_map(const Memory *a,const Memory *b) {
    const Block *x=a->head,*y=b->head;
    while(x&&y){if(x->start!=y->start||x->size!=y->size||x->is_free!=y->is_free||strcmp(x->owner,y->owner))return 0;x=x->next;y=y->next;}
    return !x&&!y;
}
int main(void) {
    FILE *f=fopen("test_size.tmp","wb"); if(f){for(int i=0;i<1234567;i++)fputc('x',f);fclose(f);}
    CHECK(measure_file_size("test_size.tmp")==1234567 && bytes_to_units(measure_file_size("test_size.tmp"),1024)==1206,"N1 measured file size rounds 1,234,567 bytes to 1206 KB");
    CHECK(bytes_to_units(0,1024)==-1 && measure_file_size(".")==-1 && measure_file_size("missing_file.test")==-1,"N2 empty/missing files and directories are rejected");
    remove("test_size.tmp");

    Memory *m=mem_create(100,1024); CHECK(m!=NULL,"memory pool created");
    Block *ba=mem_alloc(m,"A",30,FIRST_FIT),*bb=mem_alloc(m,"B",20,FIRST_FIT),*bc=mem_alloc(m,"C",10,FIRST_FIT);
    CHECK(ba!=NULL && bb!=NULL && bc!=NULL,"allocate three blocks");
    CHECK(ba->start+ba->size<=bb->start && bb->start+bb->size<=bc->start && mem_stats(m).used_memory+mem_stats(m).total_free==100,"N9 allocated blocks do not overlap and N16 used plus free equals pool");
    CHECK(mem_free(m,"B") && mem_free(m,"A") && mem_free(m,"C"),"N6 freeing middle/left/right merges both neighbours");
    CHECK(mem_stats(m).total_free==100 && mem_stats(m).free_fragments==1,"free blocks fully coalesce");
    CHECK(mem_check_invariants(m)==0,"N11 consistency checker returns 0 for valid owner and block invariants");
    m->head->owner[0]='X'; CHECK(mem_check_invariants(m)!=0,"N11 checker rejects a free block with an owner"); m->head->owner[0]='\0';
    CHECK(!mem_free(m,"A"),"double free is rejected safely");
    CHECK(mem_alloc(m,"D",10,FIRST_FIT)!=NULL && mem_alloc(m,"D",5,FIRST_FIT)==NULL,"duplicate owner allocation is rejected");
    for(Block *it=m->head;it;it=it->next) if(!it->is_free && !strcmp(it->owner,"D")){char save=it->owner[0];it->owner[0]='\0';CHECK(mem_check_invariants(m)!=0,"N11 checker rejects an allocated block without an owner");it->owner[0]=save;break;}
    mem_destroy(m);

    m=mem_create(100,1024); CHECK(mem_alloc(m,"BIG",101,FIRST_FIT)==NULL && failure_kind(m,101)==2,"N3 request larger than pool is true out-of-memory");
    mem_destroy(m);

    /* Each placement policy must select the intended free extent. */
    for(int strategy=FIRST_FIT;strategy<=WORST_FIT;strategy++) {
        m=mem_create(100,1024);mem_alloc(m,"A",20,FIRST_FIT);mem_alloc(m,"B",30,FIRST_FIT);mem_alloc(m,"C",10,FIRST_FIT);mem_alloc(m,"D",40,FIRST_FIT);
        mem_free(m,"A");mem_free(m,"C");int examined=0;Block *chosen=find_block(m,10,(Strategy)strategy,&examined);
        int expected=strategy==FIRST_FIT?0:strategy==BEST_FIT?50:0;
        CHECK(chosen && chosen->start==expected,"First/Best/Worst Fit selects correct candidate block");mem_destroy(m);
    }

    /* Failed search cannot mutate the linked list. */
    m=mem_create(100,1024);mem_alloc(m,"P1",30,FIRST_FIT);mem_alloc(m,"P2",20,FIRST_FIT);mem_alloc(m,"P3",30,FIRST_FIT);mem_free(m,"P2");
    Memory *snapshot=mem_create(100,1024);mem_alloc(snapshot,"P1",30,FIRST_FIT);mem_alloc(snapshot,"P2",20,FIRST_FIT);mem_alloc(snapshot,"P3",30,FIRST_FIT);mem_free(snapshot,"P2");
    CHECK(mem_alloc(m,"TOO_BIG",50,FIRST_FIT)==NULL && same_map(m,snapshot),"N12 failed allocation leaves memory map unchanged");
    CHECK(failure_kind(m,25)==1 && failure_kind(m,50)==2,"N13 fragmentation and out-of-memory are distinguished");
    Stats before=mem_stats(m);mem_compact(m);Stats after=mem_stats(m);
    CHECK(before.total_free==after.total_free && after.free_fragments==1,"N15 compaction preserves free total and leaves one free block");
    CHECK(mem_check_invariants(m)==0 && after.used_memory+after.total_free==m->total_units,"N16 used plus free equals total after compaction");
    mem_destroy(snapshot);mem_destroy(m);

    Proc a[100],b[100];generate_processes(a,100,42,BALANCED_PATTERN,FILE_PROFILES);generate_processes(b,100,42,BALANCED_PATTERN,FILE_PROFILES);
    CHECK(memcmp(a,b,sizeof(a))==0,"N4 identical seed creates identical file-profile workload");
    generate_processes(b,100,43,BALANCED_PATTERN,FILE_PROFILES);CHECK(memcmp(a,b,sizeof(a))!=0,"different seed changes workload");
    int ranges=1;for(int i=0;i<100;i++)if(!profile_range(&a[i],0))ranges=0;CHECK(ranges,"N5 every file profile size is within its declared range");
    generate_processes(a,100,42,BALANCED_PATTERN,PROGRAM_PROFILES);generate_processes(b,100,42,BALANCED_PATTERN,PROGRAM_PROFILES);
    CHECK(memcmp(a,b,sizeof(a))==0,"N20 program-profile workload repeats with same seed");
    ranges=1;for(int i=0;i<100;i++)if(!profile_range(&a[i],1))ranges=0;CHECK(ranges,"N20 every program-profile size is within its declared range");
    CHECK(bytes_to_units(66,32)==3,"storage mode rounds 66 bytes to 3 units");

    /* Smart compaction moves a stored block while preserving its bytes. */
    m=mem_create(64,32);unsigned char input[]="Data survives compaction",output[64];int actual=0;
    mem_alloc(m,"GAP",1,FIRST_FIT);mem_alloc(m,"TEXT",1,FIRST_FIT);mem_write_bytes(m,"TEXT",input,(int)sizeof(input));mem_free(m,"GAP");mem_compact(m);
    CHECK(mem_read_bytes(m,"TEXT",output,sizeof(output),&actual)&&actual==(int)sizeof(input)&&memcmp(input,output,sizeof(input))==0,"N17 stored bytes remain identical after compaction");
    CHECK(mem_check_invariants(m)==0 && mem_stats(m).used_memory+mem_stats(m).total_free==64,"N16 memory accounting holds in storage mode");
    mem_alloc(m,"FILL",63,FIRST_FIT);CHECK(mem_stats(m).total_free==0&&mem_stats(m).external_fragmentation==0.0,"N18 zero free memory reports 0% fragmentation without division by zero");mem_destroy(m);

    /* Waiting for exactly four ticks must be reported as four, not three. */
    Proc w[2];init_proc(&w[0],1,"HOLDER",50,4,0);init_proc(&w[1],2,"WAITER",60,2,0);
    m=mem_create(100,1024);Metrics mt;sim_run(w,2,m,FIRST_FIT,&mt);
    CHECK(w[1].state==P_COMPLETED && w[1].wait_ticks==4,"N7 waiter retries after release and waiting duration counts all four ticks");
    CHECK(w[0].end_addr-w[0].start_addr==50 && w[1].end_addr-w[1].start_addr==60,"N9 completed processes retain their original half-open address ranges");
    CHECK(mt.waiting_processes==1 && mt.avg_wait==4.0,"waiting metrics use elapsed ticks, not failed retry count");
    CHECK(mt.oom_failures==1 && mt.fragmentation_failures==0,"N13 first failed attempt is classified and counted only once across repeated retries");
    CHECK(!(w[0].start_tick<w[1].finish_tick && w[1].start_tick<w[0].finish_tick && w[0].start_addr<w[1].end_addr && w[1].start_addr<w[0].end_addr),"N9 no two simultaneously running processes overlap in address and time");mem_destroy(m);

    Proc d[2];init_proc(&d[0],1,"TOO_LARGE",101,1,0);init_proc(&d[1],2,"FITS",50,1,1);
    m=mem_create(100,1024);sim_run(d,2,m,FIRST_FIT,&mt);
    CHECK(d[0].state==P_REJECTED && d[1].state==P_COMPLETED,"N8 no deadlock: waiting process runs when all memory is free");
    CHECK(mt.oom_failures==1,"N3 oversized request is counted as out-of-memory");mem_destroy(m);

    /* Compaction address refresh matches the block-list source of truth. */
    m=mem_create(100,1024);mem_alloc(m,"LEFT",20,FIRST_FIT);mem_alloc(m,"MOVE",30,FIRST_FIT);mem_free(m,"LEFT");
    Proc one;init_proc(&one,1,"MOVE",30,5,0);one.state=P_RUNNING;one.start_addr=20;one.end_addr=50;mem_compact(m);refresh_process_addresses(&one,1,m);
    int st,en;CHECK(mem_lookup(m,"MOVE",&st,&en)&&one.start_addr==st&&one.end_addr==en,"N10 running process addresses refresh after compaction");mem_destroy(m);

    /* Combined-guide worked example: 200 KB compacted; 500 KB request placed at [1400,1900). */
    m=mem_create(2048,KB);
    mem_alloc(m,"report.pdf",1200,FIRST_FIT);mem_alloc(m,"budget.xlsx",300,FIRST_FIT);mem_alloc(m,"notes.docx",200,FIRST_FIT);mem_free(m,"budget.xlsx");
    int compact_first=0;Strategy smart_choice=engine_choose(m,500,&compact_first);
    Stats pre_smart=mem_stats(m);mem_compact(m);int note_start=-1,note_end=-1;mem_lookup(m,"notes.docx",&note_start,&note_end);
    Block *new_pdf=mem_alloc(m,"new.pdf",500,FIRST_FIT);
    CHECK(smart_choice==FIRST_FIT&&compact_first==1&&pre_smart.total_free==648&&pre_smart.largest_free==348&&m->units_moved==200&&note_start==1200&&note_end==1400&&new_pdf&&new_pdf->start==1400&&new_pdf->start+new_pdf->size==1900,
          "Smart compaction reproduces guide example: 200 KB moved and new.pdf placed at [1400,1900)");
    CHECK(mem_check_invariants(m)==0,"worked-example compaction preserves all memory invariants");mem_destroy(m);

    Metrics identical[4];memset(identical,0,sizeof(identical));for(int i=0;i<4;i++){identical[i].success_rate=80;identical[i].avg_wait=2;identical[i].makespan=20;identical[i].avg_fragmentation=10;identical[i].avg_search_length=3;}
    char why[256];recommend(identical,4,why,sizeof(why));CHECK(strstr(why,"Tie")!=NULL,"N14 recommendation explicitly reports tied rank sums");

    /* N19: event stream is ordered, and logger opens once and truncates at run start. */
    Proc ev[2];init_proc(&ev[0],1,"EV1",30,1,0);init_proc(&ev[1],2,"EV2",80,1,0);
    m=mem_create(100,1024);log_begin("test_events.log");sim_run(ev,2,m,FIRST_FIT,&mt);log_end();
    f=fopen("test_events.log","r");int monotonic=1,lines=0,last=-1;char line[512];
    if(f){while(fgets(line,sizeof(line),f)){int tick;if(sscanf(line,"tick=%d",&tick)==1){if(tick<last)monotonic=0;last=tick;lines++;}}fclose(f);}else monotonic=0;
    CHECK(monotonic&&lines>=6,"N19 event log contains ordered arrival/allocation/failure/completion events");
    f=fopen("test_events.log","a");if(f){fputs("stale-line\n",f);fclose(f);} log_begin("test_events.log");log_end();
    f=fopen("test_events.log","r");int cleared=1;if(f){if(fgets(line,sizeof(line),f)&&strstr(line,"stale-line"))cleared=0;fclose(f);}else cleared=0;
    CHECK(cleared,"event log is truncated once at the beginning of a new run");remove("test_events.log");remove("simulation_trace.csv");mem_destroy(m);

    /* Light and heavy weights are deliberately biased to their named profile groups. */
    Proc light[500],heavy[500];generate_processes(light,500,777,LIGHT_PATTERN,FILE_PROFILES);generate_processes(heavy,500,777,HEAVY_PATTERN,FILE_PROFILES);
    int light_small=0,heavy_large=0;for(int i=0;i<500;i++){if(!strcmp(light[i].type,"txt")||!strcmp(light[i].type,"docx"))light_small++;if(!strcmp(heavy[i].type,"pdf")||!strcmp(heavy[i].type,"pptx"))heavy_large++;}
    printf("pattern sample: light small=%d, heavy large=%d\n",light_small,heavy_large); CHECK(light_small>150 && heavy_large>150,"Light/heavy workload patterns bias toward txt/docx and pdf/pptx respectively");

    printf("\nTests passed: %d | failed: %d\n",passed,failed);return failed?1:0;
}
