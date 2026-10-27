/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ui/apple_ui.h"
#include "../src/ui/font.h"

#define WIDTH 1920u
#define HEIGHT 1080u
static const uint32_t left_edge = X4_RGB(255,60,60);
static const uint32_t right_edge = X4_RGB(60,255,60);
static const uint32_t top_edge = X4_RGB(60,60,255);
static const uint32_t bottom_edge = X4_RGB(255,230,60);
static unsigned wrapped_calls;
static int wrap_x,wrap_y,wrap_width,wrap_step,wrap_limit;
static bool check_wrap;

void __real_x4_text_px(uint32_t *,int,int,int,int,const char *,uint32_t);
void __wrap_x4_text_px(uint32_t *pixels,int x,int y,int size,int weight,const char *text,uint32_t color)
{
    if (check_wrap) {
        assert(x==wrap_x && y==wrap_y+(int)wrapped_calls*wrap_step);
        assert(wrapped_calls<(unsigned)wrap_limit);
        assert(x4_text_width_px(size,weight,text)<=wrap_width);
        size_t n=strlen(text);
        assert(n && text[0]!=' ' && text[n-1]!=' ');
        const char *c=text;
        while (*c) assert(x4_utf8_next(&c)!=0xfffdu);
        ++wrapped_calls;
    }
    __real_x4_text_px(pixels,x,y,size,weight,text,color);
}

/* A genuine 1920x1080 raster with neutral procedural scenery, coordinate
 * texture and edge witnesses. No downloaded or AI game artwork is involved. */
static X4CoverRaster fixture(void)
{
    X4CoverRaster r={.width=WIDTH,.height=HEIGHT};
    r.rgba=malloc((size_t)WIDTH*HEIGHT*sizeof(*r.rgba));
    assert(r.rgba);
    for (unsigned y=0;y<HEIGHT;++y) for (unsigned x=0;x<WIDTH;++x) {
        unsigned mountain=550u+(x*7u/13u)%190u;
        unsigned r0=y>mountain ? 35u : 90u+x*55u/WIDTH;
        unsigned g0=y>mountain ? 75u+y*35u/HEIGHT : 125u+y*45u/HEIGHT;
        unsigned b0=y>mountain ? 85u : 175u+x*50u/WIDTH;
        unsigned grain=(x*13u+y*7u)%13u;
        uint32_t c=X4_RGB(r0+grain,g0+grain,b0+grain);
        if (x<24) c=left_edge;
        if (x>=WIDTH-24) c=right_edge;
        if (y<24) c=top_edge;
        if (y>=HEIGHT-24) c=bottom_edge;
        r.rgba[(size_t)y*WIDTH+x]=c;
    }
    return r;
}

