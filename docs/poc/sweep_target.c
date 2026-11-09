/* 검사 주기 스윕용 대상. shadow_target을 확장:
 *   argv[1] = period_ms (검사 주기)
 *   argv[2] = num_fillers (추가 감시 스칼라 수; CPU 부하용, 변하지 않음)
 * 검사 스레드는 secret/timer + filler들을 매 주기 비교하고, 종료(SIGTERM) 시
 * 자신의 CPU 시간(CLOCK_THREAD_CPUTIME_ID)과 폴 횟수를 cpu.txt에 쓴다.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>

struct watched { volatile uint64_t timer; volatile uint64_t secret; };
static struct watched *W;
static volatile uint64_t *fillers;   /* 변하지 않는 감시 변수들 (CPU 부하) */
static uint64_t *fill_shadow;
static int       num_fill;
static unsigned  period_ms;

static uint64_t shadow_timer, shadow_secret;
static atomic_int timer_window = 0;
static atomic_int stop_flag = 0;
static FILE *detlog;

static uint64_t now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}
static uint64_t thread_cpu_ns(void) {
    struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static void on_term(int sig) { (void)sig; atomic_store(&stop_flag, 1); }

static void *checker(void *arg) {
    (void)arg;
    uint64_t polls = 0;
    struct timespec ts = {0, (long)period_ms * 1000000L};
    while (!atomic_load(&stop_flag)) {
        uint64_t s = W->secret;
        if (s != shadow_secret) {
            fprintf(detlog, "DETECT 0x%016llx %llu\n",
                    (unsigned long long)s, (unsigned long long)now_ns());
            fflush(detlog);
            shadow_secret = s;
        }
        uint64_t t = W->timer;
        if (t != shadow_timer && !atomic_load(&timer_window)) {
            fprintf(detlog, "FALSE timer 0x%016llx %llu\n",
                    (unsigned long long)t, (unsigned long long)now_ns());
            fflush(detlog);
            shadow_timer = t;
        }
        /* filler 비교 (CPU 부하; 값은 안 변하므로 alert 없음) */
        for (int i = 0; i < num_fill; i++) {
            uint64_t v = fillers[i];
            if (v != fill_shadow[i]) fill_shadow[i] = v;
        }
        polls++;
        nanosleep(&ts, NULL);
    }
    uint64_t cpu = thread_cpu_ns();
    FILE *c = fopen("cpu.txt", "w");
    fprintf(c, "cpu_ns=%llu polls=%llu period_ms=%u fillers=%d\n",
            (unsigned long long)cpu, (unsigned long long)polls, period_ms, num_fill);
    fclose(c);
    return NULL;
}

int main(int argc, char **argv) {
    period_ms = (argc > 1) ? (unsigned)atoi(argv[1]) : 100;
    num_fill  = (argc > 2) ? atoi(argv[2]) : 0;
    if (period_ms == 0) period_ms = 100;

    W = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    W->timer = 0;
    W->secret = 0x2222222222222222ULL;
    shadow_timer = W->timer;
    shadow_secret = W->secret;

    if (num_fill > 0) {
        fillers = calloc((size_t)num_fill, sizeof(uint64_t));
        fill_shadow = calloc((size_t)num_fill, sizeof(uint64_t));
    }

    detlog = fopen("detect.log", "w");
    FILE *info = fopen("target.info", "w");
    fprintf(info, "%d %p\n", (int)getpid(), (void *)&W->secret);
    fclose(info);

    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term; sigaction(SIGTERM, &sa, NULL);

    pthread_t th;
    pthread_create(&th, NULL, checker, NULL);

    struct timespec tick = {0, 100L * 1000 * 1000};  /* 정상 timer tick은 100ms 고정 */
    while (!atomic_load(&stop_flag)) {
        atomic_store(&timer_window, 1);
        W->timer++;
        shadow_timer = W->timer;
        atomic_store(&timer_window, 0);
        nanosleep(&tick, NULL);
    }
    pthread_join(th, NULL);
    return 0;
}
