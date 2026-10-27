/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/ui/picture_settings.h"
#include "../src/ui/server_settings.h"
#include "../src/ui/catalog_gui.h"
#include "../src/ui/apple_ui.h"
#include "../src/ui/font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=1920, HEIGHT=1080, PIXELS=WIDTH*HEIGHT };
static bool reference_draw;
static unsigned settings_background_calls, keyboard_background_calls;

/* Rendering and fonts are production code. Assets are deliberately absent:
 * this adapter exercises the actual procedural fallback, with no network. */
int x4_catalog_assets_acquire(X4CatalogAssets *assets,const char *id,X4CatalogAssetView *out)
{ (void)assets;(void)id;(void)out;return 0; }
void x4_catalog_assets_release(X4CatalogAssets *assets,X4CatalogAssetView *view)
{ (void)assets;(void)view; }

void __real_x4_apple_glass_panel(uint32_t *,int,int,int,int,int,unsigned,unsigned);
void __wrap_x4_apple_glass_panel(uint32_t *p,int x,int y,int w,int h,int r,unsigned fill,unsigned border)
{
    if(!reference_draw&&x==100&&y==126&&w==1720&&h==836)++settings_background_calls;
    __real_x4_apple_glass_panel(p,x,y,w,h,r,fill,border);
}
void __real_x4_apple_fill_round_rect(uint32_t *,int,int,int,int,int,uint32_t);
void __wrap_x4_apple_fill_round_rect(uint32_t *p,int x,int y,int w,int h,int r,uint32_t color)
{
    if(!reference_draw&&x==0&&y==0&&w==WIDTH&&h==HEIGHT&&color==X4_RGBA(6,10,20,168))
        ++keyboard_background_calls;
    __real_x4_apple_fill_round_rect(p,x,y,w,h,r,color);
}

