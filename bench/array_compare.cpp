/* array_compare.cpp — S2-14 배열 비교 오버헤드 측정
 *
 * 질문: mine_field·cell_state(각 480바이트)를 바이트 비교하면 검사 1회 비용과 CPU가 얼마인가
 *       (docs/monitoring-design.md §3 "배열 검사 오버헤드는 미측정").
 *
 * 측정 1 (마이크로): 변수 1개 검사 = memcpy(스냅샷) + memcmp(섀도)의 ns/회. 4바이트 vs 480바이트.
 *         엔진(libmtd/src/mtd.cpp detect_outside)이 변수마다 하는 일과 같다.
 * 측정 2 (엔진): 실제 libmtd 검사 스레드를 T=50ms로 돌리고, 메인 스레드는 잠든 채
 *         프로세스 CPU 시간(CLOCK_PROCESS_CPUTIME_ID) ≈ 검사 스레드 CPU를 잰다.
 *         구성: 지뢰찾기 GameState 1개(스칼라 4 + 배열 2), 스칼라만 4개, GameState 100개.
 *
 * 실행: bench_array_compare [측정 2 구간 초, 기본 5]
 * 출력: docs/poc/array-compare.md에 그대로 붙일 수 있는 마크다운 표.
 */
#include "mtd.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

std::uint64_t clock_ns(clockid_t id)
{
    timespec ts{};
    clock_gettime(id, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

volatile int g_sink = 0;

/* 변수 1개 검사 비용 (ns/회) */
double micro_ns_per_check(std::size_t size, int iters)
{
    /* 실제값 == 섀도: memcmp가 끝까지 읽는 정상 상태(최악 비용) */
    std::vector<std::uint8_t> src(size, 0), shadow(size, 0), scratch(size, 0);
    int acc = 0;
    std::uint64_t t0 = clock_ns(CLOCK_MONOTONIC);
    for (int i = 0; i < iters; ++i) {
        /* 컴파일러가 반복을 합치거나 없애지 않게 메모리 장벽 */
        __asm__ __volatile__("" : : "r"(src.data()), "r"(scratch.data()) : "memory");
        std::memcpy(scratch.data(), src.data(), size);
        acc += std::memcmp(scratch.data(), shadow.data(), size);
    }
    std::uint64_t t1 = clock_ns(CLOCK_MONOTONIC);
    g_sink = acc;
    return static_cast<double>(t1 - t0) / iters;
}

struct FakeState {  /* game/minesweeper.hpp GameState의 감시 필드와 같은 크기 */
    std::uint32_t timer;
    std::int32_t  mines_remaining;
    std::uint8_t  mine_field[480];
    std::uint8_t  cell_state[480];
    std::uint32_t cells_opened;
    std::uint32_t game_state;
};

struct EngineResult {
    unsigned vars;
    unsigned arrays;
    double   cpu_pct;
    double   us_per_scan;
};

EngineResult engine_cpu(unsigned states, bool with_arrays, unsigned seconds, unsigned period_ms)
{
    mtd_config cfg{};
    cfg.period_ms = period_ms;
    if (mtd_init(nullptr, &cfg) != MTD_OK) {
        std::fprintf(stderr, "mtd_init failed\n");
        std::exit(1);
    }
    std::vector<FakeState> s(states);
    std::memset(s.data(), 0, s.size() * sizeof(FakeState));
    unsigned vars = 0, arrays = 0;
    for (auto &st : s) {
        mtd_watch(&st.timer, 4, "timer");
        mtd_watch(&st.mines_remaining, 4, "mines_remaining");
        mtd_watch(&st.cells_opened, 4, "cells_opened");
        mtd_watch(&st.game_state, 4, "game_state");
        vars += 4;
        if (with_arrays) {
            mtd_watch(st.mine_field, sizeof st.mine_field, "mine_field");
            mtd_watch(st.cell_state, sizeof st.cell_state, "cell_state");
            vars += 2;
            arrays += 2;
        }
    }
    usleep(200 * 1000);  /* 검사 스레드 기동 */
    std::uint64_t c0 = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    std::uint64_t w0 = clock_ns(CLOCK_MONOTONIC);
    sleep(seconds);
    std::uint64_t c1 = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    std::uint64_t w1 = clock_ns(CLOCK_MONOTONIC);
    mtd_shutdown();

    double cpu = static_cast<double>(c1 - c0);
    double wall = static_cast<double>(w1 - w0);
    double scans = wall / (period_ms * 1e6);
    return {vars, arrays, 100.0 * cpu / wall, cpu / scans / 1000.0};
}

}  // namespace

int main(int argc, char **argv)
{
    unsigned seconds = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 5u;
    if (seconds == 0) seconds = 5;
    const unsigned T = MTD_DEFAULT_PERIOD_MS;

    std::printf("## 측정 1: 변수 1개 검사(memcpy + memcmp) 비용\n\n");
    std::printf("| 크기(바이트) | ns/회 |\n|---|---|\n");
    for (std::size_t sz : {std::size_t{4}, std::size_t{480}}) {
        micro_ns_per_check(sz, 200000);  /* 예열 */
        std::printf("| %zu | %.1f |\n", sz, micro_ns_per_check(sz, 2000000));
    }

    std::printf("\n## 측정 2: libmtd 검사 스레드 CPU (T = %u ms, %u초)\n\n", T, seconds);
    std::printf("| 구성 | 감시 변수 | 배열(480B) | 검사 CPU(%%) | 검사 1회(us) |\n|---|---|---|---|---|\n");
    struct { const char *label; unsigned states; bool arrays; } cases[] = {
        {"스칼라만 (GameState 1개, 배열 제외)", 1, false},
        {"GameState 1개 (게임 실제 구성)", 1, true},
        {"GameState 100개", 100, true},
    };
    for (const auto &c : cases) {
        EngineResult r = engine_cpu(c.states, c.arrays, seconds, T);
        std::printf("| %s | %u | %u | %.4f | %.2f |\n", c.label, r.vars, r.arrays, r.cpu_pct, r.us_per_scan);
    }
    return 0;
}
