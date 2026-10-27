/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "../src/ui/ui_surface.h"
#include "ui_surface_test_trap.h"
#include "../src/ui/screen.h"
#include "../src/ui/apple_ui.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"
#include "../src/ui/picture_settings.h"
#include "../src/ui/server_settings.h"
#include "../src/ui/catalog_gui.h"
#include "../src/ui/loading_screen.h"
#include "../src/ui/stream_settings.h"
#include "../src/auth/http_client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

/* Only public-resource acquisition and HTTP side effects are fixtures.
 * All drawing, fonts, modal caching, damage tracking and presentation code
 * below are the actual production implementations. No game/network starts. */
X4Http *x4_http_open(int *error){if(error)*error=0;return NULL;}
void x4_http_close(X4Http *http){(void)http;}
int x4_http_releases_request(X4Http *http,const char *url,char *body,size_t capacity,
    size_t *length,int *status,const _Atomic int *cancel)
{(void)http;(void)url;(void)body;(void)capacity;(void)length;(void)status;(void)cancel;return -1;}

struct X4CatalogAssets { X4CatalogAssetView entry[6]; unsigned count; };
int x4_catalog_assets_acquire(X4CatalogAssets *assets,const char *id,X4CatalogAssetView *out)
{
    if(!assets||!id||!out)return 0;
    for(unsigned i=0;i<assets->count;++i)if(!strcmp(id,assets->entry[i].metadata.product_id)){
        *out=assets->entry[i];out->slot=i;return 1;
    }
    return 0;
}
void x4_catalog_assets_release(X4CatalogAssets *assets,X4CatalogAssetView *view)
{(void)assets;(void)view;}

enum { W=1920,H=1080,PIXELS=W*H };
static X4UiSurface *surface;
static X4UiTestTrap trap;
static uint32_t *canvas;
static unsigned frames,partial_copies;
static uint64_t copied_bytes;
static uint64_t draw_begin_ns;
static const char *output;
static X4CatalogSnapshot catalog;
static X4CatalogBrowser browser;
static X4RecentGames recent;
static struct X4CatalogAssets assets;
static X4CoverRaster neutral,poster[6];
static X4Controller controller;
static X4HomeDrawContext home_context;
static const X4CatalogGuiStatus gui_status={1,0,0,"Host fixture / no network"};

