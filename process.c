#include "process.h"
#include "metrics.h"
#include <sys/stat.h>

static const Profile file_profiles[] = {
    {"txt","Text file",1,30,2,5,3}, {"docx","Word document",50,400,4,10,3},
    {"xlsx","Excel sheet",100,800,5,12,2}, {"pdf","PDF",200,2000,6,15,2},
    {"pptx","Presentation",500,3000,8,20,1}
};
static const Profile program_profiles[] = {
    {"calc","Calculator",80,150,2,5,3}, {"textproc","Text Processor",150,300,4,10,3},
    {"analyzer","Number Analyzer",100,250,3,8,2}, {"media","Media Player",200,400,6,14,2},
    {"browser","Browser",250,500,8,20,1}
};

const char *state_name(PState s) {
    static const char *v[]={"NEW","READY","RUNNING","WAITING","COMPLETED","REJECTED"};
    return (s>=P_NEW && s<=P_REJECTED)?v[s]:"UNKNOWN";
}
const char *strategy_name(Strategy s) {
    static const char *v[]={"First Fit","Best Fit","Worst Fit","Smart"};
    return (s>=FIRST_FIT && s<=SMART_FIT)?v[s]:"Unknown";
}

long measure_file_size(const char *path) {
    struct stat st;
    if(!path || !*path || stat(path,&st)!=0) return -1;
#if defined(_WIN32)
    if((st.st_mode & _S_IFDIR)!=0) return -1;
#else
    if(S_ISDIR(st.st_mode) || !S_ISREG(st.st_mode)) return -1;
#endif
    if(st.st_size<0 || (uintmax_t)st.st_size>(uintmax_t)LONG_MAX) return -1;
    return (long)st.st_size;
}

int bytes_to_units(long bytes, int unit) {
    if(bytes<=0 || unit<=0) return -1;
    long units=1+(bytes-1)/unit; /* avoids overflow in bytes + unit - 1 */
    return units>INT_MAX ? INT_MAX : (int)units;
}

static unsigned rng_next(unsigned *s) {
    *s = (*s * 1664525u) + 1013904223u;
    return *s;
}
int generate_processes(Proc *out, int n, unsigned seed, int pattern, ProfileMode mode) {
    if (!out || n<0 || n>MAX_PROCS) return 0;
    unsigned state=seed?seed:1;
    const Profile *profiles=mode==PROGRAM_PROFILES?program_profiles:file_profiles;
    int total_weight=0;
    for (int i=0;i<5;i++) {
        int w=profiles[i].weight;
        if (pattern==LIGHT_PATTERN) w=(i<2?5:1);
        if (pattern==HEAVY_PATTERN) w=(i>=3?5:1);
        total_weight+=w;
    }
    for (int i=0;i<n;i++) {
        int pick=(int)(rng_next(&state)%(unsigned)total_weight), idx=0;
        for (;idx<4;idx++) {
            int w=profiles[idx].weight;
            if (pattern==LIGHT_PATTERN) w=(idx<2?5:1);
            if (pattern==HEAVY_PATTERN) w=(idx>=3?5:1);
            if (pick<w) break;
            pick-=w;
        }
        const Profile *pr=&profiles[idx];
        int size=pr->min_kb+(int)(rng_next(&state)%(unsigned)(pr->max_kb-pr->min_kb+1));
        int dur=pr->min_ticks+(int)(rng_next(&state)%(unsigned)(pr->max_ticks-pr->min_ticks+1));
        memset(&out[i],0,sizeof(Proc));
        out[i].id=i+1; snprintf(out[i].name,sizeof(out[i].name),"%s_%03d",pr->type,i+1);
        snprintf(out[i].type,sizeof(out[i].type),"%s",pr->type);
        out[i].size_bytes=(long)size*KB; out[i].units=size; out[i].duration=dur;
        out[i].arrive_tick=i/10; out[i].start_tick=-1; out[i].finish_tick=-1; out[i].wait_start_tick=-1; out[i].state=P_NEW;
    }
    return n;
}

void queue_init(ProcQueue *q) { if(q) memset(q,0,sizeof(*q)); }
int queue_push(ProcQueue *q,int pid) {
    if(!q || q->count>=MAX_PROCS) return 0;
    q->items[q->rear]=pid; q->rear=(q->rear+1)%MAX_PROCS; q->count++; return 1;
}
int queue_pop(ProcQueue *q,int *pid) {
    if(!q || q->count==0) return 0;
    if(pid) *pid=q->items[q->front];
    q->front=(q->front+1)%MAX_PROCS; q->count--; return 1;
}
int queue_size(const ProcQueue *q) { return q?q->count:0; }

