/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include <stdio.h>
/* One owner per histogram, export only after that owner has stopped. Fixed
 * 50us upper bins; overflow uses the actual maximum, never a false bound. */
enum { X4_UI_PERF_BINS=512, X4_UI_PERF_QUANTUM_US=50 };
typedef struct { uint64_t bins[X4_UI_PERF_BINS],n,total,max; } X4UiPerf;
static inline void x4_ui_perf_record(X4UiPerf *p,uint64_t us)
{
    uint64_t bin=us/X4_UI_PERF_QUANTUM_US;
    if(bin>=X4_UI_PERF_BINS)bin=X4_UI_PERF_BINS-1;
    ++p->bins[bin];++p->n;p->total+=us;if(us>p->max)p->max=us;
}
static inline uint64_t x4_ui_perf_percentile(const X4UiPerf *p,unsigned percent)
{
    uint64_t target=(p->n*percent+99)/100,sum=0;if(!target)return 0;
    for(unsigned i=0;i<X4_UI_PERF_BINS;++i)if((sum+=p->bins[i])>=target)
        return i==X4_UI_PERF_BINS-1?p->max:(uint64_t)(i+1)*X4_UI_PERF_QUANTUM_US;
    return p->max;
}
static inline void x4_ui_perf_write(FILE *f,const X4UiPerf *p)
{
    fprintf(f,"{\"n\":%llu,\"p50_upper_us\":%llu,\"p95_upper_us\":%llu,\"maximum_us\":%llu,\"total_us\":%llu,\"bin_us\":50}",
        (unsigned long long)p->n,(unsigned long long)x4_ui_perf_percentile(p,50),
        (unsigned long long)x4_ui_perf_percentile(p,95),(unsigned long long)p->max,(unsigned long long)p->total);
}
