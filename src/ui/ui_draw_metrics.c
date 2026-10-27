/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_draw_metrics.h"
#include <inttypes.h>
#include <limits.h>
#include <string.h>
enum { UI_BUCKETS=1024, UI_QUANTUM_US=100 };
static const char *const screen_names[X4_UI_SCREEN_COUNT]={
    "home","catalog","account","fortnite","about","support","loading","queue","errors",
    "options_picture","options_server","first_time","keyboard","lab_settings","demo","other"
};
#if !X4_PUBLIC
typedef struct {
    uint32_t bins[UI_BUCKETS];
    uint64_t n,total,max,overflow,saturated;
} Timing;
typedef struct {
    Timing draw,copy,present;
    uint64_t presentations,copy_only,bytes;
    bool dirty;
} Row;
static Row rows[X4_UI_SCREEN_COUNT][2];
static int last_key=-1;
static uint64_t last_emit_us,last_dirty_skips;
static void add(Timing *h,uint64_t us)
{
    uint64_t bucket=us/UI_QUANTUM_US;
    if(bucket>=UI_BUCKETS){bucket=UI_BUCKETS-1;++h->overflow;}
    if(h->bins[bucket]!=UINT32_MAX)++h->bins[bucket];else ++h->saturated;
    ++h->n;h->total+=us;if(us>h->max)h->max=us;
}
static uint64_t percentile(const Timing *h,unsigned pct,bool *overflow)
{
    *overflow=false;uint64_t target=(h->n*pct+99)/100,sum=0;if(!target)return 0;
    for(unsigned i=0;i<UI_BUCKETS;++i)if((sum+=h->bins[i])>=target){
        if(i==UI_BUCKETS-1){*overflow=true;return (uint64_t)(UI_BUCKETS-1)*UI_QUANTUM_US;}
        return (uint64_t)(i+1)*UI_QUANTUM_US;
    }
    *overflow=true;return (uint64_t)(UI_BUCKETS-1)*UI_QUANTUM_US;
}
static X4UiTimingView view(const Timing *h)
{
    X4UiTimingView v={.n=h->n,.max_us=h->max,.total_us=h->total,.overflow=h->overflow+h->saturated};
    v.p50_us=percentile(h,50,&v.p50_overflow);v.p95_us=percentile(h,95,&v.p95_overflow);return v;
}
static void emit(enum X4UiScreen screen,unsigned navigation)
{
    Row *row=&rows[screen][navigation];if(!row->dirty)return;
    X4UiDrawMetricView v={row->presentations,row->copy_only,row->bytes,view(&row->draw),view(&row->copy),view(&row->present)};
    x4_ui_draw_metrics_write(stdout,screen,navigation!=0,&v,last_dirty_skips);row->dirty=false;
}
#endif
void x4_ui_draw_metrics_write(FILE *f,enum X4UiScreen screen,bool navigation,const X4UiDrawMetricView *v,uint64_t skips)
{
    if(!f||!v||(unsigned)screen>=X4_UI_SCREEN_COUNT)return;
    fprintf(f,"XCloud4 UI draw: screen=%s phase=%s frames=%" PRIu64 " present_frames=%" PRIu64
        " draw_p50_us=%" PRIu64 " draw_p95_us=%" PRIu64 " draw_max_us=%" PRIu64
        " copy_p50_us=%" PRIu64 " copy_p95_us=%" PRIu64 " copy_max_us=%" PRIu64
        " present_p50_us=%" PRIu64 " present_p95_us=%" PRIu64 " present_max_us=%" PRIu64
        " copied_bytes=%" PRIu64 " copy_only=%" PRIu64 " dirty_skips=%" PRIu64
        " bucket_us=100 draw_overflow=%" PRIu64 " copy_overflow=%" PRIu64 " present_overflow=%" PRIu64
        " draw_p50_overflow=%u draw_p95_overflow=%u copy_p95_overflow=%u present_p95_overflow=%u\n",
        screen_names[screen],navigation?"navigation":"full",v->draw.n,v->presentations,
        v->draw.p50_us,v->draw.p95_us,v->draw.max_us,v->copy.p50_us,v->copy.p95_us,v->copy.max_us,
        v->present.p50_us,v->present.p95_us,v->present.max_us,v->copied_bytes,v->copy_only,skips,
        v->draw.overflow,v->copy.overflow,v->present.overflow,(unsigned)v->draw.p50_overflow,
        (unsigned)v->draw.p95_overflow,(unsigned)v->copy.p95_overflow,(unsigned)v->present.p95_overflow);
}
void x4_ui_draw_metrics_reset(void)
{
#if !X4_PUBLIC
    memset(rows,0,sizeof(rows));last_key=-1;last_emit_us=last_dirty_skips=0;
#endif
}
void x4_ui_draw_metrics_record(enum X4UiScreen screen,bool navigation,bool redrawn,
    uint64_t draw_us,uint64_t copy_us,uint64_t present_us,uint64_t bytes,uint64_t now,uint64_t skips)
{
#if !X4_PUBLIC
    if((unsigned)screen>=X4_UI_SCREEN_COUNT)return;
    int key=(int)screen*2+(navigation?1:0);last_dirty_skips=skips;
    if(last_key>=0&&last_key!=key)emit((enum X4UiScreen)(last_key/2),(unsigned)last_key%2);
    last_key=key;Row *row=&rows[screen][navigation?1:0];
    ++row->presentations;row->bytes+=bytes;row->dirty=true;
    if(redrawn)add(&row->draw,draw_us);else ++row->copy_only;
    add(&row->copy,copy_us);add(&row->present,present_us);x4_ui_draw_metrics_poll(now,skips);
#else
    (void)screen;(void)navigation;(void)redrawn;(void)draw_us;(void)copy_us;(void)present_us;(void)bytes;(void)now;(void)skips;
#endif
}
void x4_ui_draw_metrics_flush(uint64_t now)
{
#if !X4_PUBLIC
    for(unsigned s=0;s<X4_UI_SCREEN_COUNT;++s)for(unsigned n=0;n<2;++n)emit((enum X4UiScreen)s,n);
    last_emit_us=now;
#else
    (void)now;
#endif
}
void x4_ui_draw_metrics_poll(uint64_t now,uint64_t skips)
{
#if !X4_PUBLIC
    last_dirty_skips=skips;if(!last_emit_us)last_emit_us=now;
    if(now>=last_emit_us&&now-last_emit_us>=5000000u)x4_ui_draw_metrics_flush(now);
#else
    (void)now;(void)skips;
#endif
}
bool x4_ui_draw_metrics_snapshot(enum X4UiScreen screen,bool navigation,X4UiDrawMetricView *out)
{
#if !X4_PUBLIC
    if(!out||(unsigned)screen>=X4_UI_SCREEN_COUNT)return false;
    const Row *r=&rows[screen][navigation?1:0];
    *out=(X4UiDrawMetricView){r->presentations,r->copy_only,r->bytes,view(&r->draw),view(&r->copy),view(&r->present)};return true;
#else
    (void)screen;(void)navigation;if(out)memset(out,0,sizeof(*out));return false;
#endif
}
