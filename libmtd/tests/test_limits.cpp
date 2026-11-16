/* test_limits.cpp — 주기 검사 방식의 한계 측정: "바꿨다가 되돌리기"(일시 변조)
 *
 * 섀도 비교는 검사 시점의 값만 본다. 두 검사 사이(T = 50ms 안)에 값을 바꿨다가
 * 원래 값으로 되돌리면 검사 스레드는 변화를 못 본다. 이 파일은 그 한계를
 * 유지 시간(hold)별 탐지율로 측정하고, 결과를 MESSAGE로 출력한다.
 *
 *   1) hold 1ms  → 대부분 놓친다 (기대 탐지율 ≈ hold/T, 단언은 ≤ 20%)
 *   2) hold 2T   → 반드시 잡는다 (100%)
 *   3) 사이에 해당 변수의 mtd_begin이 열리면 hold가 짧아도 잡는다
 *   4) mtd_event는 비교를 하지 않는다 → 사이에 이벤트가 와도 놓친다
 *
 * 변조는 프로세스 안 쓰기로 흉내 낸다(섀도 비교는 쓰기 경로와 무관 — docs/poc/write-paths.md).
 * 타이밍 의존 테스트라 단언은 넉넉히 잡고, 정확한 수치는 출력으로 본다.
 */
#include "doctest.h"

#include "engine_test.hpp"
#include "mtd.h"
#include "mtd.hpp"
#include "test_util.hpp"

#include <random>

using namespace mtdtest;
using namespace mtd::detail;

namespace {

constexpr unsigned kPeriod = 50;  // 기본 검사 주기 T (MTD_DEFAULT_PERIOD_MS)

RuleSet timer_rules()
{
    RuleSet rs;
    VarRule timer;
    timer.name = "timer";
    timer.value.size = 4;
    timer.behavior.allowed_tags = {"tick"};
    timer.behavior.event_window_ms = 50;
    rs.add(timer);  // 예측 규칙 없음: 행위(구간 밖 변화)만 본다
    return rs;
}

struct Fixture {
    Collector col;
    Fixture()
    {
        mtd_config cfg = col.config(kPeriod);
        REQUIRE(test_init_with_rules(timer_rules(), &cfg) == MTD_OK);
    }
    ~Fixture() { mtd_shutdown(); }
};

/* 일시 변조 trials회: 무작위 위상에서 값을 바꾸고 hold_ms 뒤 되돌린다.
 * 회차 사이에 T 이상 쉬어 앞 회차의 alert가 다 들어온 뒤 센다.
 * between(): 바꾼 뒤·되돌리기 전에 실행할 동작(없으면 nullptr). 반환 = 탐지된 회차 수 */
template <typename F>
int run_transient(Fixture &f, volatile std::uint32_t &timer, unsigned hold_ms, int trials, F between)
{
    std::mt19937 rng(12345u + hold_ms);
    std::uniform_int_distribution<unsigned> phase(0, kPeriod);
    int detected = 0;
    for (int i = 0; i < trials; ++i) {
        sleep_ms(phase(rng));  // 검사 주기와의 위상을 섞는다
        std::size_t before = f.col.count();
        std::uint32_t orig = timer;
        timer = orig + 1000;  // 변조
        between();
        if (hold_ms) sleep_ms(hold_ms);
        timer = orig;         // 되돌리기
        sleep_ms(kPeriod * 2);
        if (f.col.count() > before) ++detected;
    }
    return detected;
}

}  // namespace

TEST_CASE("한계: 검사 주기보다 짧은 일시 변조(hold 1ms)는 대부분 놓친다")
{
    Fixture f;
    volatile std::uint32_t timer = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&timer), 4, "timer") == MTD_OK);
    const int trials = 50;
    int d = run_transient(f, timer, 1, trials, [] {});
    MESSAGE("hold 1ms, T=50ms: 탐지 " << d << "/" << trials);
    CHECK(d <= trials / 5);
    mtd_unwatch(const_cast<std::uint32_t *>(&timer));
}

TEST_CASE("한계: hold를 늘리면 탐지율이 오른다 (hold/T 비례 기대, 출력용)")
{
    Fixture f;
    volatile std::uint32_t timer = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&timer), 4, "timer") == MTD_OK);
    const int trials = 30;
    for (unsigned hold : {10u, 25u, 40u}) {
        int d = run_transient(f, timer, hold, trials, [] {});
        MESSAGE("hold " << hold << "ms, T=50ms: 탐지 " << d << "/" << trials);
    }
    mtd_unwatch(const_cast<std::uint32_t *>(&timer));
}

TEST_CASE("대조: hold가 2T(100ms) 이상이면 전부 잡는다")
{
    Fixture f;
    volatile std::uint32_t timer = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&timer), 4, "timer") == MTD_OK);
    const int trials = 20;
    int d = run_transient(f, timer, kPeriod * 2, trials, [] {});
    MESSAGE("hold 100ms, T=50ms: 탐지 " << d << "/" << trials);
    CHECK(d == trials);
    mtd_unwatch(const_cast<std::uint32_t *>(&timer));
}

TEST_CASE("보완: 사이에 그 변수의 구간(mtd_begin)이 열리면 짧은 일시 변조도 잡는다")
{
    Fixture f;
    volatile std::uint32_t timer = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&timer), 4, "timer") == MTD_OK);
    const int trials = 20;
    int d = run_transient(f, timer, 0, trials, [] {
        mtd_begin("tick");  // begin 직전 비교가 변조를 본다
        mtd_end();
    });
    MESSAGE("hold 0ms + 사이에 mtd_begin: 탐지 " << d << "/" << trials);
    CHECK(d == trials);
    mtd_unwatch(const_cast<std::uint32_t *>(&timer));
}

TEST_CASE("한계: 사이에 mtd_event가 와도 비교는 하지 않는다 → 놓친다")
{
    Fixture f;
    volatile std::uint32_t timer = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&timer), 4, "timer") == MTD_OK);
    const int trials = 50;
    int d = run_transient(f, timer, 1, trials, [] { mtd_event("input"); });
    MESSAGE("hold 1ms + 사이에 mtd_event: 탐지 " << d << "/" << trials);
    CHECK(d <= trials / 5);
    mtd_unwatch(const_cast<std::uint32_t *>(&timer));
}