static void pattern(uint32_t *p)
{
    for(unsigned y=0;y<HEIGHT;++y)for(unsigned x=0;x<WIDTH;++x)
        p[(size_t)y*WIDTH+x]=X4_RGB((x*3+y)&255,(x+y*5)&255,(x*7+y*11)&255);
}
static void equal_pixels(const uint32_t *actual,const uint32_t *expected,const char *case_name)
{
    for(size_t i=0;i<PIXELS;++i)if(actual[i]!=expected[i]){
        fprintf(stderr,"%s mismatch x=%zu y=%zu actual=%08x expected=%08x\n",
                case_name,i%WIDTH,i/WIDTH,actual[i],expected[i]);
        abort();
    }
}
static void outside_equal(const uint32_t *actual,const uint32_t *before,int x,int y,int w,int h)
{
    for(int row=0;row<HEIGHT;++row)for(int col=0;col<WIDTH;++col)
        if(col<x||col>=x+w||row<y||row>=y+h)
            assert(actual[(size_t)row*WIDTH+col]==before[(size_t)row*WIDTH+col]);
}
static void capture(const char *dir,const char *name,const uint32_t *p)
{
    if(!dir)return;
    char path[1024];assert(snprintf(path,sizeof(path),"%s/%s.ppm",dir,name)<(int)sizeof(path));
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1920 1080\n255\n");
    for(size_t i=0;i<PIXELS;++i){
        unsigned char rgb[3]={(unsigned char)p[i],(unsigned char)(p[i]>>8),(unsigned char)(p[i]>>16)};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(!fclose(f));
}
static void regions(void)
{
    X4ServerRegion list[12]={0};
    for(unsigned i=0;i<12;++i){
        snprintf(list[i].name,sizeof(list[i].name),"Long_region_for_modal_scroll_%02u",i);
        snprintf(list[i].api_host,sizeof(list[i].api_host),"uksouth.gssv-play-prod.xboxlive.com");
        snprintf(list[i].host,sizeof(list[i].host),"%s",list[i].api_host);
        list[i].is_default=i==0;list[i].last_game_ms=45+(int)i*13;
    }
    assert(x4_server_settings_update_regions(list,12));
}
static void settings_reference(X4PictureSettingsUi *ui,uint32_t *ref,const uint32_t *base)
{
    memcpy(ref,base,PIXELS*sizeof(*ref));
    reference_draw=true;x4_picture_settings_ui_draw(ui,ref);reference_draw=false;
}
static void settings_case(const char *capture_dir,bool english,uint32_t *p,uint32_t *ref,
                          uint32_t *base,uint32_t *before)
{
    x4_lang_set(english?X4_LANG_EN:X4_LANG_ES,english?"en-US":"es-MX",english?"US":"MX");
    x4_picture_settings_ui_cache_invalidate();
    X4PictureSettingsCacheStats initial,stats;
    x4_picture_settings_ui_cache_snapshot(&initial);
    unsigned actual_backgrounds=settings_background_calls;
    X4PictureSettingsUi ui={0};
    assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_OPEN,true));
    pattern(base);memcpy(p,base,PIXELS*sizeof(*p));
    assert(!x4_picture_settings_ui_cache_ready(&ui));
    x4_picture_settings_ui_draw_cached(&ui,p);settings_reference(&ui,ref,base);
    equal_pixels(p,ref,"settings open");
    x4_picture_settings_ui_cache_snapshot(&stats);
    assert(stats.ready&&stats.last_draw_full&&stats.cache_bytes==PIXELS*sizeof(*p));
    assert(stats.background_preparations==initial.background_preparations+1);
    for(unsigned n=0;n<36;++n){
        if(n==3)assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_NEXT_TAB,true));
        if(n==19)assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_PREV_TAB,true));
        assert(x4_picture_settings_ui_handle(&ui,n%2?X4_PIC_SETTINGS_DOWN:X4_PIC_SETTINGS_UP,true));
        memcpy(before,p,PIXELS*sizeof(*p));
        x4_picture_settings_ui_draw_cached(&ui,p);
        outside_equal(p,before,100,126,1720,836);
        settings_reference(&ui,ref,base);equal_pixels(p,ref,"settings navigate/full equality");
        x4_picture_settings_ui_cache_snapshot(&stats);assert(!stats.last_draw_full);
    }
    assert(settings_background_calls==actual_backgrounds+1);
    x4_picture_settings_ui_cache_snapshot(&stats);
    assert(stats.background_preparations==initial.background_preparations+1);
    char name[80];snprintf(name,sizeof(name),"20c-options-cached-%s",english?"en":"es");
    capture(capture_dir,name,p);
    /* A different CPU canvas is seeded once from the frozen shell. */
    memset(before,0,PIXELS*sizeof(*before));x4_picture_settings_ui_draw_cached(&ui,before);
    settings_reference(&ui,ref,base);equal_pixels(before,ref,"settings new WB canvas");
    x4_picture_settings_ui_cache_snapshot(&stats);assert(stats.last_draw_full);
    /* Context identity and language cannot reuse another opening's background. */
    X4PictureSettingsUi other=ui;assert(!x4_picture_settings_ui_cache_ready(&other));
    x4_lang_set(english?X4_LANG_ES:X4_LANG_EN,english?"es-MX":"en-US",english?"MX":"US");
    assert(!x4_picture_settings_ui_cache_ready(&ui));
    memcpy(p,base,PIXELS*sizeof(*p));x4_picture_settings_ui_draw_cached(&ui,p);
    settings_reference(&ui,ref,base);equal_pixels(p,ref,"settings language");
    assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_CANCEL,true));
    x4_picture_settings_ui_cache_snapshot(&stats);assert(!stats.ready&&!stats.cache_bytes);
    assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_OPEN,true));
    memcpy(p,base,PIXELS*sizeof(*p));x4_picture_settings_ui_draw_cached(&ui,p);
    settings_reference(&ui,ref,base);equal_pixels(p,ref,"settings reopen");
    assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_CONFIRM,true));
    x4_picture_settings_ui_cache_snapshot(&stats);assert(!stats.ready&&!stats.cache_bytes);

    x4_lang_set(english?X4_LANG_EN:X4_LANG_ES,english?"en-US":"es-MX",english?"US":"MX");
    x4_picture_settings_open_server_first_time(&ui);
    memcpy(p,base,PIXELS*sizeof(*p));x4_picture_settings_ui_draw_cached(&ui,p);
    settings_reference(&ui,ref,base);equal_pixels(p,ref,"first-time open");
    unsigned first_backgrounds=settings_background_calls;
    for(unsigned n=0;n<20;++n){
        assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_DOWN,false));
        memcpy(before,p,PIXELS*sizeof(*p));x4_picture_settings_ui_draw_cached(&ui,p);
        outside_equal(p,before,100,126,1720,836);
        settings_reference(&ui,ref,base);equal_pixels(p,ref,"first-time navigation");
    }
    assert(settings_background_calls==first_backgrounds);
    snprintf(name,sizeof(name),"20c-first-time-cached-%s",english?"en":"es");capture(capture_dir,name,p);
    assert(x4_picture_settings_ui_handle(&ui,X4_PIC_SETTINGS_CANCEL,false));
    x4_picture_settings_ui_cache_snapshot(&stats);assert(!stats.ready&&!stats.cache_bytes);
    /* An unchanged old-style caller may independently clear before each draw. */
    ui.open=true;ui.first_time_mode=false;
    settings_reference(&ui,p,base);settings_reference(&ui,ref,base);
    equal_pixels(p,ref,"settings legacy callers");
    ui.open=false;x4_picture_settings_ui_cache_invalidate();
    printf("PASS settings %s: pixel-identical full/partial, one backdrop/open, tabs/focus/scroll, lifecycle, canaries\n",
           english?"en":"es");
}