void refresh_process_addresses(Proc *p,int n,const Memory *m) {
    for(int i=0;i<n;i++) if(p[i].state==P_RUNNING)
        mem_lookup(m,p[i].name,&p[i].start_addr,&p[i].end_addr);
}

static void attempt(Proc *p, int n, int i, Memory *m, Strategy strategy,
                    int tick, ProcQueue *q, Metrics *met) {
    Proc *x=&p[i];
    if(x->units>m->total_units) {
        x->state=P_REJECTED;
        if(!x->first_try_failure) { x->first_try_failure=2; met->oom_failures++; }
        log_event(tick,x->id,"REJECTED: request exceeds entire pool");
        return;
    }
    int compact_first=0;
    Strategy use=strategy==SMART_FIT?engine_choose(m,x->units,&compact_first):strategy;
    if(compact_first) {
        Stats before=mem_stats(m); mem_compact(m); refresh_process_addresses(p,n,m);
        met->compactions++; met->units_moved=m->units_moved;
        log_event(tick,x->id,"Smart engine compacted scattered free memory");
        Stats after=mem_stats(m);
        (void)before; (void)after;
    }
    int examined=0;
    Block *b=find_block(m,x->units,use,&examined);
    met->total_searches+=examined; met->allocation_attempts++;
    if(b) {
        /* Policy selected the candidate; memory manager performs the actual split. */
        b=mem_alloc(m,x->name,x->units,use);
        if(b) {
            x->start_addr=b->start; x->end_addr=b->start+b->size;
            x->start_tick=tick; x->finish_tick=tick+x->duration; x->state=P_RUNNING;
            if(x->wait_start_tick>=0) x->wait_ticks=tick-x->wait_start_tick;
            if(!x->first_try_failure) met->first_try_success++;
            log_event(tick,x->id,"ALLOCATED successfully");
            return;
        }
    }
    if(!x->first_try_failure) {
        x->first_try_failure=failure_kind(m,x->units);
        if(x->first_try_failure==1) met->fragmentation_failures++;
        else met->oom_failures++;
    }
    x->state=P_WAITING;
    if(x->wait_start_tick<0) x->wait_start_tick=tick;
    if(!x->queued && queue_push(q,i)) x->queued=1;
    { char reason[160]; Stats now=mem_stats(m);
      snprintf(reason,sizeof(reason),"WAITING: %s (free=%d KB, largest=%d KB)",
               x->first_try_failure==1?"fragmentation failure":"insufficient total free memory",now.total_free,now.largest_free);
      log_event(tick,x->id,reason); }
}

