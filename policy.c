#include "policy.h"

Block *find_block(const Memory *m, int units, Strategy strategy, int *examined) {
    Block *chosen=NULL; int count=0;
    if (!m || units<=0) { if(examined) *examined=0; return NULL; }
    for (Block *b=m->head;b;b=b->next) {
        count++; if(!b->is_free || b->size<units) continue;
        if(strategy==FIRST_FIT) { chosen=b; break; }
        if(!chosen || (strategy==BEST_FIT && b->size<chosen->size) ||
           (strategy==WORST_FIT && b->size>chosen->size)) chosen=b;
    }
    if(examined) *examined=count;
    return chosen;
}


Strategy engine_choose(const Memory *m, int units, int *compact_first) {
    if (compact_first) *compact_first = 0;
    if (!m) return BEST_FIT;
    Stats s = mem_stats(m);
    if (units > s.largest_free && units <= s.total_free) {
        if (compact_first) *compact_first = 1;
        return FIRST_FIT;
    }
    if ((double)s.total_free / m->total_units > 0.60) return FIRST_FIT;
    return BEST_FIT;
}

int failure_kind(const Memory *m, int units) {
    if (!m || units <= 0) return 2;
    Stats s = mem_stats(m);
    return s.total_free >= units ? 1 : 2;
}

void calculate_rank_sums(const Metrics r[4], int rank_sum[4]) {
    if(!r || !rank_sum) return;
    for(int i=0;i<4;i++) rank_sum[i]=0;
    for(int metric=0;metric<5;metric++) {
        int order[4]={0,1,2,3};
        for(int i=0;i<4;i++) for(int j=i+1;j<4;j++) {
            double a,b;
            if(metric==0){a=r[order[i]].success_rate;b=r[order[j]].success_rate;}
            else if(metric==1){a=r[order[i]].avg_wait;b=r[order[j]].avg_wait;}
            else if(metric==2){a=r[order[i]].makespan;b=r[order[j]].makespan;}
            else if(metric==3){a=r[order[i]].avg_fragmentation;b=r[order[j]].avg_fragmentation;}
            else {a=r[order[i]].avg_search_length;b=r[order[j]].avg_search_length;}
            int swap = metric==0 ? a<b : a>b;
            if(swap){int t=order[i];order[i]=order[j];order[j]=t;}
        }
        /* Competition ranking: equal metric values receive the same rank. */
        int rank=1;
        for(int pos=0;pos<4;pos++) {
            if(pos>0) {
                int prev=order[pos-1], cur=order[pos]; double a,b;
                if(metric==0){a=r[prev].success_rate;b=r[cur].success_rate;}
                else if(metric==1){a=r[prev].avg_wait;b=r[cur].avg_wait;}
                else if(metric==2){a=r[prev].makespan;b=r[cur].makespan;}
                else if(metric==3){a=r[prev].avg_fragmentation;b=r[cur].avg_fragmentation;}
                else {a=r[prev].avg_search_length;b=r[cur].avg_search_length;}
                if(a!=b) rank=pos+1;
            }
            rank_sum[order[pos]]+=rank;
        }
    }
}

Strategy recommend(const Metrics r[], int n, char *why, size_t why_size) {
    if (!r || n < 1) return FIRST_FIT;
    int rank_sum[4]={0,0,0,0}; calculate_rank_sums(r,rank_sum);
    int best=rank_sum[0], ties=0, winner=0;
    for(int i=1;i<4;i++) if(rank_sum[i]<best){best=rank_sum[i];winner=i;}
    for(int i=0;i<4;i++) if(rank_sum[i]==best) ties++;
    const char *names[]={"First Fit","Best Fit","Worst Fit","Smart"};
    if(why && why_size) {
        if(ties>1) snprintf(why,why_size,"Tie: %d strategies share the lowest rank sum (%d); no clear winner.",ties,best);
        else snprintf(why,why_size,"%s has the lowest rank sum (%d). Metrics: success rate, waiting time, makespan, fragmentation and search length.",names[winner],best);
    }
    return (Strategy)winner;
}