static uint64_t now_ns(void)
{
    struct timespec now;assert(!clock_gettime(CLOCK_MONOTONIC,&now));
    return (uint64_t)now.tv_sec*1000000000u+(uint64_t)now.tv_nsec;
}
#if !X4_UI_SURFACE_TEST_HOOKS
typedef struct {const char *name;unsigned count;uint64_t draw[128],copy[128];} Perf;
static Perf perf[64];
static unsigned perf_count;
static int compare_u64(const void *a,const void *b)
{uint64_t aa=*(const uint64_t *)a,bb=*(const uint64_t *)b;return aa<bb?-1:aa>bb;}
#endif
static void record_perf(const char *name,uint64_t draw,uint64_t copy)
{
#if !X4_UI_SURFACE_TEST_HOOKS
    unsigned key=0;while(key<perf_count&&strcmp(perf[key].name,name))++key;
    if(key==perf_count){assert(perf_count<64);perf[key].name=name;++perf_count;}
    Perf *entry=&perf[key];assert(entry->count<128);
    entry->draw[entry->count]=draw;entry->copy[entry->count]=copy;++entry->count;
#else
    (void)name;(void)draw;(void)copy;
#endif
}
static void report_perf(void)
{
#if !X4_UI_SURFACE_TEST_HOOKS
    for(unsigned i=0;i<perf_count;++i){
        Perf *entry=&perf[i];unsigned n=entry->count;
        qsort(entry->draw,n,sizeof(uint64_t),compare_u64);qsort(entry->copy,n,sizeof(uint64_t),compare_u64);
        printf("HOST UI real-NT benchmark (normal host RAM, not PS4 WC memory): public=%d screen=%s n=%u draw_p50_us=%.3f draw_p95_us=%.3f draw_max_us=%.3f copy_p50_us=%.3f copy_p95_us=%.3f copy_max_us=%.3f\n",
            X4_PUBLIC,entry->name,n,entry->draw[(n-1)/2]/1000.0,entry->draw[(n*95+99)/100-1]/1000.0,
            entry->draw[n-1]/1000.0,entry->copy[(n-1)/2]/1000.0,entry->copy[(n*95+99)/100-1]/1000.0,
            entry->copy[n-1]/1000.0);
    }
#endif
}
static uint32_t *begin_draw(void)
{
    draw_begin_ns=now_ns();
    uint32_t *p=x4_ui_surface_pixels(surface);
    assert(p==canvas&&p!=trap.destination[0]&&p!=trap.destination[1]);
    return p;
}
static void equal_canvas(unsigned buffer,const char *name)
{
    const uint32_t *presented=
#if X4_UI_SURFACE_TEST_HOOKS
        trap.mirror[buffer];
#else
        trap.destination[buffer]; /* Host benchmark memory is explicitly RW. */
#endif
    if(memcmp(presented,canvas,(size_t)PIXELS*4)){
        for(size_t i=0;i<PIXELS;++i)if(presented[i]!=canvas[i]){
            fprintf(stderr,"%s buffer=%u mismatch x=%zu y=%zu canvas=%08x presented=%08x\n",
                name,buffer,i%W,i/W,canvas[i],presented[i]);abort();
        }
    }
}
static void present_frame(const char *name,const X4UiRect *rects,size_t count)
{
    uint64_t drawn_at=now_ns();
    if(rects)x4_ui_surface_set_damage(surface,rects,count);
    /* Reuse one GPU destination repeatedly before revisiting the other.
     * This catches lost damage across more than two canvas generations. */
    unsigned buffer=frames%5?0:1;
    X4UiCopyStats stats;
    uint64_t copy_begin=now_ns();
    assert(!x4_ui_surface_publish(surface,trap.destination[buffer],buffer,&stats));
    record_perf(name,drawn_at-draw_begin_ns,now_ns()-copy_begin);
    equal_canvas(buffer,name);x4_ui_test_trap_assert_guards(&trap);
    if(stats.bytes&&stats.bytes<(uint64_t)PIXELS*4)++partial_copies;
    copied_bytes+=stats.bytes;++frames;
    printf("PASS UI trap %s destination=%u bytes=%llu tiles=%u\n",name,buffer,
        (unsigned long long)stats.bytes,stats.tiles);
}
static void capture(const char *name,bool english)
{
    if(!output)return;
    char path[1024];assert(snprintf(path,sizeof(path),"%s/20c-%s-%s.ppm",output,name,
        english?"en":"es")<(int)sizeof(path));
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n1920 1080\n255\n");
    unsigned char row[W*3];
    for(unsigned y=0;y<H;++y){
        for(unsigned x=0;x<W;++x){uint32_t p=canvas[(size_t)y*W+x];
            row[x*3]=(unsigned char)p;row[x*3+1]=(unsigned char)(p>>8);row[x*3+2]=(unsigned char)(p>>16);}
        assert(fwrite(row,3,W,file)==W);
    }
    assert(!fclose(file));
}
static void set_language(bool english)
{x4_lang_set(english?X4_LANG_EN:X4_LANG_ES,english?"en-US":"es-MX",english?"US":"MX");}
static void draw_home(bool english)
{
    X4Screen screen={.page=0,.signed_in=1,.selected=0};
    x4_screen_draw(&screen,&controller,begin_draw());present_frame("home",NULL,0);
    capture("home",english);
}
static void fixtures(void)
{
    neutral=(X4CoverRaster){W,H,malloc((size_t)PIXELS*4)};assert(neutral.rgba);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        neutral.rgba[(size_t)y*W+x]=X4_RGB(24+y/30,58+x/38,95+(x+y)/36);
    catalog.state=X4_CATALOG_READY;catalog.count=catalog.total=6;
    for(unsigned i=0;i<6;++i){
        poster[i]=(X4CoverRaster){400,600,malloc(400u*600u*4u)};assert(poster[i].rgba);
        for(unsigned y=0;y<600;++y)for(unsigned x=0;x<400;++x)
            poster[i].rgba[(size_t)y*400+x]=X4_RGB(30+i*13+x/9,38+i*11+y/13,72+i*15+(x+y)/18);
        X4CatalogTitle *title=&catalog.titles[i];
        snprintf(title->id,sizeof(title->id),"fixture-title-%u",i);
        snprintf(title->product_id,sizeof(title->product_id),"FIXTURE%08u",i);
        snprintf(title->name,sizeof(title->name),"Neutral adventure %u",i+1);title->entitled=1;
        X4CatalogAssetView *view=&assets.entry[i];
        snprintf(view->metadata.product_id,sizeof(view->metadata.product_id),"%s",title->product_id);
        snprintf(view->metadata.name,sizeof(view->metadata.name),"%s",title->name);
        snprintf(view->metadata.description,sizeof(view->metadata.description),
            "Original neutral host fixture. No game artwork, account or console data.");
        snprintf(view->metadata.publisher,sizeof(view->metadata.publisher),"XCloud4 test fixture");
        view->state=X4_ASSET_READY;view->generation=1;
        for(unsigned j=0;j<X4_COVER_SURFACES;++j)view->surface[j]=poster[i];
        ++assets.count;
    }
    x4_catalog_browser_init(&browser);x4_recent_init(&recent);
    x4_catalog_browser_rebuild(&browser,&catalog);
    char ids[3][96];for(unsigned i=0;i<3;++i)snprintf(ids[i],sizeof(ids[i]),"%s",catalog.titles[i].id);
    x4_recent_begin(&recent,1,1);assert(x4_recent_publish(&recent,1,1,ids,3,0,200,&catalog));
    x4_catalog_browser_rebuild_featured(&browser,&catalog,&recent);
    home_context.signed_in=1;home_context.featured_hero=&neutral;home_context.fortnite_cover=&poster[0];
    snprintf(home_context.gamertag,sizeof(home_context.gamertag),"Host fixture");
    x4_screen_set_home_context(&home_context);controller.data.connected=1;
    X4ServerRegion regions[4]={0};
    for(unsigned i=0;i<4;++i){
        snprintf(regions[i].name,sizeof(regions[i].name),"Fixture_region_%u",i);
        snprintf(regions[i].api_host,sizeof(regions[i].api_host),"uksouth.gssv-play-prod.xboxlive.com");
        regions[i].is_default=i==0;regions[i].last_game_ms=38+(int)i*21;
    }
    assert(x4_server_settings_update_regions(regions,4));
}
static void ordinary_screens(bool english)
{
    draw_home(english);
    home_context.featured_hero=NULL;home_context.signed_in=0;x4_screen_set_home_context(&home_context);
    X4Screen screen={.page=0,.selected=1};x4_screen_draw(&screen,&controller,begin_draw());
    present_frame("home-fallback-logged-out",NULL,0);
    home_context.featured_hero=&neutral;home_context.signed_in=1;x4_screen_set_home_context(&home_context);
    const int pages[]={2,7,1,3};
    const char *names[]={"about","support","controller","demo-no-media"};
    for(unsigned i=0;i<4;++i){
        screen.page=pages[i];x4_screen_draw(&screen,&controller,begin_draw());
#if !X4_PUBLIC
        if(screen.page==3){X4DemoVideo video={0};X4DemoAudio audio={0};x4_media_draw(&video,&audio,canvas);}
#endif
        present_frame(names[i],NULL,0);
    }
    X4AuthSnapshot auth={.state=X4_AUTH_IDLE};
    const enum X4AuthState states[]={X4_AUTH_IDLE,X4_AUTH_WAITING,X4_AUTH_CONNECTING,
        X4_AUTH_AUTHORIZED,X4_AUTH_ERROR,X4_AUTH_DENIED,X4_AUTH_EXPIRED};
    for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i){
        auth.state=states[i];auth.seconds_left=899;
        /* This is a deliberately invalid fixture code, not a console token. */
        snprintf(auth.user_code,sizeof(auth.user_code),"TSTX-1234");
        snprintf(auth.verification_uri,sizeof(auth.verification_uri),"https://www.microsoft.com/link");
        auth.error=auth.state==X4_AUTH_ERROR?(int)0x80431068u:0;
        x4_auth_draw(&auth,auth.state==X4_AUTH_CONNECTING,0,begin_draw());present_frame("account",NULL,0);
    }
    X4SessionSnapshot session={.state=X4_SESSION_WAITING,.elapsed_seconds=17};
    snprintf(session.title_name,sizeof(session.title_name),"Neutral session fixture");
    x4_session_draw(&session,1,0,begin_draw());present_frame("session-waiting",NULL,0);
    session.state=X4_SESSION_ERROR;session.error=(int)0x80431068u;session.http_status=202;
    x4_session_draw(&session,0,0,begin_draw());present_frame("session-error-card",NULL,0);
    X4LiveMediaSnapshot live={.video_error=-7413,.video_rejected_width=2560,.video_rejected_height=1440};
    x4_session_draw(&session,0,0,begin_draw());x4_live_status_draw(&live,-7413,canvas);
    present_frame("video-rejected-card",NULL,0);
    x4_session_draw(&session,0,0,begin_draw());x4_exit_error_draw(-1,canvas);present_frame("exit-error",NULL,0);
    screen.page=8;screen.selected=1;
    x4_fortnite_summary_draw(&screen,&catalog,0,begin_draw());present_frame("fortnite-ready",NULL,0);
    X4CatalogSnapshot *error=calloc(1,sizeof(*error));assert(error);error->state=X4_CATALOG_ERROR;
    error->error=X4_AUTH_E_XBOX;error->http_status=403;
    x4_fortnite_summary_draw(&screen,error,0,begin_draw());present_frame("fortnite-region-error",NULL,0);free(error);
    browser.editing=false;browser.active_tab=X4_CAT_TAB_DISCOVER;browser.apple_mode=true;
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    present_frame("catalog-discover",NULL,0);capture("catalog-discover",english);
    browser.active_tab=X4_CAT_TAB_CATALOG;
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    present_frame("catalog-grid",NULL,0);capture("catalog-grid",english);
    browser.active_tab=X4_CAT_TAB_RECENTS;
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    present_frame("catalog-recents",NULL,0);
    browser.active_tab=X4_CAT_TAB_CATALOG;
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,NULL,&gui_status,begin_draw());
    present_frame("catalog-no-assets",NULL,0);