static void sim_run_internal(Proc *p,int n,Memory *m,Strategy strategy,Metrics *out,int show_map) {
    if(!p||!m||!out||n<0) return;
    memset(out,0,sizeof(*out)); out->process_count=n;
    ProcQueue q; queue_init(&q);
    int remaining=n, tick=0, max_ticks=100000;
    double frag_sum=0; int frag_samples=0;
    while(remaining>0 && tick<max_ticks) {
        for(int i=0;i<n;i++) if(p[i].state==P_RUNNING && p[i].finish_tick==tick) {
            Stats before_free=mem_stats(m);
            mem_free(m,p[i].name); p[i].state=P_COMPLETED;
            Stats after_free=mem_stats(m);
            remaining--;
            log_event(tick,p[i].id,after_free.free_fragments<before_free.free_fragments ?
                      "COMPLETED: block released; adjacent free blocks coalesced" :
                      "COMPLETED: block released; no adjacent free block to merge");
        }
        /* Retry in FIFO arrival order; retain those that still do not fit. */
        int qcount=queue_size(&q);
        for(int k=0;k<qcount;k++) {
            int idx = -1; if (!queue_pop(&q,&idx)) continue; if (idx < 0 || idx >= n) continue; p[idx].queued=0;
            if(p[idx].state==P_WAITING) attempt(p,n,idx,m,strategy,tick,&q,out);
        }
        for(int i=0;i<n;i++) if(p[i].state==P_NEW && p[i].arrive_tick<=tick) {
            p[i].state=P_READY; log_event(tick,p[i].id,"ARRIVED");
            attempt(p,n,i,m,strategy,tick,&q,out);
        }
        Stats st=mem_stats(m); frag_sum+=st.external_fragmentation; frag_samples++;
        log_snapshot(tick,&st);
        if(mem_check_invariants(m)!=0) { fprintf(stderr,"FATAL: memory invariant failed at tick %d\n",tick); break; }
        if(show_map) { printf("\n--- LIVE MEMORY SNAPSHOT AT TICK %d ---\n",tick); mem_print_map(m); }
        tick++;
        int active=0;
        for(int i=0;i<n;i++) if(p[i].state==P_RUNNING || p[i].state==P_WAITING || p[i].state==P_NEW) active++;
        if(active && tick<max_ticks) {
            int running=0;
            for(int i=0;i<n;i++) if(p[i].state==P_RUNNING) running++;
            if(!running && queue_size(&q)>0) {
                /* With no running blocks, all memory is free; retry next iteration. */
            }
        }
        remaining=0;
        for(int i=0;i<n;i++) if(p[i].state!=P_COMPLETED && p[i].state!=P_REJECTED) remaining++;
    }
    out->accepted=0; out->waiting_processes=0; out->total_wait_ticks=0; out->waiters=0;
    out->makespan=tick?tick-1:0; out->compactions=m->compactions; out->units_moved=m->units_moved;
    for(int i=0;i<n;i++) {
        if(p[i].state==P_RUNNING) { mem_free(m,p[i].name); p[i].state=P_COMPLETED; }
        if(p[i].state!=P_REJECTED) out->accepted++;
        if(p[i].wait_ticks>0) { out->waiting_processes++; out->total_wait_ticks+=p[i].wait_ticks; out->waiters++; }
        if(p[i].finish_tick>out->makespan) out->makespan=p[i].finish_tick;
    }
    out->avg_wait=out->waiters?(double)out->total_wait_ticks/out->waiters:0;
    out->success_rate=out->accepted?(double)out->first_try_success/out->accepted*100.0:0;
    out->avg_search_length=out->allocation_attempts?(double)out->total_searches/out->allocation_attempts:0;
    out->avg_fragmentation=frag_samples?frag_sum/frag_samples:0;
    Stats final_stats=mem_stats(m);
    out->final_free_memory=final_stats.total_free; out->final_used_memory=final_stats.used_memory;
    out->final_largest_free=final_stats.largest_free; out->final_free_fragments=final_stats.free_fragments;
}

void sim_run(Proc *p,int n,Memory *m,Strategy strategy,Metrics *out) {
    sim_run_internal(p,n,m,strategy,out,0);
}
void sim_run_display(Proc *p,int n,Memory *m,Strategy strategy,Metrics *out) {
    sim_run_internal(p,n,m,strategy,out,1);
}

int add_real_file_process(Proc *p,int *count,const char *path,int duration,int pool_units) {
    if(!p||!count||*count>=MAX_PROCS||duration<=0) return 0;
    long bytes=measure_file_size(path);
    if(bytes<=0) { printf("Cannot add file: missing, unreadable, folder or empty file.\n"); return 0; }
    int units=bytes_to_units(bytes,KB);
    if(units<=0) return 0;
    Proc *x=&p[*count]; memset(x,0,sizeof(*x)); x->id=*count+1;
    const char *base=strrchr(path,'/'); const char *win=strrchr(path,'\\');
    if(win && (!base || win>base)) base=win;
    snprintf(x->name,sizeof(x->name),"%s",base?base+1:path);
    for(int i=0;i<*count;i++) if(strcmp(p[i].name,x->name)==0) {
        printf("Duplicate file name rejected: %s\n",x->name); return 0;
    }
    const char *dot=strrchr(x->name,'.'); snprintf(x->type,sizeof(x->type),"%s",dot?dot+1:"file");
    x->from_real_file=1; x->size_bytes=bytes; x->units=units; x->duration=duration;
    x->arrive_tick=*count/3; x->start_tick=-1; x->finish_tick=-1; x->wait_start_tick=-1; x->state=P_NEW;
    (*count)++;
    printf("Added %s: %ld bytes -> %d KB units%s\n",x->name,bytes,units,units>pool_units?" (will be rejected: larger than pool)":"");
    return 1;
}
