/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/ui/loading_screen.h"
#include "../src/ui/apple_ui.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"

#define W 1920
#define H 1080
#define PIXELS ((size_t)W*H)
static uint32_t *owned[3],*pixels[3];
static uint64_t now_ns(void)
{struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;}
static int cmp_u64(const void *a,const void *b)
{uint64_t aa=*(const uint64_t *)a,bb=*(const uint64_t *)b;return aa<bb?-1:aa>bb;}
static void guards(void)
{for(unsigned i=0;i<3;++i)assert(owned[i][0]==0xaabbccddu&&owned[i][PIXELS+1]==0xaabbccddu);}
static void capture(const char *dir,const char *mode,const char *stage,const char *lang,unsigned slot)
{
    guards();if(!dir)return;
    char name[1024];snprintf(name,sizeof(name),"%s/20b_loading_%s_%s_%s.ppm",dir,stage,mode,lang);
    FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",W,H);
    unsigned char *row=malloc(W*3);assert(row);
    for(unsigned y=0;y<H;++y){for(unsigned x=0;x<W;++x){uint32_t p=pixels[slot][(size_t)y*W+x];row[x*3]=(unsigned char)p;row[x*3+1]=(unsigned char)(p>>8);row[x*3+2]=(unsigned char)(p>>16);}assert(fwrite(row,3,W,f)==W);}
    free(row);assert(!fclose(f));
}
static void neutral(X4CoverRaster *r)
{
    *r=(X4CoverRaster){W,H,malloc(PIXELS*4)};assert(r->rgba);
    /* Original 16:9 synthetic backdrop. This is a test, not game artwork. */
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        unsigned blue=75+(H-y)*95/H,green=56+x*95/W,red=32+y*24/H;
        if(y>650+(x/110%4)*35){red=36;green=54;blue=70;}
        r->rgba[(size_t)y*W+x]=X4_RGB(red,green,blue);
    }
}
static void snapshots(const char *dir,const X4CoverRaster *r,const char *mode)
{
    static const struct{enum X4SessionState state;int rtc;uint64_t at;const char *name;unsigned pct;} stages[]={
        {X4_SESSION_STARTING,0,1000000,"request_05",5},
        {X4_SESSION_WAITING,0,2000000,"resources_20",20},
        {X4_SESSION_READY,0,2500000,"resources_ready_35",35},
        {X4_SESSION_AUTHORIZING,0,3000000,"authorize_42",42},
        {X4_SESSION_AUTHORIZED,0,3500000,"authorized_49",49},
        {X4_SESSION_NEGOTIATING,0,4000000,"connect_55",55},
        {X4_SESSION_CONNECTING,0,4500000,"connecting_64",64},
        {X4_SESSION_STREAMING,1,5000000,"connected_70",70},
        {X4_SESSION_STREAMING,1,10000000,"first_picture_74",74},
        {X4_SESSION_STREAMING,1,21000000,"first_picture_83",83},
        {X4_SESSION_STREAMING,1,35000000,"first_picture_95",95}
    };
    for(unsigned lang=0;lang<2;++lang){
        x4_lang_set(lang?X4_LANG_EN:X4_LANG_ES,lang?"en-US":"es-MX",lang?"US":"MX");
        const char *suffix=lang?"en":"es";
        assert(x4_loading_screen_begin(lang?"Sample adventure":"Aventura de prueba",r,1000000));
        unsigned slot=0;
        for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);++i){
            X4SessionSnapshot s={.state=stages[i].state,.rtc_connected=stages[i].rtc};
            x4_loading_screen_update(&s,stages[i].at);
            assert(x4_loading_screen_draw(pixels[slot],stages[i].at));
            X4LoadingView v;x4_loading_screen_snapshot(&v);assert(v.percent==stages[i].pct);
            assert(v.background_preparations==1&&v.has_game_background==(r!=NULL));
            capture(dir,mode,stages[i].name,suffix,slot);slot=(slot+1)%3;
        }
        X4LoadingView before;x4_loading_screen_snapshot(&before);assert(before.delayed_notice);
        X4SessionSnapshot backwards={.state=X4_SESSION_STARTING};
        x4_loading_screen_update(&backwards,60000000);X4LoadingView v;x4_loading_screen_snapshot(&v);assert(v.percent==95);
        x4_loading_screen_first_picture(61000000);x4_loading_screen_snapshot(&v);assert(v.percent==100&&v.first_picture);
        assert(x4_loading_screen_draw(pixels[slot],61000000));capture(dir,mode,"complete_100",suffix,slot);
        /* This is the one and only native-picture notification. No AU,
         * session update, conversion or overlay callback follows it. The UI
         * clock alone must permit the normal handoff after exactly160ms. */
        uint32_t *complete=malloc(PIXELS*4);assert(complete);memcpy(complete,pixels[slot],PIXELS*4);
        assert(!x4_loading_screen_transition_ready(61000000));
        assert(x4_loading_screen_draw(pixels[slot],61080000));
        size_t black=0;
        for(size_t i=0;i<PIXELS;++i){
            if(pixels[slot][i]==0xff000000u)++black;
            else assert(pixels[slot][i]==complete[i]);
        }
        assert(black>PIXELS*45/100&&black<PIXELS*55/100);
        capture(dir,mode,"fade_80ms",suffix,slot);
        assert(!x4_loading_screen_transition_ready(61159999));
        assert(x4_loading_screen_transition_ready(61160000));
        x4_loading_screen_snapshot(&v);
        assert(v.first_picture_us==61000000&&v.transition_ready_us==61160000&&v.transition_wait_us==160000);
        assert(x4_loading_screen_draw(pixels[slot],61160000));
        for(size_t i=0;i<PIXELS;++i)assert(pixels[slot][i]==0xff000000u);
        assert(!x4_loading_screen_draw(pixels[slot],61160001)); /* no repeated stores */
        capture(dir,mode,"handoff_dark_160ms",suffix,slot);
        free(complete);
        x4_loading_screen_end();assert(!x4_loading_screen_active());guards();
    }
}
static void failure_and_cancel(void)
{
    assert(x4_loading_screen_begin("Failure fixture",NULL,1));
    X4SessionSnapshot s={.state=X4_SESSION_ERROR,.error=-1};
    x4_loading_screen_update(&s,100000);assert(!x4_loading_screen_draw(pixels[0],100000));
    X4LoadingView v;x4_loading_screen_snapshot(&v);assert(v.stage==X4_LOADING_ERROR);
    assert(x4_loading_screen_transition_ready(100000));
    /* Returning false selects the existing TAREA20 error card in main. */
    x4_loading_screen_end();assert(!x4_loading_screen_draw(pixels[0],200000));
    assert(x4_loading_screen_begin("Cancel fixture",NULL,300000));
    x4_loading_screen_end();assert(!x4_loading_screen_active());
    assert(x4_loading_screen_transition_ready(300001));
    X4CoverRaster invalid={100,100,pixels[0]};
    assert(x4_loading_screen_begin("Aspect fallback",&invalid,400000));
    x4_loading_screen_snapshot(&v);assert(!v.has_game_background);
    x4_loading_screen_end();
}
static void benchmark(void)
{
    assert(x4_loading_screen_begin("Loading benchmark",NULL,1000000));
    X4SessionSnapshot s={.state=X4_SESSION_STREAMING,.rtc_connected=1};
    x4_loading_screen_update(&s,1000000);
    for(unsigned i=0;i<3;++i)assert(x4_loading_screen_draw(pixels[i],1000000));
    X4LoadingView initial;x4_loading_screen_snapshot(&initial);
    assert(initial.full_buffer_initializations==3);
    /* Stable 30 Hz writes stay strictly within the central animation zone. */
    uint32_t corner=pixels[0][0];pixels[0][0]=0xff112233u;
    size_t fixed=(size_t)570*W+600;uint32_t fixed_value=pixels[0][fixed];pixels[0][fixed]=0xff456789u;
    uint64_t elapsed[300];
    for(unsigned i=0;i<300;++i){
        uint64_t t=1033334ULL+(uint64_t)i*33334ULL;
        uint64_t begin=now_ns();x4_loading_screen_update(&s,t);
        assert(x4_loading_screen_draw(pixels[i%3],t));elapsed[i]=now_ns()-begin;
    }
    assert(pixels[0][0]==0xff112233u);pixels[0][0]=corner;
    assert(pixels[0][fixed]==0xff456789u);pixels[0][fixed]=fixed_value;
    /* A second call in the same tick and same framebuffer does no work. */
    assert(!x4_loading_screen_draw(pixels[2],11000000));
    X4LoadingView end;x4_loading_screen_snapshot(&end);
    assert(end.full_buffer_initializations==3&&end.background_preparations==1);
    assert(end.static_preparations<30); /* never rerasterize all text at 30Hz */
    qsort(elapsed,300,sizeof(elapsed[0]),cmp_u64);
    printf("HOST loading complete frame (update + region draw) n=300 p50_us=%.3f p95_us=%.3f max_us=%.3f (host only; native target <=3000 us remains unmeasured)\n",elapsed[149]/1000.0,elapsed[284]/1000.0,elapsed[299]/1000.0);
    printf("Loading JSON: ");x4_loading_screen_metrics_write(stdout);putchar('\n');
    x4_loading_screen_end();guards();
}
static void fade_benchmark(bool warm)
{
    uint64_t elapsed[150];unsigned sample=0;
    for(unsigned trial=0;trial<30;++trial){
        uint64_t start=1000000ULL+(uint64_t)trial*1000000ULL;
        assert(x4_loading_screen_begin("Single-picture fade benchmark",NULL,start));
        if(warm)for(unsigned i=0;i<3;++i)assert(x4_loading_screen_draw(pixels[i],start));
        /* Every trial receives precisely one native-image notification.
         * Only UI time advances afterwards, with no network/media calls. */
        uint64_t begin=now_ns();x4_loading_screen_first_picture(start);
        assert(x4_loading_screen_draw(pixels[0],start));elapsed[sample++]=now_ns()-begin;
        for(unsigned frame=1;frame<5;++frame){
            uint64_t t=start+(uint64_t)frame*33334ULL;
            assert(!x4_loading_screen_transition_ready(t));begin=now_ns();
            assert(x4_loading_screen_draw(pixels[frame%3],t));elapsed[sample++]=now_ns()-begin;
        }
        assert(!x4_loading_screen_transition_ready(start+159999));
        assert(x4_loading_screen_transition_ready(start+160000));
        X4LoadingView view;x4_loading_screen_snapshot(&view);
        assert(view.fade_frames==5&&view.transition_wait_us==160000);
        assert(view.fade_full_initializations==(warm?0u:3u));
        assert(view.fade_seed_copies==(warm?3u:0u));
        x4_loading_screen_end();guards();
    }
    assert(sample==150);qsort(elapsed,sample,sizeof(elapsed[0]),cmp_u64);
    printf("HOST loading %s fade complete frame (first-picture prepare + UI draw; one image only) n=%u p50_us=%.3f p95_us=%.3f max_us=%.3f (host only; native target <=3000 us remains unmeasured; cold includes full initial copy)\n",
        warm?"warm-buffer incremental":"cold-buffer",sample,elapsed[74]/1000.0,elapsed[142]/1000.0,elapsed[149]/1000.0);
}
int main(int argc,char **argv)
{
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    for(unsigned i=0;i<3;++i){owned[i]=calloc(PIXELS+2,4);assert(owned[i]);owned[i][0]=owned[i][PIXELS+1]=0xaabbccddu;pixels[i]=owned[i]+1;}
    X4CoverRaster r;neutral(&r);snapshots(argc>1?argv[1]:NULL,NULL,"original");snapshots(argc>1?argv[1]:NULL,&r,"neutral16x9");
    failure_and_cancel();benchmark();fade_benchmark(true);fade_benchmark(false);free(r.rgba);
    for(unsigned i=0;i<3;++i)free(owned[i]);
    x4_font_close();
    puts("PASS TAREA20b loading stages, bounded progress, delayed notice, 30Hz cache, triple-buffer dirty zones, single-picture160ms UI-clock fade/handoff, cancel/error fallback");return 0;
}
