/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/ui/screen.h"
#include "../src/ui/apple_ui.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"
#include "../src/ui/server_settings.h"
#include "../src/auth/http_client.h"
#include "../third_party/qrcodegen/qrcodegen.h"

#define WIDTH 1920
#define HEIGHT 1080
#define PIXELS ((size_t)WIDTH*HEIGHT)

X4Http *x4_http_open(int *err){if(err)*err=0;return NULL;}
void x4_http_close(X4Http *h){(void)h;}
int x4_http_releases_request(X4Http *h,const char *url,char *body,size_t cap,
    size_t *len,int *status,const _Atomic int *cancel)
{(void)h;(void)url;(void)body;(void)cap;(void)len;(void)status;(void)cancel;return -1;}

/* The real font renderer is compiled under these two names. The wrappers
 * observe the actual measured labels requested by screen.c/apple_ui.c. */
void x4_text_px_impl(uint32_t *,int,int,int,int,const char *,uint32_t);
void x4_text_ellipsis_px_impl(uint32_t *,int,int,int,int,const char *,int,uint32_t);
typedef struct{int x,y,w,h;char label[96];} TextBox;
static TextBox boxes[512];
static unsigned box_count;
static bool observe;
static const char *state_name;
static uint32_t *owned,*pixels;

static void observe_text(int x,int y,int size,int weight,const char *text)
{
    if(!observe||!text||!text[0])return;
    int w=x4_text_width_px(size,weight,text);
    assert(w>=0);
    if(x<0||y<0||x+w>WIDTH||y+size>HEIGHT){
        fprintf(stderr,"Out of bounds %s (%d,%d %dx%d): %.80s\n",state_name,x,y,w,size,text);abort();
    }
    assert(box_count<sizeof(boxes)/sizeof(boxes[0]));
    TextBox *b=&boxes[box_count++];*b=(TextBox){x,y,w,size,{0}};
    snprintf(b->label,sizeof(b->label),"%s",text);
}
void x4_text_px(uint32_t *p,int x,int y,int size,int weight,const char *s,uint32_t c)
{observe_text(x,y,size,weight,s);x4_text_px_impl(p,x,y,size,weight,s,c);}
void x4_text_ellipsis_px(uint32_t *p,int x,int y,int size,int weight,const char *s,int width,uint32_t c)
{
    char fit[1024];x4_text_fit_utf8(fit,sizeof(fit),size,weight,s,width);
    observe_text(x,y,size,weight,fit);
    x4_text_ellipsis_px_impl(p,x,y,size,weight,s,width,c);
}
static void begin_frame(const char *name)
{state_name=name;box_count=0;observe=true;}
static void end_frame(void)
{
    assert(owned[0]==0xdeadbeefu && owned[PIXELS+1]==0xdeadbeefu);
    for(unsigned i=0;i<box_count;++i)for(unsigned j=i+1;j<box_count;++j){
        const TextBox *a=&boxes[i],*b=&boxes[j];
        if(a->w&&b->w&&a->x<b->x+b->w&&b->x<a->x+a->w&&a->y<b->y+b->h&&b->y<a->y+a->h){
            fprintf(stderr,"Text overlap %s: %.80s (%d,%d %dx%d) / %.80s (%d,%d %dx%d)\n",state_name,a->label,a->x,a->y,a->w,a->h,b->label,b->x,b->y,b->w,b->h);abort();
        }
    }
    observe=false;
}
static void capture(const char *dir,const char *state,const char *lang)
{
    if(!dir)return;
    char path[1024];snprintf(path,sizeof(path),"%s/%s_%s.ppm",dir,state,lang);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    for(size_t i=0;i<PIXELS;++i){unsigned char rgb[3]={(unsigned char)pixels[i],(unsigned char)(pixels[i]>>8),(unsigned char)(pixels[i]>>16)};assert(fwrite(rgb,1,3,f)==3);}
    assert(fclose(f)==0);
}
static void neutral_video(void)
{
    for(int y=0;y<HEIGHT;++y)for(int x=0;x<WIDTH;++x)
        pixels[(size_t)y*WIDTH+x]=X4_RGB(40+(x/100)%4*18,46+(y/100)%4*16,65+((x+y)/180)%3*22);
}
static uint64_t now_ns(void)
{struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;}
static int cmp_u64(const void *a,const void *b)
{uint64_t aa=*(const uint64_t *)a,bb=*(const uint64_t *)b;return aa<bb?-1:aa>bb;}
static void menu_benchmark(void)
{
    X4TouchMenu menu;x4_touch_menu_init(&menu,X4_PUBLIC!=0);menu.is_open=true;
    uint64_t elapsed[120];observe=false;
    for(unsigned i=0;i<120;++i){neutral_video();menu.selected_index=(int)(i%(unsigned)menu.item_count);uint64_t start=now_ns();x4_touch_menu_draw(&menu,i%2,pixels);elapsed[i]=now_ns()-start;}
    qsort(elapsed,120,sizeof(elapsed[0]),cmp_u64);
    unsigned over_budget=0;for(unsigned i=0;i<120;++i)if(elapsed[i]>16666667ULL)++over_budget;
    printf("HOST menu draw only (no decoder/PS4): n=120 p50_ms=%.3f p95_ms=%.3f max_ms=%.3f frames_over_16.667ms=%u simulated_60Hz_budget_only\n",elapsed[59]/1e6,elapsed[113]/1e6,elapsed[119]/1e6,over_budget);
}
static void touch_behavior(void)
{
    X4TouchMenu m;x4_touch_menu_init(&m,X4_PUBLIC!=0);
    assert(!x4_touch_menu_feed_touch(&m,true,2,1));
    assert(x4_touch_menu_feed_touch(&m,true,2,1000001));
    assert(m.is_open && m.selected_index==0);
    assert(x4_touch_menu_update(&m,ORBIS_PAD_BUTTON_DOWN,0,1000002)==X4_TOUCH_MENU_ACTION_NONE);
    assert(m.selected_index==1);
    assert(x4_touch_menu_update(&m,ORBIS_PAD_BUTTON_CROSS,ORBIS_PAD_BUTTON_CROSS,1000003)==X4_TOUCH_MENU_ACTION_TOGGLE_MUTE);
    assert(!m.is_open);
    assert(x4_touch_menu_filter_buttons(&m,ORBIS_PAD_BUTTON_CROSS)==0);
    assert(x4_touch_menu_filter_buttons(&m,0)==0);
    m.is_open=true;m.last_activity_us=2000000;
    assert(x4_touch_menu_update(&m,0,0,17000000)==X4_TOUCH_MENU_ACTION_NONE && !m.is_open);
    X4Screen s={.page=8,.selected=0};
    x4_screen_update(&s,ORBIS_PAD_BUTTON_DOWN,0);assert(s.selected==1);
    x4_screen_update(&s,ORBIS_PAD_BUTTON_DOWN,0);assert(s.selected==2);
    x4_screen_update(&s,ORBIS_PAD_BUTTON_UP,0);assert(s.selected==1);
    x4_screen_update(&s,ORBIS_PAD_BUTTON_CIRCLE,0);assert(s.page==0);
    printf("PASS real Fortnite one-step navigation and touch hold/actions/15s timeout\n");
}
static void donation_qr_pixels(void)
{
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX],temp[qrcodegen_BUFFER_LEN_MAX];
    assert(!strcmp(x4_support_qr_url(),"https://ko-fi.com/asapedgr"));
    assert(qrcodegen_encodeText(x4_support_qr_url(),temp,qr,qrcodegen_Ecc_MEDIUM,qrcodegen_VERSION_MIN,qrcodegen_VERSION_MAX,qrcodegen_Mask_AUTO,false));
    X4Controller controller={0};X4Screen screen={.page=7};x4_screen_draw(&screen,&controller,pixels);
    int n=qrcodegen_getSize(qr),scale=330/(n+8),box=(n+8)*scale;
    int x=1260+(330-box)/2,y=386+(330-box)/2;
    for(int row=0;row<n+8;++row)for(int col=0;col<n+8;++col){
        bool dark=row>=4&&row<n+4&&col>=4&&col<n+4&&qrcodegen_getModule(qr,col-4,row-4);
        assert(pixels[(size_t)(y+row*scale+scale/2)*WIDTH+x+col*scale+scale/2]==(dark?0xff000000u:0xffffffffu));
    }
    char payload[512];
    assert(x4_auth_qr_payload("https://www.microsoft.com/link","TSTX-1234",payload,sizeof(payload)));
    assert(!strcmp(payload,"https://www.microsoft.com/link?otc=TSTX-1234"));
    assert(!x4_auth_qr_payload("https://invalid.example","TSTX-1234",payload,sizeof(payload)));
    printf("PASS Ko-fi exact URL/real rendered QR modules/quiet zone and dummy auth payload\n");
}
static X4CoverRasters load_icon(void)
{
    X4CoverRasters out={0};FILE *f=fopen("assets/icon0.png","rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<2000000);rewind(f);
    unsigned char *bytes=malloc((size_t)n);assert(bytes);assert(fread(bytes,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));
    assert(!x4_cover_decode(bytes,(size_t)n,&out));free(bytes);return out;
}
static void render_states(const char *dir)
{
    X4CoverRasters icon=load_icon();
    X4CoverRaster poster={.width=400,.height=600,.rgba=malloc(400*600*sizeof(uint32_t))};assert(poster.rgba);
    for(unsigned y=0;y<600;++y)for(unsigned x=0;x<400;++x)poster.rgba[(size_t)y*400+x]=X4_RGB(28+x/6,40+y/10,90+(x+y)/15);
    /* Full-size neutral 16:9 pixel fixture; not generated game artwork. */
    X4CoverRaster panorama={.width=1920,.height=1080,.rgba=malloc(PIXELS*sizeof(uint32_t))};
    assert(panorama.rgba);
    for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x) {
        uint32_t color=X4_RGB(42+x/24,62+y/12,100+(x+y)/48);
        if(x<12||x>=1908||y<12||y>=1068)color=X4_RGB(245,220,115);
        if((x>=950&&x<970)||(y>=530&&y<550))color=X4_RGB(80,190,190);
        panorama.rgba[(size_t)y*1920+x]=color;
    }
    X4HomeDrawContext ctx={.signed_in=1,.fortnite_cover=&poster,.app_icon=&icon.surface[X4_COVER_RECENT]};
    snprintf(ctx.gamertag,sizeof(ctx.gamertag),"LongPlayerNameForLayout1234567");x4_screen_set_home_context(&ctx);
    X4Controller controller={0};controller.data.connected=1;
    X4ServerRegion region={.is_default=true};snprintf(region.name,sizeof(region.name),"EastUS");snprintf(region.api_host,sizeof(region.api_host),"example.gssv-play-prod.xboxlive.com");
    for(int language=0;language<2;++language){
        x4_lang_set(language==0?X4_LANG_ES:X4_LANG_EN,language==0?"es-MX":"en-US",language==0?"MX":"US");
        const char *suffix=language==0?"es":"en";
        snprintf(region.name,sizeof(region.name),"%s",language==0?"EastUS":"WestEurope");
        assert(x4_server_settings_update_regions(&region,1));
        X4AuthSnapshot auth={.state=X4_AUTH_WAITING,.seconds_left=899};
        /* Deliberately fake fixture. Never copies a real console access code. */
        snprintf(auth.user_code,sizeof(auth.user_code),"TSTX-1234");snprintf(auth.verification_uri,sizeof(auth.verification_uri),"https://www.microsoft.com/link");
        begin_frame("auth_waiting_dummy");x4_auth_draw(&auth,1,0,pixels);end_frame();capture(dir,"20_auth_waiting_dummy",suffix);
        auth.state=X4_AUTH_IDLE;
        begin_frame("auth_idle");x4_auth_draw(&auth,0,0,pixels);end_frame();capture(dir,"20_auth_idle",suffix);
        auth.state=X4_AUTH_AUTHORIZED;
        begin_frame("auth_authorized_long_gamertag");x4_auth_draw(&auth,0,0,pixels);end_frame();capture(dir,"20_auth_authorized",suffix);
        auth.state=X4_AUTH_ERROR;auth.error=(int)0x80431068u;auth.http_status=0;
        snprintf(auth.stage,sizeof(auth.stage),"Microsoft connection timed out while waiting for a response from the service.");
        begin_frame("auth_error_timeout");x4_auth_draw(&auth,0,0,pixels);end_frame();capture(dir,"20_auth_error_timeout",suffix);
        auth.error=0;auth.state=X4_AUTH_CONNECTING;
        begin_frame("auth_connecting");x4_auth_draw(&auth,1,0,pixels);end_frame();capture(dir,"20_auth_connecting",suffix);
        X4CatalogSnapshot *cat=calloc(1,sizeof(*cat));assert(cat);cat->state=X4_CATALOG_READY;
        X4Screen screen={.page=8,.selected=1};
        begin_frame("fortnite_unmeasured");x4_fortnite_summary_draw(&screen,cat,0,pixels);end_frame();capture(dir,"20_fortnite_unmeasured",suffix);
        assert(x4_server_settings_record_game_rtt(region.name,42));
        begin_frame("fortnite_measured");x4_fortnite_summary_draw(&screen,cat,0,pixels);end_frame();capture(dir,"20_fortnite_measured",suffix);
        cat->state=X4_CATALOG_LOADING;
        begin_frame("fortnite_loading");x4_fortnite_summary_draw(&screen,cat,1,pixels);end_frame();capture(dir,"20_fortnite_loading",suffix);
        cat->state=X4_CATALOG_ERROR;cat->error=X4_AUTH_E_FORTNITE_NOT_FOUND;
        begin_frame("fortnite_unavailable");x4_fortnite_summary_draw(&screen,cat,0,pixels);end_frame();capture(dir,"20_fortnite_unavailable",suffix);
        cat->error=X4_AUTH_E_XBOX;cat->http_status=403;snprintf(cat->offering,sizeof(cat->offering),"xgpuwebf2p");
        snprintf(cat->stage,sizeof(cat->stage),"Xbox Cloud Gaming rechazo la cuenta");
        begin_frame("fortnite_country");x4_fortnite_summary_draw(&screen,cat,0,pixels);end_frame();capture(dir,"20_fortnite_country",suffix);
        cat->xerr=2148916233u;
        begin_frame("fortnite_xerr");x4_fortnite_summary_draw(&screen,cat,0,pixels);end_frame();capture(dir,"20_fortnite_xerr",suffix);
        cat->xerr=0;cat->error=(int)0x80431068u;cat->http_status=0;
        begin_frame("fortnite_timeout");x4_fortnite_summary_draw(&screen,cat,0,pixels);end_frame();capture(dir,"20_fortnite_timeout",suffix);
        free(cat);
        screen.page=2;
        begin_frame("about");x4_screen_draw(&screen,&controller,pixels);end_frame();capture(dir,"20_about",suffix);
        screen.page=7;
        begin_frame("support");x4_screen_draw(&screen,&controller,pixels);end_frame();capture(dir,"20_support",suffix);
        X4TouchMenu menu;x4_touch_menu_init(&menu,X4_PUBLIC!=0);menu.is_open=true;menu.selected_index=menu.item_count-1;
        neutral_video();begin_frame("touch_menu");x4_touch_menu_draw(&menu,0,pixels);end_frame();capture(dir,"20_touch_menu",suffix);
        /* The overlay must not touch video outside its left-hand bounding box. */
        uint32_t *before=malloc(PIXELS*sizeof(uint32_t));assert(before);neutral_video();memcpy(before,pixels,PIXELS*sizeof(uint32_t));x4_touch_menu_draw(&menu,0,pixels);
        for(int y=0;y<HEIGHT;++y) {
            for(int x=0;x<WIDTH;++x) {
                if(x<66||x>826||y<116||y>944)assert(pixels[(size_t)y*WIDTH+x]==before[(size_t)y*WIDTH+x]);
            }
        }
        free(before);
        X4SessionSnapshot session={.state=X4_SESSION_WAITING,.elapsed_seconds=23};
        snprintf(session.title_name,sizeof(session.title_name),"The extraordinarily long neutral fixture title: a complete collection of adventures for layout verification - no game art");
        snprintf(session.stage,sizeof(session.stage),"Xbox está preparando el juego");
        begin_frame("session_preparing_long_title");x4_session_draw(&session,1,0,pixels);end_frame();capture(dir,"20_session_loading",suffix);
        session.state=X4_SESSION_ERROR;session.error=(int)0x80431068u;session.http_status=202;
        begin_frame("session_http_timeout");x4_session_draw(&session,0,0,pixels);end_frame();capture(dir,"20_session_http_timeout",suffix);
        X4LiveMediaSnapshot media={.video_error=-7433};
        observe=false;x4_session_draw(&session,0,0,pixels);begin_frame("video_no_image");x4_live_status_draw(&media,-7433,pixels);end_frame();capture(dir,"20_video_no_image",suffix);
        media.video_error=-7413;media.video_rejected_width=2560;media.video_rejected_height=1440;
        observe=false;x4_session_draw(&session,0,0,pixels);begin_frame("video_size_rejected");x4_live_status_draw(&media,-7413,pixels);end_frame();capture(dir,"20_video_size_rejected",suffix);
        media.video_error=-7450;media.video_rejected_width=media.video_rejected_height=0;
        observe=false;x4_session_draw(&session,0,0,pixels);begin_frame("video_native_error");x4_live_status_draw(&media,-7450,pixels);end_frame();capture(dir,"20_video_native_error",suffix);
        /* G2: exercise the complete existing home composition, with a real
         * 1920x1080 source raster offered to BOTH cards. The tall Fortnite
         * card must still respect the 10% crop cap (contain if incompatible). */
        ctx.featured_hero=&panorama;ctx.fortnite_cover=&panorama;ctx.signed_in=0;
        x4_screen_set_home_context(&ctx);
        screen=(X4Screen){.page=0,.selected=1};
        begin_frame("home_16x9_logged_out");x4_screen_draw(&screen,&controller,pixels);end_frame();capture(dir,"20_home_16x9_logged_out",suffix);
        ctx.signed_in=1;screen.signed_in=1;screen.selected=0;x4_screen_set_home_context(&ctx);
        begin_frame("home_16x9_logged_in");x4_screen_draw(&screen,&controller,pixels);end_frame();capture(dir,"20_home_16x9_logged_in",suffix);
        ctx.featured_hero=NULL;ctx.fortnite_cover=&poster;x4_screen_set_home_context(&ctx);
        printf("PASS phase-2 actual text bounds/non-overlap + guarded framebuffer and full home 1920x1080 fixture: %s %s\n",X4_PUBLIC?"public":"lab",suffix);
    }
    x4_cover_rasters_free(&icon);free(poster.rgba);free(panorama.rgba);x4_screen_set_home_context(NULL);
}
int main(int argc,char **argv)
{
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    owned=malloc((PIXELS+2)*sizeof(uint32_t));assert(owned);owned[0]=owned[PIXELS+1]=0xdeadbeefu;pixels=owned+1;
    x4_server_settings_set_path("/nonexistent-tarea20-host/settings.bin");
    touch_behavior();render_states(argc>1?argv[1]:NULL);donation_qr_pixels();menu_benchmark();
    x4_font_close();free(owned);puts("PASS TAREA20 screens");return 0;
}
