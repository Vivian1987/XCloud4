/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ui/loading_screen.h"
#include "../src/ui/apple_ui.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"

enum { W=1920,H=1080,AX=800,AY=300,AW=320,AH=180 };
#define PIXELS ((size_t)W*H)
static uint32_t *owned,*pixels;
static void capture(const char *dir,unsigned phase,const char *language)
{
    if(!dir)return;
    char path[1024];snprintf(path,sizeof(path),"%s/20c_rocket_phase_%02u_%s.ppm",dir,phase,language);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",W,H);
    unsigned char *row=malloc(W*3);assert(row);
    for(unsigned y=0;y<H;++y){
        for(unsigned x=0;x<W;++x){uint32_t v=pixels[(size_t)y*W+x];row[x*3]=(unsigned char)v;row[x*3+1]=(unsigned char)(v>>8);row[x*3+2]=(unsigned char)(v>>16);}
        assert(fwrite(row,3,W,f)==W);
    }
    free(row);assert(!fclose(f));
}
static unsigned count_color(uint32_t color)
{
    unsigned count=0;
    for(unsigned y=AY;y<AY+AH;++y)for(unsigned x=AX;x<AX+AW;++x)
        if(pixels[(size_t)y*W+x]==color)++count;
    return count;
}
static int rocket_tip(void)
{
    for(int y=AY;y<430;++y)for(int x=946;x<975;++x)
        if(pixels[(size_t)y*W+x]==X4_RGB(232,237,245))return y;
    assert(!"Rocket body not found");return -1;
}
static void rocket_frames(const char *dir,unsigned language)
{
    x4_lang_set(language?X4_LANG_EN:X4_LANG_ES,language?"en-US":"es-MX",language?"US":"MX");
    const char *suffix=language?"en":"es";
    const uint64_t begin=1000000;
    assert(x4_loading_screen_begin(language?"Original rocket fixture":"Cohete original de prueba",NULL,begin));
    assert(x4_loading_screen_draw(pixels,begin));
    uint32_t *first=malloc((size_t)AW*AH*4);assert(first);
    for(unsigned y=0;y<AH;++y)memcpy(first+(size_t)y*AW,pixels+(size_t)(AY+y)*W+AX,AW*4);
    assert(count_color(X4_RGB(232,237,245))>500);
    assert(count_color(X4_RGB(154,168,190))>200);
    assert(count_color(X4_RGB(61,220,151))>100);
    assert(count_color(X4_RGB(255,255,255))>3000);
    assert(count_color(X4_RGB(255,143,51))>50);
    assert(count_color(X4_RGB(255,221,96))>25);
    assert(count_color(X4_RGB(13,27,45))>50);
    capture(dir,0,suffix);
    /* A UI canvas may be reused with no new state or progress publication.
     * Any animation write outside its advertised damage zone is a failure. */
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        if(x<AX||x>=AX+AW||y<AY||y>=AY+AH)pixels[(size_t)y*W+x]=0xff273849u;
    int lower=-1,upper=-1;
    for(unsigned phase=1;phase<=54;++phase){
        uint64_t now=begin+(uint64_t)phase*33334;
        assert(x4_loading_screen_draw(pixels,now));
        assert(owned[0]==0xabcdef12u&&owned[PIXELS+1]==0xabcdef12u);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
            if(x<AX||x>=AX+AW||y<AY||y>=AY+AH)assert(pixels[(size_t)y*W+x]==0xff273849u);
        if(phase==13)lower=rocket_tip();
        if(phase==40)upper=rocket_tip();
        if(phase==54)for(unsigned y=0;y<AH;++y)
            assert(!memcmp(first+(size_t)y*AW,pixels+(size_t)(AY+y)*W+AX,AW*4));
    }
    assert(lower-upper==20); /* +/-10px in one1.8s period. */
    free(first);x4_loading_screen_end();
    /* Full UI captures at representative loop positions, with all text and
     * backing intact, use fresh launches rather than the sentinel canvas. */
    const unsigned phases[3]={13,27,40};
    for(unsigned i=0;i<3;++i){
        assert(x4_loading_screen_begin(language?"Original rocket fixture":"Cohete original de prueba",NULL,begin));
        assert(x4_loading_screen_draw(pixels,begin+(uint64_t)phases[i]*33334));
        capture(dir,phases[i],suffix);x4_loading_screen_end();
    }
}
int main(int argc,char **argv)
{
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    owned=malloc((PIXELS+2)*4);assert(owned);owned[0]=owned[PIXELS+1]=0xabcdef12u;pixels=owned+1;
    for(unsigned language=0;language<2;++language)rocket_frames(argc>1?argv[1]:NULL,language);
    free(owned);x4_font_close();
    puts("PASS TAREA20c original procedural rocket: requested colors, white cloud, flame, exact54-frame/1.8s loop, +/-10px bob,320x180-only writes, ES/EN");return 0;
}
