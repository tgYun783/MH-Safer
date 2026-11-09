/* S1-12 주입기: /proc/<pid>/mem 로 secret에 count회 외부 변조.
 * 매 회 고유 값 + 주입 시각(CLOCK_MONOTONIC)을 inject.log에 남긴다.
 * 사용법: shadow_injector <pid> <hexaddr> <count> <interval_ms>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: %s pid hexaddr count interval_ms\n", argv[0]); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    off_t addr = (off_t)strtoull(argv[2], NULL, 16);
    int count = atoi(argv[3]);
    long iv_ms = atol(argv[4]);

    char path[64];
    snprintf(path, sizeof path, "/proc/%d/mem", (int)pid);
    int fd = open(path, O_RDWR);
    if (fd < 0) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); return 1; }

    FILE *log = fopen("inject.log", "w");
    int ok = 0;
    for (int i = 1; i <= count; i++) {
        uint64_t v = 0xC0DE000000000000ULL + (uint64_t)i;  /* 매회 고유 값 */
        uint64_t t0 = now_ns();
        ssize_t n = pwrite(fd, &v, sizeof v, addr);
        if (n == (ssize_t)sizeof v) ok++;
        fprintf(log, "INJECT %d 0x%016llx %llu\n", i, (unsigned long long)v, (unsigned long long)t0);
        fflush(log);
        struct timespec ts = { iv_ms / 1000, (iv_ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    fclose(log);
    close(fd);
    fprintf(stderr, "injected ok=%d/%d\n", ok, count);
    return ok == count ? 0 : 1;
}
