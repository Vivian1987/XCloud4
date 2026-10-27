/* SPDX-License-Identifier: GPL-3.0-only */
#include "loading_screen.h"
#include "apple_ui.h"
#include "font.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#ifdef X4_OPENORBIS
#include <orbis/libkernel.h>
#else
#include <time.h>
#endif

#define WIDTH 1920
#define HEIGHT 1080
#define PIXELS ((size_t)WIDTH*HEIGHT)
#define QW 240
#define QH 135
#define DX 510
#define DY 230
#define DW 900
#define DH 680
#define AX 800
#define AY 300
#define AW 320
#define AH 180
#define FRAME_US 33333ULL
#define FADE_US 160000ULL
#define HIST_BINS 256
#define HIST_STEP 25ULL

static struct {
    uint32_t *backdrop,*frame,*base_zone;
    uint32_t quarter[QW*QH],blur[QW*QH];
    uint32_t *destinations[4];
    unsigned destination_generation[4],generation;
    unsigned destination_static_generation[4],static_generation;
    unsigned destination_fade_rank[4];
    bool destination_fade_seeded[4];
    uint64_t begin_us,wait_us,first_us,animation_tick;
    uint64_t render_hist[HIST_BINS],render_total_us;
    uint64_t frame_hist[HIST_BINS],frame_total_us,pending_static_us;
    uint64_t fade_hist[HIST_BINS],fade_total_us;
    unsigned visual_percent,visual_seconds,visual_queue_seconds,visual_eta_seconds;
    bool visual_queue,visual_eta;
    enum X4LoadingStage visual_stage;
    enum X4Lang language;
    char title[128];
    bool dirty,logged,static_ready,wait_started;
    X4LoadingView view;
} loading;
/* At most30x17 blocks share one Bayer rank. Lists are built once, before
 * playback. Incremental fade touches only the new ranks in each UI buffer. */
enum { FADE_RANKS=64, FADE_BLOCK=8, FADE_BLOCKS_PER_RANK=510 };
static uint32_t fade_offsets[FADE_RANKS][FADE_BLOCKS_PER_RANK];
static unsigned fade_counts[FADE_RANKS];
static bool fade_map_ready;
static void prepare_fade_map(void)
{
    if(fade_map_ready)return;
    static const unsigned char ranks[64]={0,48,12,60,3,51,15,63,32,16,44,28,35,19,47,31,8,56,4,52,11,59,7,55,40,24,36,20,43,27,39,23,2,50,14,62,1,49,13,61,34,18,46,30,33,17,45,29,10,58,6,54,9,57,5,53,42,26,38,22,41,25,37,21};
    for(unsigned by=0;by<HEIGHT/FADE_BLOCK;++by)for(unsigned bx=0;bx<WIDTH/FADE_BLOCK;++bx){
        unsigned rank=ranks[(by%8)*8+bx%8];
        fade_offsets[rank][fade_counts[rank]++]=(by*FADE_BLOCK)*WIDTH+bx*FADE_BLOCK;
    }
    fade_map_ready=true;
}

