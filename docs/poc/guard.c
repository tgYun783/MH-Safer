#define _GNU_SOURCE
#include "guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ucontext.h>

volatile sig_atomic_t g_fault_count = 0;
volatile uintptr_t     g_last_pc    = 0;
volatile uintptr_t     g_last_addr  = 0;
volatile uint64_t      g_last_old   = 0;

static void       *g_page;
static unsigned long g_psize;

/* SA_SIGINFO 핸들러: async-signal-safe 범위만 사용(mprotect만, printf 금지) */
static void on_segv(int sig, siginfo_t *si, void *uctx)
{
    (void)sig;
    ucontext_t *uc = (ucontext_t *)uctx;
    g_fault_count++;
    g_last_addr = (uintptr_t)si->si_addr;
#if defined(__aarch64__)
    g_last_pc = (uintptr_t)uc->uc_mcontext.pc;
#elif defined(__x86_64__)
    g_last_pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
    g_last_pc = 0;
#endif
    /* 폴트 시점에는 아직 쓰기가 실행되기 전 → 현재 값이 '이전 값' */
    g_last_old = *(volatile uint64_t *)g_page;
    /* 보호를 풀어 faulting 쓰기 명령이 재실행되어 완료되게 한다 */
    mprotect(g_page, g_psize, PROT_READ | PROT_WRITE);
}

void *g_setup(unsigned long *out_psize)
{
    g_psize = (unsigned long)sysconf(_SC_PAGESIZE);
    g_page = mmap(NULL, g_psize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_page == MAP_FAILED) { perror("mmap"); exit(1); }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0) { perror("sigaction SIGSEGV"); exit(1); }
    sigaction(SIGBUS, &sa, NULL); /* 일부 환경은 보호 위반을 SIGBUS로 전달 */

    if (out_psize) *out_psize = g_psize;
    return g_page;
}

void g_arm(void)
{
    mprotect(g_page, g_psize, PROT_READ);
}
