/* S1-11 대상 프로세스: 변수 하나를 PROT_READ로 보호하고 계속 살아 있으면서
 * 상태(현재 값, 폴트 횟수)를 파일로 내보낸다. 외부 주입기가 이 변수에 쓴다.
 *
 * 매 주기 g_arm()으로 재무장 → 외부 쓰기가 폴트를 '낸다면' 잡힌다.
 * 폴트 없이 값만 바뀌면 = mprotect가 그 경로를 못 막은 것.
 */
#define _GNU_SOURCE
#include "guard.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

int main(void)
{
    unsigned long psize;
    volatile uint64_t *var = (volatile uint64_t *)g_setup(&psize);
    *var = 0x1111111111111111ULL;
    g_arm();

    FILE *f = fopen("target.info", "w");
    fprintf(f, "%d %p\n", (int)getpid(), (void *)var);
    fclose(f);

    for (;;) {
        g_arm();                               /* 재무장 (PROT_READ) */
        uint64_t v = *(volatile uint64_t *)var; /* 읽기는 허용됨 */
        int fc = g_fault_count;

        FILE *s = fopen("target.status", "w");
        fprintf(s, "val=0x%016llx faults=%d last_pc=0x%lx last_addr=0x%lx\n",
                (unsigned long long)v, fc,
                (unsigned long)g_last_pc, (unsigned long)g_last_addr);
        fclose(s);

        struct timespec ts = {0, 100L * 1000 * 1000}; /* 100ms */
        nanosleep(&ts, NULL);
    }
}
