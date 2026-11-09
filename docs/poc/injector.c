/* S1-11 주입기: 외부 프로세스의 특정 주소에 센티넬 값을 쓴다.
 * 사용법: injector <pid> <hex_addr> <vm|proc|ptrace>
 * 출력: 쓰기 성공 여부와 errno (쓰는 쪽 관점).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>

static const uint64_t SENTINEL = 0xDEADBEEFCAFEBABEULL;

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s pid hexaddr vm|proc|ptrace\n", argv[0]); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    uintptr_t addr = (uintptr_t)strtoull(argv[2], NULL, 16);
    const char *m = argv[3];
    uint64_t val = SENTINEL;

    if (!strcmp(m, "vm")) {
        struct iovec local  = { &val, sizeof val };
        struct iovec remote = { (void *)addr, sizeof val };
        errno = 0;
        ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
        printf("method=vm           ret=%zd errno=%d(%s)\n",
               n, errno, n < 0 ? strerror(errno) : "-");
        return n == (ssize_t)sizeof val ? 0 : 1;

    } else if (!strcmp(m, "proc")) {
        char path[64];
        snprintf(path, sizeof path, "/proc/%d/mem", (int)pid);
        errno = 0;
        int fd = open(path, O_RDWR);
        if (fd < 0) { printf("method=proc         open errno=%d(%s)\n", errno, strerror(errno)); return 1; }
        errno = 0;
        ssize_t n = pwrite(fd, &val, sizeof val, (off_t)addr);
        printf("method=proc         ret=%zd errno=%d(%s)\n",
               n, errno, n < 0 ? strerror(errno) : "-");
        close(fd);
        return n == (ssize_t)sizeof val ? 0 : 1;

    } else if (!strcmp(m, "ptrace")) {
        errno = 0;
        long a = ptrace(PTRACE_ATTACH, pid, 0, 0);
        if (a != 0) { printf("method=ptrace       attach errno=%d(%s)\n", errno, strerror(errno)); return 1; }
        waitpid(pid, NULL, 0);
        errno = 0;
        long p = ptrace(PTRACE_POKEDATA, pid, (void *)addr, (void *)val);
        int pe = errno;
        ptrace(PTRACE_DETACH, pid, 0, 0);
        printf("method=ptrace       poke=%ld errno=%d(%s)\n",
               p, pe, p != 0 ? strerror(pe) : "-");
        return p == 0 ? 0 : 1;
    }
    fprintf(stderr, "unknown method %s\n", m);
    return 2;
}