static void keyboard_case(const char *capture_dir,bool english,uint32_t *p,uint32_t *ref,uint32_t *before)
{
    static X4CatalogSnapshot catalog;
    X4CatalogBrowser browser;X4RecentGames recent;
    X4CatalogGuiStatus status={1,0,0,"Host fixture: original procedural artwork"};
    memset(&catalog,0,sizeof(catalog));catalog.state=X4_CATALOG_READY;catalog.count=catalog.total=2;
    for(unsigned i=0;i<catalog.count;++i){
        snprintf(catalog.titles[i].id,sizeof(catalog.titles[i].id),"fixture-title-%u",i);
        snprintf(catalog.titles[i].product_id,sizeof(catalog.titles[i].product_id),"fixture-product-%u",i);
        snprintf(catalog.titles[i].name,sizeof(catalog.titles[i].name),"Original fixture %u",i);
        catalog.titles[i].entitled=1;
    }
    x4_catalog_browser_init(&browser);x4_recent_init(&recent);x4_catalog_browser_rebuild(&browser,&catalog);
    x4_lang_set(english?X4_LANG_EN:X4_LANG_ES,english?"en-US":"es-MX",english?"US":"MX");
    x4_catalog_gui_keyboard_cache_invalidate();
    X4CatalogKeyboardCacheStats initial,stats;x4_catalog_gui_keyboard_cache_snapshot(&initial);
    unsigned actual_backgrounds=keyboard_background_calls;
    browser.editing=true;
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
    assert(x4_catalog_gui_keyboard_cache_ready(&browser));
    x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(stats.last_draw_full&&stats.ready);
    /* The legacy renderer restores the complete identical frozen WB backdrop,
     * so it is the reference without repeating any blur/dim implementation. */
    reference_draw=true;x4_catalog_gui_draw(&catalog,&browser,&recent,NULL,&status,ref);reference_draw=false;
    equal_pixels(p,ref,"keyboard initial full equality");
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
    for(unsigned n=0;n<40;++n){
        x4_catalog_browser_keyboard_move(&browser,1,n%6==0?1:0);
        if(n%5==0)assert(x4_catalog_browser_query_append(&browser,(char)('A'+n%26)));
        memcpy(before,p,PIXELS*sizeof(*p));
        x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
        x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(!stats.last_draw_full);
        outside_equal(p,before,420,200,1080,680);
        reference_draw=true;x4_catalog_gui_draw(&catalog,&browser,&recent,NULL,&status,ref);reference_draw=false;
        equal_pixels(p,ref,"keyboard navigation/query full equality");
        /* Re-bind the persistent canvas after the reference's distinct canvas. */
        x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
    }
    assert(keyboard_background_calls==actual_backgrounds+1);
    x4_catalog_gui_keyboard_cache_snapshot(&stats);
    assert(stats.background_preparations==initial.background_preparations+1);
    char name[80];snprintf(name,sizeof(name),"20c-keyboard-cached-%s",english?"en":"es");capture(capture_dir,name,p);
    X4CatalogBrowser other=browser;assert(!x4_catalog_gui_keyboard_cache_ready(&other));
    x4_lang_set(english?X4_LANG_ES:X4_LANG_EN,english?"es-MX":"en-US",english?"MX":"US");
    assert(!x4_catalog_gui_keyboard_cache_ready(&browser));
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
    assert(keyboard_background_calls==actual_backgrounds+2);
    browser.editing=false;x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
    x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(!stats.ready&&!stats.cache_bytes);
    browser.editing=true;x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&status,p);
    assert(keyboard_background_calls==actual_backgrounds+3);
    reference_draw=true;x4_catalog_gui_draw(&catalog,&browser,&recent,NULL,&status,ref);reference_draw=false;
    equal_pixels(p,ref,"keyboard reopen");
    x4_catalog_gui_keyboard_cache_invalidate();
    x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(!stats.ready&&!stats.cache_bytes);
    printf("PASS keyboard %s: pixel-identical full/partial, one backdrop/open, cursor/query, lifecycle, canaries\n",
           english?"en":"es");
}

int main(int argc,char **argv)
{
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    x4_picture_settings_set_path("/tmp/x4-tarea20c-picture-test.bin");
    x4_server_settings_set_path("/tmp/x4-tarea20c-server-test.bin");
    remove("/tmp/x4-tarea20c-picture-test.bin");remove("/tmp/x4-tarea20c-server-test.bin");
    x4_picture_settings_init();x4_server_settings_init();regions();
    uint32_t *owned[4],*p[4];
    for(unsigned i=0;i<4;++i){owned[i]=malloc((PIXELS+2)*sizeof(**owned));assert(owned[i]);
        owned[i][0]=owned[i][PIXELS+1]=0xcafebabeu;p[i]=owned[i]+1;}
    for(unsigned english=0;english<2;++english){
        settings_case(argc>1?argv[1]:NULL,english!=0,p[0],p[1],p[2],p[3]);
        keyboard_case(argc>1?argv[1]:NULL,english!=0,p[0],p[1],p[3]);
        for(unsigned i=0;i<4;++i)assert(owned[i][0]==0xcafebabeu&&owned[i][PIXELS+1]==0xcafebabeu);
    }
    x4_picture_settings_ui_cache_invalidate();x4_catalog_gui_keyboard_cache_invalidate();
    for(unsigned i=0;i<4;++i)free(owned[i]);
    x4_font_close();remove("/tmp/x4-tarea20c-picture-test.bin");remove("/tmp/x4-tarea20c-server-test.bin");
    printf("PASS T20c WB modals public=%d: preparations settings=%u keyboard=%u; no retained cache\n",
           X4_PUBLIC,settings_background_calls,keyboard_background_calls);
    return 0;
}
