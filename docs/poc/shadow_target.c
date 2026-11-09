/* S1-12 대상: 섀도 복사 + 100ms 주기 검사.
 *  - secret: 합법 변경 구간이 영영 안 열림(지뢰 배치형) → 외부 변조를 섀도 불일치로 탐지
 *  - timer : 100ms마다 합법 구간 안에서 ++ (tick형) → 정상 변경이므로 오탐 0이어야 함
 * 탐지/오탐을 detect.log에 CLOCK_MONOTONIC 타임스탬프와 함께 기록한다.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/mman.h>

struct watched { volatile uint64_t timer; volatile uint64_t secret; };
static struct watched *W;

static uint64_t shadow_timer, shadow_secret;
static atomic_int  timer_window = 0;   /* 합법 변경 구간 플래그 (mtd_begin/end) */
static FILE *detlog;
static long false_alerts = 0;

static uint64_t now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

/* 100ms 주기 검사 스레드 */
static void *checker(void *arg) {
    (void)arg;
    for (;;) {
        /* secret: 합법 구간이 없음 → 섀도와 다르면 무조건 변조 */
        uint64_t s = W->secret;
        if (s != shadow_secret) {
            fprintf(detlog, "DETECT 0x%016llx %llu\n",
                    (unsigned long long)s, (unsigned long long)now_ns());
            fflush(detlog);
            shadow_secret = s;          /* ack: 다음 변화까지 재알림 안 함 */
        }
        /* timer: 합법 구간 '밖'에서 섀도와 다르면 행위 위반(여기선 없어야 정상) */
        uint64_t t = W->timer;
        if (t != shadow_timer && !atomic_load(&timer_window)) {
            false_alerts++;
            fprintf(detlog, "FALSE timer 0x%016llx %llu\n",
                    (unsigned long long)t, (unsigned long long)now_ns());
            fflush(detlog);
            shadow_timer = t;
        }
        struct timespec ts = {0, 100L * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

int main(void) {
    W = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    W->timer  = 0;
    W->secret = 0x2222222222222222ULL;
    shadow_timer  = W->timer;
    shadow_secret = W->secret;

    detlog = fopen("detect.log", "w");

    FILE *info = fopen("target.info", "w");
    fprintf(info, "%d %p\n", (int)getpid(), (void *)&W->secret);  /* 주입기가 쓸 주소 */
    fclose(info);

    pthread_t th;
    pthread_create(&th, NULL, checker, NULL);

    /* 메인: 100ms마다 timer를 합법적으로 ++ (정상 게임 진행 흉내) */
    for (;;) {
        atomic_store(&timer_window, 1);   /* mtd_begin("tick") */
        W->timer++;
        shadow_timer = W->timer;          /* mtd_end(): 섀도 동기화(예측 검사 자리) */
        atomic_store(&timer_window, 0);

        struct timespec ts = {0, 100L * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
}
