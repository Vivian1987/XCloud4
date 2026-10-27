/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "ui_surface_test_trap.h"
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define PIXELS ((size_t)X4_UI_WIDTH*X4_UI_HEIGHT)
#define GUARD UINT32_C(0x6f991abc)
static X4UiTestTrap *active;

void x4_ui_test_trap_open(X4UiTestTrap *trap)
{
    assert(trap&&!active);memset(trap,0,sizeof(*trap));
    for(unsigned b=0;b<X4_UI_SCANOUTS;++b){
        void *mapped=mmap(NULL,PIXELS*4,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(mapped!=MAP_FAILED);trap->destination[b]=mapped;
        trap->guarded[b]=calloc(PIXELS+16,4);assert(trap->guarded[b]);
        trap->mirror[b]=trap->guarded[b]+8;
        for(unsigned i=0;i<8;++i)trap->guarded[b][i]=trap->guarded[b][PIXELS+8+i]=GUARD;
    }
    active=trap;
}
void x4_ui_test_trap_assert_guards(const X4UiTestTrap *trap)
{
    for(unsigned b=0;b<X4_UI_SCANOUTS;++b)for(unsigned i=0;i<8;++i)
        assert(trap->guarded[b][i]==GUARD&&trap->guarded[b][PIXELS+8+i]==GUARD);
}
void x4_ui_test_trap_close(X4UiTestTrap *trap)
{
    assert(trap==active);x4_ui_test_trap_assert_guards(trap);
    for(unsigned b=0;b<X4_UI_SCANOUTS;++b){
        assert(!munmap(trap->destination[b],PIXELS*4));free(trap->guarded[b]);
    }
    active=NULL;memset(trap,0,sizeof(*trap));
}
/* Only the store primitive is substituted. Any plain C or SIMD load through
 * a destination pointer elsewhere still accesses PROT_NONE and faults. */
void x4_ui_surface_test_store(uint32_t *destination,const uint32_t *values,size_t count)
{
    assert(active&&count&&values);
    uintptr_t pointer=(uintptr_t)destination;
    for(unsigned b=0;b<X4_UI_SCANOUTS;++b){
        uintptr_t begin=(uintptr_t)active->destination[b],end=begin+PIXELS*4;
        if(pointer>=begin&&pointer<end){
            assert(!(pointer&3u)&&count<=PIXELS&&count*4<=end-pointer);
            size_t at=(pointer-begin)/4;memcpy(active->mirror[b]+at,values,count*4);
            ++active->stores;active->pixels_stored+=count;return;
        }
    }
    assert(!"Presentation store escaped the destination traps");
}
void x4_ui_test_trap_positive_read(const X4UiTestTrap *trap)
{
    pid_t child=fork();assert(child>=0);
    if(!child){
        struct rlimit limit={0,0};(void)setrlimit(RLIMIT_CORE,&limit);
        signal(SIGSEGV,SIG_DFL);signal(SIGBUS,SIG_DFL);
        volatile uint32_t value=*(volatile uint32_t *)trap->destination[0];
        (void)value;_exit(93);
    }
    int status;assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGSEGV);
}