static uint64_t clock_us(void)
{
#ifdef X4_OPENORBIS
    return sceKernelGetProcessTime();
#else
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000ULL+(uint64_t)t.tv_nsec/1000ULL;
#endif
}
static const char *tr(const char *es,const char *en)
{return loading.language==X4_LANG_EN?en:es;}
static void record_render(uint64_t elapsed,bool fade)
{
    unsigned b=(unsigned)(elapsed/HIST_STEP);
    if(b>=HIST_BINS)b=HIST_BINS-1;
    ++loading.render_hist[b];++loading.view.render_calls;
    loading.render_total_us+=elapsed;
    if(elapsed>loading.view.render_max_us)loading.view.render_max_us=elapsed;
    /* Charge all state-dependent preparation to the next displayed frame,
     * including the first-picture completion. Never hide its cost inside
     * update merely because update precedes draw in the main loop. */
    uint64_t total=elapsed+loading.pending_static_us;loading.pending_static_us=0;
    unsigned fb=(unsigned)(total/HIST_STEP);if(fb>=HIST_BINS)fb=HIST_BINS-1;
    ++loading.frame_hist[fb];loading.frame_total_us+=total;
    if(total>loading.view.frame_total_max_us)loading.view.frame_total_max_us=total;
    if(fade){
        ++loading.fade_hist[fb];++loading.view.fade_frames;loading.fade_total_us+=total;
        if(total>loading.view.fade_max_us)loading.view.fade_max_us=total;
    }
}
static uint64_t hist_percentile(const uint64_t hist[HIST_BINS],uint64_t count,uint64_t max,unsigned pct)
{
    if(!count)return 0;
    uint64_t need=(count*pct+99)/100,seen=0;
    for(unsigned i=0;i<HIST_BINS;++i){
        seen+=hist[i];
        if(seen>=need)return i==HIST_BINS-1?max:(i+1)*HIST_STEP;
    }
    return max;
}
static uint64_t percentile(unsigned pct)
{return hist_percentile(loading.render_hist,loading.view.render_calls,loading.view.render_max_us,pct);}
static uint64_t frame_percentile(unsigned pct)
{return hist_percentile(loading.frame_hist,loading.view.render_calls,loading.view.frame_total_max_us,pct);}
static uint64_t fade_percentile(unsigned pct)
{return hist_percentile(loading.fade_hist,loading.view.fade_frames,loading.view.fade_max_us,pct);}
static void log_metrics(void)
{
    if(loading.logged||!loading.view.background_preparations)return;
    printf("XCloud4 UI loading: render_n=%llu p50_us=%llu p95_us=%llu max_us=%llu frame_total_p50_us=%llu frame_total_p95_us=%llu frame_total_max_us=%llu background_us=%llu static_n=%llu static_max_us=%llu full_copies=%llu region_copies=%llu fade_frames=%llu fade_p50_us=%llu fade_p95_us=%llu fade_max_us=%llu first_native_us=%llu transition_ready_us=%llu transition_wait_us=%llu fade_seed_copies=%llu fade_cold_full_copies=%llu fade_blocks=%llu (UI-only measurements; frame_total includes state preparation; transition clock does not need another AU)\n",
        (unsigned long long)loading.view.render_calls,(unsigned long long)percentile(50),(unsigned long long)percentile(95),
        (unsigned long long)loading.view.render_max_us,(unsigned long long)frame_percentile(50),(unsigned long long)frame_percentile(95),(unsigned long long)loading.view.frame_total_max_us,(unsigned long long)loading.view.background_us,
        (unsigned long long)loading.view.static_preparations,(unsigned long long)loading.view.static_max_us,
        (unsigned long long)loading.view.full_buffer_initializations,(unsigned long long)loading.view.region_copies,(unsigned long long)loading.view.fade_frames,
        (unsigned long long)fade_percentile(50),(unsigned long long)fade_percentile(95),(unsigned long long)loading.view.fade_max_us,
        (unsigned long long)loading.view.first_picture_us,(unsigned long long)loading.view.transition_ready_us,(unsigned long long)loading.view.transition_wait_us,
        (unsigned long long)loading.view.fade_seed_copies,(unsigned long long)loading.view.fade_full_initializations,(unsigned long long)loading.view.fade_blocks_written);
    loading.logged=true;
}
static void copy_region(uint32_t *dst,const uint32_t *src,int x,int y,int w,int h)
{
    for(int row=y;row<y+h;++row)
        memcpy(dst+(size_t)row*WIDTH+x,src+(size_t)row*WIDTH+x,(size_t)w*4);
}
static uint32_t dim_rgb(unsigned r,unsigned g,unsigned b,int y)
{
    /* Dark translucent appearance is baked into normal memory once; output
     * is fully opaque. Keep the game recognizable without competing text. */
    unsigned factor=(unsigned)(y<68?77:61);
    return X4_RGB(r*factor/255,g*factor/255,b*factor/255);
}
static bool valid_game(const X4CoverRaster *r)
{
    if(!r||!r->rgba||!r->width||!r->height||r->width>4096||r->height>4096)return false;
    uint64_t a=(uint64_t)r->width*9,b=(uint64_t)r->height*16;
    uint64_t delta=a>b?a-b:b-a;
    return delta*100<=b; /* at most one percent aspect-ratio deviation */
}
static void prepare_backdrop(const X4CoverRaster *game)
{
    loading.view.has_game_background=valid_game(game);
    if(loading.view.has_game_background){
        for(unsigned y=0;y<QH;++y)for(unsigned x=0;x<QW;++x){
            unsigned sx=x*game->width/QW,sy=y*game->height/QH;
            loading.quarter[(size_t)y*QW+x]=game->rgba[(size_t)sy*game->width+sx];
        }
        /* One bounded separable box blur on the 1/8-scale image. */
        for(int y=0;y<QH;++y)for(int x=0;x<QW;++x){
            unsigned rr=0,gg=0,bb=0;
            for(int k=-6;k<=6;++k){int xx=x+k;if(xx<0)xx=0;if(xx>=QW)xx=QW-1;
                uint32_t c=loading.quarter[(size_t)y*QW+xx];rr+=c&255;gg+=(c>>8)&255;bb+=(c>>16)&255;}
            loading.blur[(size_t)y*QW+x]=X4_RGB(rr/13,gg/13,bb/13);
        }
        for(int y=0;y<QH;++y)for(int x=0;x<QW;++x){
            unsigned rr=0,gg=0,bb=0;
            for(int k=-6;k<=6;++k){int yy=y+k;if(yy<0)yy=0;if(yy>=QH)yy=QH-1;
                uint32_t c=loading.blur[(size_t)yy*QW+x];rr+=c&255;gg+=(c>>8)&255;bb+=(c>>16)&255;}
            loading.quarter[(size_t)y*QW+x]=dim_rgb(rr/13,gg/13,bb/13,y);
        }
        for(unsigned y=0;y<HEIGHT;++y)for(unsigned x=0;x<WIDTH;++x)
            loading.backdrop[(size_t)y*WIDTH+x]=loading.quarter[(size_t)(y/8)*QW+x/8];
    }else x4_apple_bg_draw(loading.backdrop);
    /* All backing text is cached; no public game logo or Xbox artwork. */
    x4_apple_draw_brand_logo(loading.backdrop,76,48);
    x4_text_px(loading.backdrop,76,1000,19,X4_FONT_REGULAR,
        tr("Preparando una conexión segura con tu partida","Preparing a secure connection to your game"),X4_RGB(155,168,190));
    ++loading.view.background_preparations;
}
static void centered_text(uint32_t *p,int y,int size,int weight,const char *text,uint32_t color)
{
    char fit[256];x4_text_fit_utf8(fit,sizeof(fit),size,weight,text,820);
    int width=x4_text_width_px(size,weight,fit);
    x4_text_px(p,(WIDTH-width)/2,y,size,weight,fit,color);
}
static const char *stage_text(char out[160])
{
    if(loading.view.waiting_for_resources){
        snprintf(out,160,tr("Los servidores de Xbox están llenos. Estás en la fila… %u:%02u",
            "Xbox servers are busy. You're in line… %u:%02u"),
            loading.view.queue_elapsed_seconds/60,loading.view.queue_elapsed_seconds%60);
        return out;
    }
    switch(loading.view.stage){
    case X4_LOADING_REQUEST:return tr("Solicitando sesión…","Requesting session…");
    case X4_LOADING_RESOURCES:return tr("Xbox está preparando tu partida…","Xbox is preparing your session…");
    case X4_LOADING_AUTHORIZE:return tr("Autorizando la conexión…","Authorizing the connection…");
    case X4_LOADING_CONNECT:return tr("Conectando…","Connecting…");
    case X4_LOADING_CONNECTED:return tr("Conexión establecida…","Connection established…");
    case X4_LOADING_COMPLETE:return tr("Todo listo","Ready to play");
    default:
        snprintf(out,160,tr("Xbox está preparando el juego… %u s","Xbox is preparing the game… %u s"),loading.view.waiting_seconds);
        return out;
    }
}
static int rocket_edge(int ax,int ay,int bx,int by,int px,int py)
{return (px-ax)*(by-ay)-(py-ay)*(bx-ax);}
static int rocket_round(float value)
{return (int)(value+(value>=0.0f?0.5f:-0.5f));}
static void rocket_triangle(int ax,int ay,int bx,int by,int cx,int cy,uint32_t color)
{
    int left=ax<bx?ax:bx;if(cx<left)left=cx;
    int right=ax>bx?ax:bx;if(cx>right)right=cx;
    int top=ay<by?ay:by;if(cy<top)top=cy;
    int bottom=ay>by?ay:by;if(cy>bottom)bottom=cy;
    /* The motif lives entirely in the known dirty region. Clip these own
     * geometric primitives too, rather than allowing a later stale edge. */
    if(left<AX)left=AX;
    if(right>=AX+AW)right=AX+AW-1;
    if(top<AY)top=AY;
    if(bottom>=AY+AH)bottom=AY+AH-1;
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
        int e0=rocket_edge(ax,ay,bx,by,x,y),e1=rocket_edge(bx,by,cx,cy,x,y),e2=rocket_edge(cx,cy,ax,ay,x,y);
        if((e0>=0&&e1>=0&&e2>=0)||(e0<=0&&e1<=0&&e2<=0))loading.frame[(size_t)y*WIDTH+x]=color;
    }
}
static void animation(unsigned phase)
{
    /* Original XCloud4 artwork: a small launch vehicle leaving its own
     * white cloud. All shapes, motion and flame are procedural; there is
     * no image, video, Microsoft or Xbox animation asset in this motif. */
    const int x=WIDTH/2;
    unsigned cycle=phase%54u; /* 54 frames at 30 Hz = 1.8 seconds. */
    float a=(float)cycle*6.28318530718f/54.0f;
    int bob=rocket_round(sinf(a)*10.0f);
    int cloud_x=x+rocket_round(sinf(a+0.55f)*5.0f);
    const uint32_t body=X4_RGB(232,237,245),fin=X4_RGB(154,168,190);
    const uint32_t green=X4_RGB(61,220,151),white=X4_RGB(255,255,255);

    /* Thin falling streaks stay behind the vehicle and inside 320x180. */
    static const int streak_x[6]={814,842,870,1050,1078,1104};
    for(unsigned i=0;i<6;++i){
        int y=AY+6+(int)((cycle*156u/54u+i*26u)%156u);
        int length=6+(int)(i%3u)*3;
        if(y+length>AY+AH-2)length=AY+AH-2-y;
        if(length>0)x4_apple_fill_round_rect(loading.frame,streak_x[i],y,2,length,1,
            (i==1||i==4)?X4_RGBA(61,220,151,150):X4_RGBA(179,194,216,105));
    }

    /* Cloud circles overlap a rounded base; its only motion is a small
     * horizontal drift, leaving ample space for the rocket's 10px bob. */
    x4_apple_fill_round_rect(loading.frame,cloud_x-106,447,212,27,13,X4_RGB(232,240,249));
    x4_apple_fill_round_rect(loading.frame,cloud_x-92,432,54,40,20,white);
    x4_apple_fill_round_rect(loading.frame,cloud_x-54,419,65,54,27,white);
    x4_apple_fill_round_rect(loading.frame,cloud_x-4,426,63,48,24,white);
    x4_apple_fill_round_rect(loading.frame,cloud_x+44,438,53,35,17,white);
    x4_apple_fill_round_rect(loading.frame,cloud_x-76,446,151,28,14,white);

    int exhaust=409+bob;
    int flicker=(int)(cycle%3u);
    rocket_triangle(x-12,exhaust,x+12,exhaust,x+(flicker-1)*2,exhaust+26+flicker*4,X4_RGB(255,143,51));
    rocket_triangle(x-7,exhaust,x+7,exhaust,x,exhaust+17+(int)(cycle%2u)*4,X4_RGB(255,221,96));
    rocket_triangle(x-15,377+bob,x-41,420+bob,x-12,410+bob,fin);
    rocket_triangle(x+15,377+bob,x+41,420+bob,x+12,410+bob,fin);
    x4_apple_fill_round_rect(loading.frame,x-18,330+bob,36,80,17,body);
    rocket_triangle(x,310+bob,x-18,345+bob,x+18,345+bob,body);
    x4_apple_fill_round_rect(loading.frame,x-12,340+bob,5,49,2,X4_RGBA(255,255,255,185));
    x4_apple_fill_round_rect(loading.frame,x-13,351+bob,26,26,13,green);
    x4_apple_fill_round_rect(loading.frame,x-9,355+bob,18,18,9,X4_RGB(13,27,45));
    x4_apple_fill_round_rect(loading.frame,x-6,357+bob,5,5,2,X4_RGBA(255,255,255,215));
    x4_apple_fill_round_rect(loading.frame,x-4,391+bob,8,14,3,X4_RGB(199,211,228));
}
static void prepare_fixed(void)
{
    centered_text(loading.backdrop,510,42,X4_FONT_BOLD,"XCloud4/",X4_RGB(242,247,255));
    centered_text(loading.backdrop,590,36,X4_FONT_SEMIBOLD,loading.title,X4_RGB(243,247,255));
    x4_apple_fill_round_rect(loading.backdrop,620,710,680,18,9,X4_RGB(29,40,57));
    const char *cancel=tr("Cancelar","Cancel");
    int hint_width=40+x4_text_width_px(23,X4_FONT_REGULAR,cancel);
    x4_apple_draw_footer_hint(loading.backdrop,(WIDTH-hint_width)/2,808,X4_PAD_CIRCLE,cancel);
}
static void restore_dynamic(int y,int height)
{
    for(int row=y;row<y+height;++row)
        memcpy(loading.backdrop+(size_t)row*WIDTH+DX,
            loading.base_zone+(size_t)(row-DY)*DW,(size_t)DW*4);
}
static void copy_dynamic_regions(uint32_t *dst,const uint32_t *src)
{
    copy_region(dst,src,DX,640,DW,66);copy_region(dst,src,DX,700,DW,40);
    copy_region(dst,src,DX,740,DW,48);copy_region(dst,src,DX,852,DW,50);
}
static void prepare_static(void)
{
    if(!loading.view.active||loading.view.stage==X4_LOADING_ERROR)return;
    if(loading.static_ready&&loading.visual_percent==loading.view.percent&&loading.visual_seconds==loading.view.waiting_seconds&&loading.visual_stage==loading.view.stage&&loading.visual_queue==loading.view.waiting_for_resources&&loading.visual_queue_seconds==loading.view.queue_elapsed_seconds&&loading.visual_eta==loading.view.queue_eta_available&&loading.visual_eta_seconds==loading.view.queue_eta_seconds)return;
    uint64_t before=clock_us();
    /* The logo, title and Cancel footer are baked once. Only changing text
     * and progress rectangles are restored; all blending stays in RAM. */
    restore_dynamic(640,66);restore_dynamic(700,40);
    restore_dynamic(740,48);restore_dynamic(852,50);
    char label[192];
    if(loading.view.waiting_for_resources){
        centered_text(loading.backdrop,641,25,X4_FONT_REGULAR,
            tr("Los servidores de Xbox están llenos. Estás en la fila…","Xbox servers are busy. You're in line…"),X4_RGB(205,215,231));
        if(loading.view.queue_eta_available)
            snprintf(label,sizeof(label),tr("Tiempo en fila: %u:%02u · Estimación: %u:%02u (orientativa)",
                "Time in line: %u:%02u · Estimate: %u:%02u (approximate)"),
                loading.view.queue_elapsed_seconds/60,loading.view.queue_elapsed_seconds%60,
                loading.view.queue_eta_seconds/60,loading.view.queue_eta_seconds%60);
        else snprintf(label,sizeof(label),tr("Tiempo en fila: %u:%02u","Time in line: %u:%02u"),
            loading.view.queue_elapsed_seconds/60,loading.view.queue_elapsed_seconds%60);
        centered_text(loading.backdrop,674,21,X4_FONT_REGULAR,label,X4_RGB(161,177,198));
    }else centered_text(loading.backdrop,651,25,X4_FONT_REGULAR,stage_text(label),X4_RGB(205,215,231));
    int filled=(int)(680u*loading.view.percent/100u);
    if(filled>0)x4_apple_fill_round_rect(loading.backdrop,620,710,filled,18,filled<18?filled/2:9,X4_RGB(37,214,139));
    char pct[16];snprintf(pct,sizeof(pct),"%u %%",loading.view.percent);
    centered_text(loading.backdrop,746,24,X4_FONT_SEMIBOLD,pct,X4_RGB(228,236,247));
    if(loading.view.waiting_for_resources)centered_text(loading.backdrop,866,19,X4_FONT_REGULAR,
        tr("Puedes esperar o cancelar con (círculo).","You can wait or cancel with (circle)."),X4_RGB(161,177,198));
    else if(loading.view.delayed_notice)centered_text(loading.backdrop,866,19,X4_FONT_REGULAR,
        tr("Xbox está preparando el juego. Puedes cancelar cuando quieras.","Xbox is preparing the game. You can cancel at any time."),X4_RGB(161,177,198));
    copy_dynamic_regions(loading.frame,loading.backdrop);++loading.static_generation;
    loading.visual_percent=loading.view.percent;loading.visual_seconds=loading.view.waiting_seconds;
    loading.visual_stage=loading.view.stage;loading.visual_queue=loading.view.waiting_for_resources;
    loading.visual_queue_seconds=loading.view.queue_elapsed_seconds;
    loading.visual_eta=loading.view.queue_eta_available;loading.visual_eta_seconds=loading.view.queue_eta_seconds;
    loading.static_ready=true;loading.dirty=true;
    uint64_t elapsed=clock_us()-before;++loading.view.static_preparations;
    loading.pending_static_us+=elapsed;
    if(elapsed>loading.view.static_max_us)loading.view.static_max_us=elapsed;
}
static void render_frame(uint64_t now)
{
    unsigned phase=(unsigned)((now>=loading.begin_us?now-loading.begin_us:0)/FRAME_US);
    copy_region(loading.frame,loading.backdrop,AX,AY,AW,AH);
    animation(phase);
    ++loading.generation;
    loading.animation_tick=now/FRAME_US;loading.dirty=false;
}
bool x4_loading_screen_begin(const char *title,const X4CoverRaster *game,uint64_t now)
{
    x4_loading_screen_end();memset(&loading,0,sizeof(loading));
    loading.backdrop=malloc(PIXELS*4);loading.frame=malloc(PIXELS*4);
    loading.base_zone=malloc((size_t)DW*DH*4);
    if(!loading.backdrop||!loading.frame||!loading.base_zone){free(loading.backdrop);free(loading.frame);free(loading.base_zone);loading.backdrop=loading.frame=loading.base_zone=NULL;return false;}
    loading.language=x4_lang_current();loading.begin_us=now;
    loading.view.active=true;loading.view.stage=X4_LOADING_REQUEST;loading.view.percent=5;
    snprintf(loading.title,sizeof(loading.title),"%s",title&&title[0]?title:tr("Tu próxima partida","Your next game"));
    uint64_t before=clock_us();prepare_fade_map();prepare_backdrop(game);prepare_fixed();memcpy(loading.frame,loading.backdrop,PIXELS*4);
    for(int y=0;y<DH;++y)memcpy(loading.base_zone+(size_t)y*DW,loading.backdrop+(size_t)(DY+y)*WIDTH+DX,(size_t)DW*4);
    loading.view.background_us=clock_us()-before;prepare_static();loading.dirty=true;
    return true;
}
void x4_loading_screen_end(void)
{
    log_metrics();
    free(loading.backdrop);free(loading.frame);free(loading.base_zone);loading.backdrop=loading.frame=loading.base_zone=NULL;
    loading.view.active=false;
}
bool x4_loading_screen_active(void){return loading.view.active;}
void x4_loading_screen_update(const X4SessionSnapshot *s,uint64_t now)
{
    if(!loading.view.active||!s||loading.view.first_picture)return;
    if(s->state==X4_SESSION_ERROR||s->state==X4_SESSION_STOPPING||s->state==X4_SESSION_CLOSED||s->state==X4_SESSION_CANCELLED||s->error||s->http_status>=400||s->cleanup_failed){loading.view.stage=X4_LOADING_ERROR;return;}
    loading.view.waiting_for_resources=s->waiting_for_resources!=0;
    loading.view.queue_elapsed_seconds=s->queue_elapsed_seconds;
    loading.view.queue_eta_available=s->queue_eta_available!=0;
    loading.view.queue_eta_seconds=s->queue_eta_seconds;
    if(loading.view.waiting_for_resources)loading.view.delayed_notice=false;
    enum X4LoadingStage stage=loading.view.stage;unsigned percent=loading.view.percent;
    switch(s->state){
    case X4_SESSION_WAITING:stage=X4_LOADING_RESOURCES;percent=20;break;
    case X4_SESSION_READY:stage=X4_LOADING_RESOURCES;percent=35;break;
    case X4_SESSION_AUTHORIZING:stage=X4_LOADING_AUTHORIZE;percent=42;break;
    case X4_SESSION_AUTHORIZED:stage=X4_LOADING_AUTHORIZE;percent=49;break;
    case X4_SESSION_NEGOTIATING:stage=X4_LOADING_CONNECT;percent=55;break;
    case X4_SESSION_CONNECTING:stage=X4_LOADING_CONNECT;percent=64;break;
    case X4_SESSION_STREAMING:stage=X4_LOADING_CONNECTED;percent=70;break;
    default:break;
    }
    if(s->rtc_connected){stage=X4_LOADING_CONNECTED;percent=70;}
    if(stage==X4_LOADING_CONNECTED||stage==X4_LOADING_FIRST_PICTURE){
        if(!loading.wait_started){loading.wait_us=now;loading.wait_started=true;}
        uint64_t age=now>=loading.wait_us?now-loading.wait_us:0;
        if(age>=250000ULL)stage=X4_LOADING_FIRST_PICTURE;
        if(age>30000000ULL)age=30000000ULL;
        percent=70+(unsigned)(age*25/30000000ULL);
        loading.view.waiting_seconds=(unsigned)((now>=loading.wait_us?now-loading.wait_us:0)/1000000ULL);
        loading.view.delayed_notice=loading.view.waiting_seconds>=5;
    }
    if(stage>loading.view.stage)loading.view.stage=stage;
    if(percent>loading.view.percent)loading.view.percent=percent;
    if(loading.view.percent>95)loading.view.percent=95;
    prepare_static();
}
void x4_loading_screen_first_picture(uint64_t now)
{
    if(!loading.view.active||loading.view.first_picture||loading.view.stage==X4_LOADING_ERROR)return;
    loading.view.first_picture=true;loading.first_us=now;
    loading.view.first_picture_us=now;
    loading.view.percent=100;loading.view.stage=X4_LOADING_COMPLETE;
    loading.view.delayed_notice=false;prepare_static();loading.dirty=true;
}
bool x4_loading_screen_transition_ready(uint64_t now)
{
    if(!loading.view.active||loading.view.stage==X4_LOADING_ERROR)return true;
    if(!loading.view.first_picture||now<loading.first_us||now-loading.first_us<FADE_US)return false;
    if(!loading.view.transition_ready_us){
        loading.view.transition_ready_us=now;
        loading.view.transition_wait_us=now-loading.first_us;
    }
    return true;
}
static void blacken_new_ranks(uint32_t *dst,unsigned from,unsigned to)
{
#if defined(__SSE2__)
    const __m128i black=_mm_set1_epi32((int)UINT32_C(0xff000000));
#endif
    for(unsigned rank=from;rank<to;++rank)for(unsigned block=0;block<fade_counts[rank];++block){
        size_t offset=fade_offsets[rank][block];
        for(unsigned row=0;row<FADE_BLOCK;++row){
            uint32_t *line=dst+offset+(size_t)row*WIDTH;
#if defined(__SSE2__)
            _mm_storeu_si128((__m128i *)(void *)line,black);
            _mm_storeu_si128((__m128i *)(void *)(line+4),black);
#else
            for(unsigned pixel=0;pixel<FADE_BLOCK;++pixel)line[pixel]=UINT32_C(0xff000000);
#endif
        }
        ++loading.view.fade_blocks_written;
    }
}
bool x4_loading_screen_draw(uint32_t *pixels,uint64_t now)
{
    if(!pixels||!loading.view.active||loading.view.stage==X4_LOADING_ERROR)return false;
    uint64_t before=clock_us();
    bool changed=loading.dirty||now/FRAME_US!=loading.animation_tick||loading.visual_percent!=loading.view.percent||
        loading.visual_seconds!=loading.view.waiting_seconds||loading.visual_stage!=loading.view.stage;
    if(loading.view.first_picture){if(loading.dirty)render_frame(loading.first_us);}
    else if(changed)render_frame(now);
    unsigned slot=0;while(slot<4&&loading.destinations[slot]&&loading.destinations[slot]!=pixels)++slot;
    if(slot==4)slot=3;
    bool fresh=loading.destinations[slot]!=pixels;
    if(loading.view.first_picture){
        uint64_t age=now>=loading.first_us?now-loading.first_us:0;
        unsigned rank=age>=FADE_US?FADE_RANKS:(unsigned)(age*FADE_RANKS/FADE_US);
        if(!fresh&&loading.destination_fade_seeded[slot]&&loading.destination_fade_rank[slot]==rank)return false;
        if(fresh){
            loading.destinations[slot]=pixels;++loading.view.full_buffer_initializations;
            memcpy(pixels,loading.frame,PIXELS*4);++loading.view.fade_full_initializations;
            loading.destination_fade_rank[slot]=0;loading.destination_fade_seeded[slot]=true;
        }else if(!loading.destination_fade_seeded[slot]){
            /* Outside the center this buffer still contains its once-copied
             * backdrop. Seed only the100% center, exactly once per buffer. */
            copy_region(pixels,loading.frame,DX,DY,DW,DH);++loading.view.fade_seed_copies;
            loading.destination_fade_rank[slot]=0;loading.destination_fade_seeded[slot]=true;
        }
        blacken_new_ranks(pixels,loading.destination_fade_rank[slot],rank);
        loading.destination_fade_rank[slot]=rank;
    }else{
        bool animate=fresh||loading.destination_generation[slot]!=loading.generation;
        bool text=fresh||loading.destination_static_generation[slot]!=loading.static_generation;
        if(!animate&&!text)return false;
        if(fresh){loading.destinations[slot]=pixels;++loading.view.full_buffer_initializations;memcpy(pixels,loading.frame,PIXELS*4);}
        else{
            if(text)copy_dynamic_regions(pixels,loading.frame);
            if(animate)copy_region(pixels,loading.frame,AX,AY,AW,AH);
            ++loading.view.region_copies;
        }
    }
    loading.destination_generation[slot]=loading.generation;
    loading.destination_static_generation[slot]=loading.static_generation;
    record_render(clock_us()-before,loading.view.first_picture);return true;
}
void x4_loading_screen_snapshot(X4LoadingView *out){if(out)*out=loading.view;}
void x4_loading_screen_metrics_write(FILE *f)
{
    if(!f)return;
    fprintf(f,"{\"render_n\":%llu,\"render_p50_us\":%llu,\"render_p95_us\":%llu,\"render_max_us\":%llu,\"render_mean_us\":%.3f,\"frame_total_p50_us\":%llu,\"frame_total_p95_us\":%llu,\"frame_total_max_us\":%llu,\"frame_total_mean_us\":%.3f,\"background_preparations\":%llu,\"background_us\":%llu,\"background_from_game\":%s,\"full_buffer_initializations\":%llu,\"region_copies\":%llu,\"fade_frames\":%llu,\"static_preparations\":%llu,\"static_max_us\":%llu,\"fade_us\":160000,\"animation_hz\":30,\"animation_zone\":{\"x\":800,\"y\":300,\"width\":320,\"height\":180},\"state_dirty_rectangles\":4,\"progress_is_stage_estimate\":true,\"transition_mode\":\"incremental_8x8_dissolve_to_dark_before_stream_handoff\",\"first_native_picture_us\":%llu,\"transition_ready_observed_us\":%llu,\"transition_wait_us\":%llu,\"fade_p50_us\":%llu,\"fade_p95_us\":%llu,\"fade_max_us\":%llu,\"fade_mean_us\":%.3f,\"fade_seed_copies\":%llu,\"fade_cold_full_copies\":%llu,\"fade_blocks_written\":%llu,\"transition_scope\":\"UI clock independent of new AU; first native picture is main observation, not proof of physical display\"}",
        (unsigned long long)loading.view.render_calls,(unsigned long long)percentile(50),(unsigned long long)percentile(95),(unsigned long long)loading.view.render_max_us,
        loading.view.render_calls?(double)loading.render_total_us/(double)loading.view.render_calls:0,
        (unsigned long long)frame_percentile(50),(unsigned long long)frame_percentile(95),(unsigned long long)loading.view.frame_total_max_us,
        loading.view.render_calls?(double)loading.frame_total_us/(double)loading.view.render_calls:0,
        (unsigned long long)loading.view.background_preparations,(unsigned long long)loading.view.background_us,loading.view.has_game_background?"true":"false",
        (unsigned long long)loading.view.full_buffer_initializations,(unsigned long long)loading.view.region_copies,(unsigned long long)loading.view.fade_frames,
        (unsigned long long)loading.view.static_preparations,(unsigned long long)loading.view.static_max_us,
        (unsigned long long)loading.view.first_picture_us,(unsigned long long)loading.view.transition_ready_us,(unsigned long long)loading.view.transition_wait_us,
        (unsigned long long)fade_percentile(50),(unsigned long long)fade_percentile(95),(unsigned long long)loading.view.fade_max_us,
        loading.view.fade_frames?(double)loading.fade_total_us/(double)loading.view.fade_frames:0,
        (unsigned long long)loading.view.fade_seed_copies,(unsigned long long)loading.view.fade_full_initializations,(unsigned long long)loading.view.fade_blocks_written);
}