#if !X4_PUBLIC
    X4StreamSettingsUi stream={0};assert(x4_stream_settings_ui_handle(&stream,X4_SETTINGS_OPEN,true));
    x4_stream_settings_ui_draw(&stream,begin_draw());present_frame("lab-stream-settings",NULL,0);
    assert(x4_stream_settings_ui_handle(&stream,X4_SETTINGS_CANCEL,true));
#endif
}
static void options(bool english)
{
    const X4UiRect rect={100,126,1720,836};
    X4PictureSettingsUi settings={0};X4PictureSettingsCacheStats initial,stats;
    x4_picture_settings_ui_cache_invalidate();x4_picture_settings_ui_cache_snapshot(&initial);
    draw_home(english);assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_OPEN,true));
    x4_picture_settings_ui_draw_cached(&settings,begin_draw());x4_picture_settings_ui_cache_snapshot(&stats);
    assert(stats.last_draw_full&&stats.ready);present_frame("options-open",NULL,0);
    for(unsigned i=0;i<12;++i){
        if(i==3)assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_NEXT_TAB,true));
        assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_DOWN,true));
        x4_picture_settings_ui_draw_cached(&settings,begin_draw());x4_picture_settings_ui_cache_snapshot(&stats);
        assert(!stats.last_draw_full);present_frame(i<3?"options-image-nav":"options-server-nav",&rect,1);
    }
    assert(stats.background_preparations==initial.background_preparations+1);
    set_language(!english);assert(!x4_picture_settings_ui_cache_ready(&settings));
    X4Screen screen={.page=0};x4_screen_draw(&screen,&controller,begin_draw());
    x4_picture_settings_ui_draw_cached(&settings,canvas);x4_picture_settings_ui_cache_snapshot(&stats);
    assert(stats.last_draw_full);present_frame("options-language-new-backdrop",NULL,0);
    assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_CANCEL,true));set_language(english);
    draw_home(english);assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_OPEN,true));
    x4_picture_settings_ui_draw_cached(&settings,begin_draw());present_frame("options-reopen",NULL,0);
    assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_CANCEL,true));
    draw_home(english);x4_picture_settings_open_server_first_time(&settings);
    x4_picture_settings_ui_draw_cached(&settings,begin_draw());present_frame("first-time-server-open",NULL,0);
    for(unsigned i=0;i<8;++i){
        assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_DOWN,false));
        x4_picture_settings_ui_draw_cached(&settings,begin_draw());x4_picture_settings_ui_cache_snapshot(&stats);
        assert(!stats.last_draw_full);present_frame("first-time-server-nav",&rect,1);
    }
    assert(x4_picture_settings_ui_handle(&settings,X4_PIC_SETTINGS_CANCEL,false));
}
static void keyboard(bool english)
{
    const X4UiRect rect={420,200,1080,680};
    X4CatalogKeyboardCacheStats initial,stats;x4_catalog_gui_keyboard_cache_invalidate();
    x4_catalog_gui_keyboard_cache_snapshot(&initial);browser.editing=true;
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(stats.last_draw_full);present_frame("keyboard-open",NULL,0);
    for(unsigned i=0;i<16;++i){
        x4_catalog_browser_keyboard_move(&browser,1,i%6==0?1:0);
        if(i%4==0)assert(x4_catalog_browser_query_append(&browser,(char)('A'+i)));
        x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
        x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(!stats.last_draw_full);
        present_frame("keyboard-nav-query",&rect,1);
    }
    assert(stats.background_preparations==initial.background_preparations+1);
    set_language(!english);assert(!x4_catalog_gui_keyboard_cache_ready(&browser));
    x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    x4_catalog_gui_keyboard_cache_snapshot(&stats);assert(stats.last_draw_full);present_frame("keyboard-language",NULL,0);
    browser.editing=false;x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    present_frame("keyboard-close",NULL,0);set_language(english);
    browser.editing=true;x4_catalog_gui_draw_cached(&catalog,&browser,&recent,&assets,&gui_status,begin_draw());
    present_frame("keyboard-reopen",NULL,0);browser.editing=false;x4_catalog_gui_keyboard_cache_invalidate();
    x4_catalog_browser_query_clear(&browser);x4_catalog_browser_rebuild(&browser,&catalog);
}
static void loading(bool with_image)
{
    static const struct {enum X4SessionState state;int rtc;uint64_t time;unsigned percent;} stages[]={
        {X4_SESSION_STARTING,0,1000000,5},{X4_SESSION_WAITING,0,2000000,20},
        {X4_SESSION_READY,0,2500000,35},{X4_SESSION_AUTHORIZING,0,3000000,42},
        {X4_SESSION_AUTHORIZED,0,3500000,49},{X4_SESSION_NEGOTIATING,0,4000000,55},
        {X4_SESSION_CONNECTING,0,4500000,64},{X4_SESSION_STREAMING,1,5000000,70},
        {X4_SESSION_STREAMING,1,10000000,74},{X4_SESSION_STREAMING,1,21000000,83},
        {X4_SESSION_STREAMING,1,35000000,95}};
    const X4UiRect dynamic[]={{800,300,320,180},{510,640,900,54},{510,700,900,40},
        {510,740,900,48},{510,852,900,50}};
    assert(x4_loading_screen_begin("Original neutral fixture",with_image?&neutral:NULL,1000000));
    for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);++i){
        X4SessionSnapshot session={.state=stages[i].state,.rtc_connected=stages[i].rtc};
        x4_loading_screen_update(&session,stages[i].time);
        assert(x4_loading_screen_draw(begin_draw(),stages[i].time));
        X4LoadingView view;x4_loading_screen_snapshot(&view);assert(view.percent==stages[i].percent);
        present_frame(with_image?"loading-neutral-image":"loading-own-background",i?dynamic:NULL,i?5:0);
        if(i==1){
            /* Real queue status fields, independently of the connection's
             * five-second first-picture notice. Also erase an ETA that
             * disappears again without repainting the full backdrop. */
            for(unsigned queued=0;queued<3;++queued){
                X4SessionSnapshot waiting={.state=X4_SESSION_WAITING,.waiting_for_resources=1,
                    .queue_elapsed_seconds=61+queued,.queue_eta_available=queued==1,
                    .queue_eta_seconds=queued==1?119u:0u};
                uint64_t at=2100000+(uint64_t)queued*100000;
                x4_loading_screen_update(&waiting,at);assert(x4_loading_screen_draw(begin_draw(),at));
                x4_loading_screen_snapshot(&view);
                assert(view.waiting_for_resources&&view.queue_elapsed_seconds==61+queued&&
                    view.queue_eta_available==(queued==1)&&view.queue_eta_seconds==(queued==1?119u:0u)&&
                    !view.delayed_notice&&view.percent==20);
                present_frame(queued==1?"loading-queue-with-eta":"loading-queue-no-eta",dynamic,5);
            }
        }
        if(i==2)assert(!view.waiting_for_resources&&!view.queue_eta_available);
    }
    /* Several new rocket frames with independent GPU reuse and narrow damage. */
    for(unsigned i=0;i<20;++i){
        uint64_t at=35033334+(uint64_t)i*33334;
        X4SessionSnapshot session={.state=X4_SESSION_STREAMING,.rtc_connected=1};
        x4_loading_screen_update(&session,at);assert(x4_loading_screen_draw(begin_draw(),at));
        present_frame("loading-rocket-30Hz",dynamic,5);
    }
    x4_loading_screen_first_picture(61000000);
    const uint64_t times[]={61000000,61080000,61160000};
    for(unsigned i=0;i<3;++i){
        assert(x4_loading_screen_draw(begin_draw(),times[i]));present_frame("loading-100-percent-and-fade",NULL,0);
    }
    assert(x4_loading_screen_transition_ready(61160000));x4_loading_screen_end();
    assert(x4_loading_screen_begin("Failure fixture",NULL,70000000));
    X4SessionSnapshot error={.state=X4_SESSION_ERROR,.error=-1};
    x4_loading_screen_update(&error,70001000);assert(!x4_loading_screen_draw(begin_draw(),70001000));
    x4_session_draw(&error,0,0,canvas);present_frame("loading-error-fallback",NULL,0);x4_loading_screen_end();
}
int main(int argc,char **argv)
{
    output=argc>1?argv[1]:NULL;
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    x4_picture_settings_set_path("/nonexistent-x4-tarea20c/picture.bin");
    x4_server_settings_set_path("/nonexistent-x4-tarea20c/server.bin");
    x4_picture_settings_init();x4_server_settings_init();fixtures();
    surface=x4_ui_surface_create();assert(surface);canvas=x4_ui_surface_pixels(surface);
    x4_ui_test_trap_open(&trap);
#if X4_UI_SURFACE_TEST_HOOKS
    x4_ui_test_trap_positive_read(&trap);
#else
    /* The separate benchmark executable uses the unmodified production NT
     * leaf. Only this mode grants destination access for result comparison.
     * It never substitutes or reads a protected destination. */
    for(unsigned b=0;b<X4_UI_SCANOUTS;++b)
        assert(!mprotect(trap.destination[b],(size_t)PIXELS*4,PROT_READ|PROT_WRITE));
#endif
    for(unsigned english=0;english<2;++english){
        set_language(english!=0);ordinary_screens(english!=0);options(english!=0);keyboard(english!=0);
        loading(false);loading(true);
        x4_ui_surface_set_damage(surface,NULL,0);
        for(unsigned b=0;b<2;++b){X4UiCopyStats stats;assert(!x4_ui_surface_publish(surface,trap.destination[b],b,&stats));equal_canvas(b,"final-backlog");}
    }
    assert(partial_copies>50);
#if X4_UI_SURFACE_TEST_HOOKS
    assert(trap.stores&&trap.pixels_stored);
#endif
    printf("PASS all UI real renderers -> WB -> %s: public=%d frames=%u partial_copies=%u bytes=%llu; pixel equality across delayed double-buffer reuse\n",
        X4_UI_SURFACE_TEST_HOOKS?"PROT_NONE store trap":"actual NT stores to normal host RAM",
        X4_PUBLIC,frames,partial_copies,(unsigned long long)copied_bytes);
    report_perf();
    x4_picture_settings_ui_cache_invalidate();x4_catalog_gui_keyboard_cache_invalidate();
    x4_ui_test_trap_close(&trap);x4_ui_surface_destroy(surface);x4_screen_set_home_context(NULL);
    free(neutral.rgba);for(unsigned i=0;i<6;++i)free(poster[i].rgba);x4_font_close();return 0;
}
