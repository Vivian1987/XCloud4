/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the real-font/layout fixture and its coverage of previous screens. */
#define main tarea20_screens_main
#include "test_tarea20_screens.c"
#undef main

static X4TouchMenuCacheStats menu_stats(void)
{
    X4TouchMenuCacheStats stats;
    x4_touch_menu_cache_snapshot(&stats);
    return stats;
}

static void cache_states(void)
{
    X4TouchMenu m;
    x4_touch_menu_init(&m,X4_PUBLIC!=0);
    x4_touch_menu_cache_reset();
    x4_lang_set(X4_LANG_ES,"es-MX","MX");
    assert(!x4_touch_menu_prepare(&m,0));
    assert(menu_stats().regenerations==0);
    m.is_open=true;
    assert(x4_touch_menu_prepare(&m,0));
    assert(menu_stats().regenerations==1 && menu_stats().is_ready);
    for(unsigned i=0;i<100;++i)x4_touch_menu_draw(&m,0,pixels);
    assert(menu_stats().regenerations==1 && menu_stats().frames==100);
    m.selected_index=1;x4_touch_menu_prepare(&m,0);
    assert(menu_stats().regenerations==2);
    x4_touch_menu_prepare(&m,1);
    assert(menu_stats().regenerations==3);
    x4_touch_menu_prepare(&m,1);
    assert(menu_stats().regenerations==3);
    x4_lang_set(X4_LANG_EN,"en-US","US");x4_touch_menu_prepare(&m,1);
    assert(menu_stats().regenerations==4);
    m.is_open=false;assert(!x4_touch_menu_prepare(&m,1));
    m.is_open=true;x4_touch_menu_prepare(&m,1);
    assert(menu_stats().regenerations==5);
    m.is_public=!m.is_public;m.item_count=m.is_public?4:6;x4_touch_menu_prepare(&m,1);
    assert(menu_stats().regenerations==6);
    m.item_count=7;assert(!x4_touch_menu_prepare(&m,1));
    assert(!menu_stats().is_ready && menu_stats().regenerations==6);
    printf("PASS menu cache: open/focus/mute/language/edition invalidate; 100 stable frames = one render; cache_bytes=%zu\n",menu_stats().cache_bytes);
}

static void opaque_copy(void)
{
    X4TouchMenu m;x4_touch_menu_init(&m,X4_PUBLIC!=0);m.is_open=true;
    uint32_t *a=malloc(PIXELS*sizeof(*a)),*b=malloc(PIXELS*sizeof(*b));
    assert(a && b);
    for(size_t i=0;i<PIXELS;++i){a[i]=0x11223344u;b[i]=0x55667788u;}
    x4_touch_menu_prepare(&m,0);
    x4_touch_menu_draw(&m,0,a);x4_touch_menu_draw(&m,0,b);
    size_t copied=0,untouched=0;
    for(size_t i=0;i<PIXELS;++i) {
        if(a[i]==b[i])++copied;
        else {assert(a[i]==0x11223344u && b[i]==0x55667788u);++untouched;}
    }
    assert(copied>400000 && untouched>1000000);
    /* Every alignment/tail, source/destination guards, repeated a million times.
     * Instruction-level proof is the separate uninlined copy leaf: only source
     * is loaded and destination stored, with no blend or destination argument
     * passed to any other UI primitive. */
    uint32_t src[96],dst[96];
    for(unsigned i=0;i<96;++i)src[i]=0xa5000000u+i;
    for(unsigned trial=0;trial<1000000;++trial) {
        size_t count=trial%65,offset=1+(trial%4);
        for(unsigned i=0;i<96;++i)dst[i]=0xdeadbeefu;
        x4_touch_menu_copy_opaque_row(dst+offset,src+offset,count);
        for(unsigned i=0;i<96;++i)
            assert(dst[i]==(i>=offset && i<offset+count?src[i]:0xdeadbeefu));
    }
    free(a);free(b);
    puts("PASS opaque result independent of destination; outside unchanged; 1000000 guarded aligned/unaligned/tail copies");
}

static void menu_focus_captures(const char *dir)
{
    if(!dir)return;
    X4TouchMenu m;x4_touch_menu_init(&m,X4_PUBLIC!=0);m.is_open=true;
    for(unsigned language=0;language<2;++language) {
        x4_lang_set(language?X4_LANG_EN:X4_LANG_ES,language?"en-US":"es-MX",language?"US":"MX");
        const char *suffix=language?"en":"es";
        for(int selected=0;selected<m.item_count;++selected) {
            m.selected_index=selected;neutral_video();
            char name[64];snprintf(name,sizeof(name),"20b_menu_focus_%d",selected);
            begin_frame(name);x4_touch_menu_draw(&m,0,pixels);end_frame();capture(dir,name,suffix);
        }
        m.selected_index=1;neutral_video();begin_frame("menu_muted");
        x4_touch_menu_draw(&m,1,pixels);end_frame();capture(dir,"20b_menu_muted",suffix);
    }
}

static void menu_cached_benchmark(void)
{
    X4TouchMenu m;x4_touch_menu_init(&m,X4_PUBLIC!=0);m.is_open=true;
    uint64_t elapsed[1200];observe=false;x4_touch_menu_cache_reset();
    uint64_t start=now_ns();x4_touch_menu_prepare(&m,0);uint64_t prepare=now_ns()-start;
    for(unsigned i=0;i<1200;++i) {
        start=now_ns();x4_touch_menu_draw(&m,0,pixels);elapsed[i]=now_ns()-start;
    }
    assert(menu_stats().regenerations==1 && menu_stats().frames==1200);
    qsort(elapsed,1200,sizeof(elapsed[0]),cmp_u64);
    printf("HOST cached menu compose only (not PS4): n=1200 p50_ms=%.6f p95_ms=%.6f max_ms=%.6f prepare_ms=%.3f regenerations=%llu bytes_per_frame=%llu\n",
        elapsed[599]/1e6,elapsed[1139]/1e6,elapsed[1199]/1e6,prepare/1e6,
        (unsigned long long)menu_stats().regenerations,
        (unsigned long long)(menu_stats().copied_bytes/menu_stats().frames));
}

int main(int argc,char **argv)
{
    assert(tarea20_screens_main(1,argv)==0);
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    owned=malloc((PIXELS+2)*sizeof(uint32_t));assert(owned);
    owned[0]=owned[PIXELS+1]=0xdeadbeefu;pixels=owned+1;
    cache_states();opaque_copy();menu_focus_captures(argc>1?argv[1]:NULL);menu_cached_benchmark();
    assert(owned[0]==0xdeadbeefu && owned[PIXELS+1]==0xdeadbeefu);
    x4_touch_menu_cache_reset();x4_font_close();free(owned);
    puts("PASS TAREA20b menu");return 0;
}
