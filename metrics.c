#include "metrics.h"
#include "process.h"
#include "memory.h"
#include "policy.h"
#include <time.h>

static FILE *event_fp=NULL;
static int event_owned=0;
static FILE *trace_fp=NULL;
void log_begin(const char *path) {
    if(event_fp && event_owned) fclose(event_fp);
    event_fp=fopen(path?path:LOG_FILE,"w"); event_owned=event_fp!=NULL;
    if(trace_fp) fclose(trace_fp);
    trace_fp=fopen("simulation_trace.csv","w");
    if(trace_fp) fprintf(trace_fp,"tick,used_kb,free_kb,free_fragments,largest_free_kb,external_fragmentation_pct\n");
}
void log_end(void) { if(event_fp && event_owned) fclose(event_fp); if(trace_fp) fclose(trace_fp); event_fp=NULL; trace_fp=NULL; event_owned=0; }
void log_event(int tick,int pid,const char *msg) {
    if(!event_fp) return; /* experiments/tests can disable logging entirely */
    fprintf(event_fp,"tick=%d pid=%d %s\n",tick,pid,msg?msg:"");
    printf("[event] tick=%d pid=%d %s\n",tick,pid,msg?msg:"");
}
void log_snapshot(int tick,const Stats *s) {
    if(trace_fp && s) fprintf(trace_fp,"%d,%d,%d,%d,%d,%.4f\n",tick,s->used_memory,s->total_free,s->free_fragments,s->largest_free,s->external_fragmentation);
}
void metrics_print(const Metrics *m) {
    if(!m) return;
    printf("\nMETRICS\nProcesses: %d | accepted: %d | first-attempt success: %d (%.2f%%)\n",m->process_count,m->accepted,m->first_try_success,m->success_rate);
    printf("Fragmentation failures: %d | out-of-memory failures: %d\n",m->fragmentation_failures,m->oom_failures);
    printf("Waiting processes: %d | average wait: %.2f ticks | makespan: %d ticks\n",m->waiting_processes,m->avg_wait,m->makespan);
    printf("Average external fragmentation: %.2f%% | average search length: %.2f blocks\n",m->avg_fragmentation,m->avg_search_length);
    printf("Final memory: used %d KB | free %d KB | largest free block %d KB | fragments %d\n",m->final_used_memory,m->final_free_memory,m->final_largest_free,m->final_free_fragments);
    printf("Compactions: %d | units moved: %ld\n",m->compactions,m->units_moved);
}
static void csv_metrics(FILE *f,const char *mode,const char *pattern,int pool,int count,unsigned seed,int strategy,const Metrics *m) {
    fprintf(f,"%s,%s,%d,%d,%u,%s,%d,%d,%d,%d,%d,%.4f,%.4f,%d,%d,%d,%d,%.4f,%.4f,%d,%ld,%d\n",mode,pattern,pool,count,seed,strategy_name((Strategy)strategy),m->accepted,m->first_try_success,m->fragmentation_failures,m->oom_failures,m->waiting_processes,m->success_rate,m->avg_wait,m->makespan,m->final_used_memory,m->final_free_memory,m->final_largest_free,m->avg_fragmentation,m->avg_search_length,m->compactions,m->units_moved,m->rank_sum);
}
void run_comparison(const Proc *workload,int n,int pool_units) {
    if(!workload || n<1 || n>MAX_PROCS) return;
    Strategy ss[4]={FIRST_FIT,BEST_FIT,WORST_FIT,SMART_FIT}; Metrics results[4];
    printf("\nFAIR STRATEGY COMPARISON (identical workload, fresh memory)\n");
    printf("%-12s %9s %9s %9s %10s %10s %8s %8s\n","Strategy","Success%","FragFail","OOM","AvgWait","Makespan","Search","Compacts");
    for(int k=0;k<4;k++) {
        Proc *copy=malloc((size_t)n*sizeof(*copy)); if(!copy) return;
        memcpy(copy,workload,(size_t)n*sizeof(*copy)); Memory *m=mem_create(pool_units,KB); if(!m){free(copy);return;}
        sim_run(copy,n,m,ss[k],&results[k]);
        printf("%-12s %9.2f %9d %9d %10.2f %10d %8.2f %8d\n",strategy_name(ss[k]),results[k].success_rate,results[k].fragmentation_failures,results[k].oom_failures,results[k].avg_wait,results[k].makespan,results[k].avg_search_length,results[k].compactions);
        mem_destroy(m); free(copy);
    }
    char why[256]; Strategy winner=recommend(results,4,why,sizeof(why));
    { int ranks[4]; calculate_rank_sums(results,ranks); printf("Rank sums (First/Best/Worst/Smart): %d / %d / %d / %d\n",ranks[0],ranks[1],ranks[2],ranks[3]); }
    printf("Recommendation: %s\nReason: %s\n",strategy_name(winner),why);
}
static const char *pattern_name(int p){return p==0?"light":p==1?"balanced":"heavy";}
void run_experiments(void) {
    const int pools[]={1024,4096,16384}, counts[]={10,50,100};
    FILE *csv=fopen("experiment_results.csv","w");
    if(!csv){puts("Could not create experiment_results.csv");return;}
    fprintf(csv,"mode,pattern,pool_kb,processes,seed,strategy,accepted,first_try_success,fragmentation_failures,oom_failures,waiting_processes,success_rate_pct,avg_wait_ticks,makespan_ticks,final_used_kb,final_free_kb,largest_free_kb,avg_fragmentation_pct,avg_search_length,compactions,units_moved,rank_sum\n");
    printf("\nEXPERIMENTS: 30 seeds; file and program profiles; raw results -> experiment_results.csv\n");
    for(int mode=0;mode<2;mode++) for(int p=0;p<3;p++) for(int pool=0;pool<3;pool++) {
        double frag[4]={0},success[4]={0},wait[4]={0}; long long makespan[4]={0};
        for(unsigned seed=1;seed<=30;seed++) {
            int n=counts[pool]; Proc *work=malloc((size_t)n*sizeof(*work)); Metrics seed_metrics[4];
            if(!work) continue;
            generate_processes(work,n,seed,p,(ProfileMode)mode);
            for(int k=0;k<4;k++) {
                Proc *copy=malloc((size_t)n*sizeof(*copy)); if(!copy) continue; memcpy(copy,work,(size_t)n*sizeof(*copy));
                Memory *m=mem_create(pools[pool],KB);
                if(m){sim_run(copy,n,m,(Strategy)k,&seed_metrics[k]);frag[k]+=seed_metrics[k].avg_fragmentation;success[k]+=seed_metrics[k].success_rate;wait[k]+=seed_metrics[k].avg_wait;makespan[k]+=seed_metrics[k].makespan;mem_destroy(m);}
                free(copy);
            }
            int ranks[4]; calculate_rank_sums(seed_metrics,ranks);
            for(int k=0;k<4;k++){seed_metrics[k].rank_sum=ranks[k];csv_metrics(csv,mode?"program":"file",pattern_name(p),pools[pool],n,seed,k,&seed_metrics[k]);}
            free(work);
        }
        Metrics aggregate[4]; memset(aggregate,0,sizeof(aggregate));
        for(int k=0;k<4;k++){aggregate[k].avg_fragmentation=frag[k]/30;aggregate[k].success_rate=success[k]/30;aggregate[k].avg_wait=wait[k]/30;aggregate[k].makespan=(int)(makespan[k]/30);}
        int ranks[4];calculate_rank_sums(aggregate,ranks);char why[200];Strategy rec=recommend(aggregate,4,why,sizeof(why));
        printf("%s/%s pool=%d KB n=%d | frag FF %.2f BF %.2f WF %.2f Smart %.2f | rank sums %d,%d,%d,%d | recommend %s\n",mode?"Program":"File",pattern_name(p),pools[pool],counts[pool],frag[0]/30,frag[1]/30,frag[2]/30,frag[3]/30,ranks[0],ranks[1],ranks[2],ranks[3],strategy_name(rec));
        printf("  %s\n",why);
    }
    fclose(csv);
    FILE *tf=fopen("timing_results.csv","w");
    if(tf){
        fprintf(tf,"processes,seed,strategy,repetitions,average_elapsed_seconds\n");
        const int sizes[]={1000,5000,10000};
        for(int z=0;z<3;z++){
            int n=sizes[z]; Proc *work=malloc((size_t)n*sizeof(*work)); if(!work)continue;
            generate_processes(work,n,2026,BALANCED_PATTERN,FILE_PROFILES);
            for(int k=0;k<4;k++){
                double total=0; const int reps=3;
                for(int rep=0;rep<reps;rep++){
                    Proc *copy=malloc((size_t)n*sizeof(*copy)); if(!copy)continue;memcpy(copy,work,(size_t)n*sizeof(*copy));
                    Memory *m=mem_create(16384,KB);if(!m){free(copy);continue;}
                    clock_t a=clock();Metrics mt;sim_run(copy,n,m,(Strategy)k,&mt);total+=(double)(clock()-a)/CLOCKS_PER_SEC;
                    mem_destroy(m);free(copy);
                }
                fprintf(tf,"%d,2026,%s,%d,%.6f\n",n,strategy_name((Strategy)k),reps,total/reps);
                printf("Timing n=%d %-10s average %.4f s (%d runs)\n",n,strategy_name((Strategy)k),total/reps,reps);
            }
            free(work);
        }
        fclose(tf);
    }
    puts("Saved experiment_results.csv and timing_results.csv. Both include raw results for report tables and charts.");
}