static void save_ppm(const char *dir,const char *name,const uint32_t *p)
{
    if (!dir) return;
    char path[1024];
    assert(snprintf(path,sizeof(path),"%s/%s.ppm",dir,name)<(int)sizeof(path));
    FILE *f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n1920 1080\n255\n");
    for (unsigned i=0;i<WIDTH*HEIGHT;++i) {
        unsigned char rgb[3]={(unsigned char)p[i],(unsigned char)(p[i]>>8),(unsigned char)(p[i]>>16)};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(!fclose(f));
}

static unsigned red(uint32_t c) { return c&255u; }
static unsigned green(uint32_t c) { return (c>>8)&255u; }

static void crop_policy(uint32_t *p,const X4CoverRaster *r,const char *dir)
{
    /* Actual Explore card geometry from screen.c: 852x448 (~1.902:1).
     * 16:9 requires 6.52% vertical crop: 10% may fill, 5% must contain. */
    const int x=84,y=304,w=852,h=448;
    double removed=1.0-(16.0/9.0)/((double)w/h);
    assert(removed>0.05 && removed<0.10);
    x4_apple_bg_draw(p);
    x4_apple_blit_card_rounded(p,x,y,w,h,24,r,X4_GRAD_NONE);
    assert(red(p[(y+h/2)*WIDTH+x+2])>220);
    assert(green(p[(y+h/2)*WIDTH+x+w-3])>220);
    /* Sample the image at each horizontal border away from rounded corners.
     * These are coloured source pixels, not empty strips. */
    for (int py=y+24;py<y+h-24;++py) {
        assert(red(p[(size_t)py*WIDTH+x+2])>220);
        assert(green(p[(size_t)py*WIDTH+x+w-3])>220);
    }
    x4_text_px(p,84,236,26,X4_FONT_SEMIBOLD,"Explore card: 16:9 source, 6.52% crop (limit 10%)",X4_RGB(240,245,255));
    save_ppm(dir,"image_card_10_percent_16x9",p);

    x4_apple_bg_draw(p);
    x4_apple_blit_image_rounded(p,x,y,w,h,24,r,X4_GRAD_NONE);
    /* The same dimensions through the cover/detail API retain 5% policy:
     * contain keeps both source edges; padding is a dark source copy. */
    int fit_w=(int)((double)h*16.0/9.0+0.5);
    int fit_x=x+(w-fit_w)/2;
    assert(red(p[(y+h/2)*WIDTH+x+2])<120);
    assert(red(p[(y+h/2)*WIDTH+fit_x])>220);
    assert(green(p[(y+h/2)*WIDTH+fit_x+fit_w-1])>220);
    x4_text_px(p,84,236,26,X4_FONT_SEMIBOLD,"Cover/detail: same source, contain (limit 5%)",X4_RGB(240,245,255));
    save_ppm(dir,"image_detail_5_percent_16x9",p);

    x4_apple_bg_draw(p);
    /* A 16:9 image cannot fill the tall Fortnite home card under a 10% cap:
     * it would require 47.27% horizontal crop. Keep full image and use a
     * source-derived dark fill. Runtime Poster/BoxArt is the preferred fit. */
    const int fx=966,fy=304,fw=420,fh=448;
    double tall_crop=1.0-((double)fw/fh)/(16.0/9.0);
    assert(tall_crop>0.47 && tall_crop<0.48);
    x4_apple_blit_card_rounded(p,fx,fy,fw,fh,24,r,X4_GRAD_NONE);
    int fit_h=(int)((double)fw/(16.0/9.0)+0.5);
    int fit_y=fy+(fh-fit_h)/2;
    assert(p[(size_t)fit_y*WIDTH+fx+fw/2]==top_edge);
    assert(p[(size_t)(fit_y+fit_h-1)*WIDTH+fx+fw/2]==bottom_edge);
    uint32_t pad1=p[(size_t)(fy+40)*WIDTH+fx+fw/3];
    uint32_t pad2=p[(size_t)(fy+70)*WIDTH+fx+fw*2/3];
    assert((pad1&0xffffffu)!=0 && (pad2&0xffffffu)!=0 && pad1!=pad2);
    x4_text_px(p,84,180,25,X4_FONT_SEMIBOLD,"Tall Fortnite card: source-derived dark fill; 47.27% crop forbidden",X4_RGB(240,245,255));
    save_ppm(dir,"image_fortnite_16x9_contain_limit_10",p);

    x4_apple_bg_draw(p);
    x4_apple_draw_hero_banner(p,120,200,1680,390,24,r);
    /* Hero keeps the full 16:9 raster at the right; its left fade is design,
     * not an accidental cropped source. The visible right witnesses survive. */
    assert(green(p[(200+195)*WIDTH+1794])>220);
    assert(p[(size_t)205*WIDTH+1700]==top_edge);
    assert(p[(size_t)584*WIDTH+1700]==bottom_edge);
    x4_text_px(p,160,270,44,X4_FONT_BOLD,"Neutral 1920x1080 fixture",X4_RGB(240,245,255));
    x4_text_px(p,160,340,24,X4_FONT_REGULAR,"All source borders retained in the hero image",X4_RGB(200,215,235));
    save_ppm(dir,"image_hero_full_16x9",p);
    printf("PASS G2: card crop %.3f%% fills; detail cap5%% contains; tall-card %.3f%% crop refused; hero full source\n",removed*100.0,tall_crop*100.0);
}

static void wrap_test(uint32_t *p)
{
    const char *text[]={
        "¿Dónde están las imágenes? La conexión podría tardar: comprueba tu servidor y vuelve a intentarlo. Ñandú, pingüino, corazón y autorización.",
        "Xbox is preparing your game. This deliberately long message must stay inside the translucent card without splitting UTF-8 characters.",
        "áéíóúüñÁÉÍÓÚÜÑáéíóúüñÁÉÍÓÚÜÑáéíóúüñÁÉÍÓÚÜÑáéíóúüñÁÉÍÓÚÜÑ",
        "Primera línea con tildes.\nSegunda línea más larga con ¿preguntas? y ¡respuestas!"
    };
    for (unsigned i=0;i<sizeof(text)/sizeof(text[0]);++i) {
        memset(p,0,WIDTH*HEIGHT*sizeof(*p));
        wrap_x=120;wrap_y=140;wrap_width=i==2 ? 90 : 360;wrap_step=34;wrap_limit=5;
        wrapped_calls=0;check_wrap=true;
        int n=x4_apple_text_wrap(p,wrap_x,wrap_y,24,X4_FONT_REGULAR,text[i],wrap_width,wrap_step,wrap_limit,X4_RGB(245,245,245));
        check_wrap=false;
        assert(n==(int)wrapped_calls && n>0 && n<=wrap_limit);
        for (unsigned y=0;y<HEIGHT;++y) for (unsigned x=0;x<WIDTH;++x) {
            if (p[(size_t)y*WIDTH+x]) {
                assert(x>=(unsigned)wrap_x && x<(unsigned)(wrap_x+wrap_width));
                assert(y>=(unsigned)wrap_y && y<(unsigned)(wrap_y+wrap_limit*wrap_step));
            }
        }
    }
    puts("PASS G2: actual UTF-8 wrapped text respects width, line count and framebuffer height bounds");
}

int main(int argc,char **argv)
{
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    uint32_t *owned=calloc(WIDTH*HEIGHT+2,sizeof(*owned));assert(owned);
    owned[0]=owned[WIDTH*HEIGHT+1]=0xcafebabeu;
    X4CoverRaster r=fixture();
    if (argc>1) save_ppm(argv[1],"image_source_1920x1080",r.rgba);
    crop_policy(owned+1,&r,argc>1 ? argv[1] : NULL);
    wrap_test(owned+1);
    assert(owned[0]==0xcafebabeu && owned[WIDTH*HEIGHT+1]==0xcafebabeu);
    free(r.rgba);free(owned);x4_font_close();
    return 0;
}
