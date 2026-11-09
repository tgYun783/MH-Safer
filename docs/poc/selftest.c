/* S1-10: 같은 프로세스 안의 쓰기를 mprotect+SIGSEGV로 잡는다.
 *  - 보호 변수에 100회 쓰기 → 이벤트 100건
 *  - 보호 페이지 밖 변수에 100회 쓰기 → 이벤트 0건
 *  - 한 건의 상세(주소/이전값/새값/쓰기명령 주소) 출력
 */
#define _GNU_SOURCE
#include "guard.h"
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    unsigned long psize;
    volatile uint64_t *var = (volatile uint64_t *)g_setup(&psize);
    *var = 0x1111111111111111ULL;

    /* --- 보호 변수에 100회 쓰기 --- */
    uint64_t sample_old = 0, sample_new = 0;
    uintptr_t sample_pc = 0, sample_addr = 0;
    for (int i = 0; i < 100; i++) {
        g_arm();                 /* PROT_READ로 재무장 */
        *var = 0xA000ULL + i;    /* → SIGSEGV → 핸들러 기록 후 쓰기 진행 */
        if (i == 0) {
            sample_old  = g_last_old;
            sample_new  = *var;
            sample_pc   = g_last_pc;
            sample_addr = g_last_addr;
        }
    }
    int guarded_events = g_fault_count;

    /* --- 보호 페이지 밖 변수에 100회 쓰기 --- */
    int before = g_fault_count;
    volatile uint64_t other = 0;
    for (int i = 0; i < 100; i++) other = 0xB000ULL + i;
    (void)other;
    int outside_events = g_fault_count - before;

    printf("[S1-10] 보호변수 쓰기 100회 → 이벤트 %d건  (기대 100)\n", guarded_events);
    printf("[S1-10] 페이지 밖  쓰기 100회 → 이벤트 %d건  (기대 0)\n", outside_events);
    printf("[S1-10] 샘플 이벤트: addr=0x%lx  old=0x%llx  new=0x%llx  write_pc=0x%lx\n",
           (unsigned long)sample_addr,
           (unsigned long long)sample_old,
           (unsigned long long)sample_new,
           (unsigned long)sample_pc);

    int ok = (guarded_events == 100) && (outside_events == 0);
    printf("[S1-10] 판정: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
