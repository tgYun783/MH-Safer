/* 공용 guard: 한 페이지를 PROT_READ로 보호하고, 쓰기 시 SIGSEGV를
 * 핸들러에서 기록한 뒤 쓰기를 계속 진행시킨다 (S1-10 메커니즘).
 *
 * 주의: 이 PoC는 "재무장(re-arm)"을 호출자가 g_rearm()으로 직접 한다.
 * 투명한 자동 재무장(single-step/userfaultfd)은 엔진 설계에서 다룬다.
 */
#ifndef GUARD_H
#define GUARD_H
#include <stdint.h>
#include <signal.h>

extern volatile sig_atomic_t g_fault_count;   /* SIGSEGV가 핸들러로 들어온 횟수 */
extern volatile uintptr_t    g_last_pc;       /* 마지막 폴트의 쓰기 명령 주소 */
extern volatile uintptr_t    g_last_addr;     /* 마지막 폴트의 접근 주소 */
extern volatile uint64_t     g_last_old;      /* 쓰기 직전(핸들러 시점) 값 */

void  *g_setup(unsigned long *out_psize);     /* 보호 페이지 mmap+핸들러 설치, 페이지 주소 반환 */
void   g_arm(void);                           /* 페이지를 PROT_READ로 (재)보호 */

#endif
