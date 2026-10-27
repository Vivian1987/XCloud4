/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the established real-font bounds/non-overlap observer, not a UI
 * double. Every card below is the actual x4_session_draw implementation. */
#define main phase20_screen_fixture_main
#include "test_tarea20_screens.c"
#undef main
int main(int argc,char **argv)
{
    assert(!x4_font_init("assets/fonts/inter.x4font"));
    owned=malloc((PIXELS+2)*sizeof(uint32_t));assert(owned);owned[0]=owned[PIXELS+1]=0xdeadbeefu;pixels=owned+1;
    for(unsigned language=0;language<2;++language){
        x4_lang_set(language?X4_LANG_EN:X4_LANG_ES,language?"en-US":"es-MX",language?"US":"MX");
        const char *suffix=language?"en":"es";
        X4SessionSnapshot session={.state=X4_SESSION_WAITING,.waiting_for_resources=1,
            .queue_elapsed_seconds=181,.elapsed_seconds=190,.queue_eta_available=1,.queue_eta_seconds=119};
        snprintf(session.title_name,sizeof(session.title_name),"Original queue fixture");
        snprintf(session.stage,sizeof(session.stage),"esperando recursos de Xbox");
        begin_frame("queue_optional_eta");x4_session_draw(&session,1,0,pixels);end_frame();
        capture(argc>1?argv[1]:NULL,"20c_queue_optional_eta",suffix);
        session.queue_eta_available=0;
        begin_frame("queue_no_eta");x4_session_draw(&session,1,0,pixels);end_frame();
        capture(argc>1?argv[1]:NULL,"20c_queue_no_eta",suffix);
        session.state=X4_SESSION_ERROR;session.waiting_for_resources=0;
        session.queue_limit_reached=1;session.error=X4_AUTH_E_DEADLINE;
        begin_frame("queue_limit");x4_session_draw(&session,0,0,pixels);end_frame();
        capture(argc>1?argv[1]:NULL,"20c_queue_limit",suffix);
    }
    x4_font_close();free(owned);puts("PASS TAREA20c real queue cards ES/EN: bounds/non-overlap and small technical code");return 0;
}
