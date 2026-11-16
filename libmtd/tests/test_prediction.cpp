/* test_prediction.cpp — S2-22 예측 기반 탐지(L4) 완료 기준
 *   규칙 6종마다 정상 케이스 alert 0 / 위반 케이스 prediction alert 1, 단위 테스트 12개 이상.
 *   (1) check_prediction 직접 호출: 규칙마다 정상·위반 (12개 이상)
 *   (2) 엔진 경유: game/rules.json으로 mtd_init → 구간 안 변경 → mtd_end에서 prediction alert
 */
#include "doctest.h"

#include "engine_test.hpp"
#include "mtd.h"
#include "mtd.hpp"
#include "prediction.hpp"
#include "rules.hpp"
#include "test_util.hpp"

#include <cstring>
#include <vector>

using namespace mtd::detail;
using namespace mtdtest;

namespace {

template <class T>
PredictionResult check(const VarRule &r, T before, T after)
{
    std::uint8_t b[sizeof(T)], a[sizeof(T)];
    std::memcpy(b, &before, sizeof(T));
    std::memcpy(a, &after, sizeof(T));
    return check_prediction(r, b, a, sizeof(T));
}

VarRule scalar(PredRule rule, std::size_t size = 4, bool is_signed = false)
{
    VarRule r;
    r.name = "v";
    r.value.size = size;
    r.value.is_signed = is_signed;
    r.prediction.rule = rule;
    return r;
}

VarRule array_rule(Direction d)
{
    VarRule r;
    r.name = "arr";
    r.value.is_array = true;
    r.value.element_size = 1;
    r.prediction.rule = PredRule::per_element_monotonic;
    r.prediction.direction = d;
    return r;
}

PredictionResult check_bytes(const VarRule &r, std::vector<std::uint8_t> before, std::vector<std::uint8_t> after)
{
    REQUIRE(before.size() == after.size());
    return check_prediction(r, before.data(), after.data(), before.size());
}

bool violated(const PredictionResult &res, const char *rule)
{
    return !res.ok && res.rule_id != nullptr && std::strcmp(res.rule_id, rule) == 0;
}

}  // namespace

/* ---------- (1) check_prediction 직접 호출 ---------- */

TEST_CASE("delta_eq: +1 정상")
{
    auto r = scalar(PredRule::delta_eq);
    r.prediction.delta = 1;
    CHECK(check<std::uint32_t>(r, 7, 8).ok);
}

TEST_CASE("delta_eq: +1이 아니면 위반 (+2, 0으로 되돌리기, 랩어라운드)")
{
    auto r = scalar(PredRule::delta_eq);
    r.prediction.delta = 1;
    CHECK(violated(check<std::uint32_t>(r, 7, 9), "delta_eq"));
    CHECK(violated(check<std::uint32_t>(r, 7, 0), "delta_eq"));
    CHECK(violated(check<std::uint32_t>(r, 0xFFFFFFFFu, 0), "delta_eq"));
}

TEST_CASE("delta_in: ±1 정상 (부호 있는 값, 0 아래로)")
{
    auto r = scalar(PredRule::delta_in, 4, true);
    r.prediction.deltas = {-1, 1};
    CHECK(check<std::int32_t>(r, 10, 9).ok);
    CHECK(check<std::int32_t>(r, 10, 11).ok);
    CHECK(check<std::int32_t>(r, 0, -1).ok);  /* 깃발을 지뢰 수보다 많이 꽂으면 음수 */
}

TEST_CASE("delta_in: 집합 밖 변화는 위반")
{
    auto r = scalar(PredRule::delta_in, 4, true);
    r.prediction.deltas = {-1, 1};
    CHECK(violated(check<std::int32_t>(r, 10, 999), "delta_in"));
    CHECK(violated(check<std::int32_t>(r, 10, 8), "delta_in"));
}

TEST_CASE("monotonic non_decreasing: 증가 정상")
{
    auto r = scalar(PredRule::monotonic);
    r.prediction.direction = Direction::non_decreasing;
    CHECK(check<std::uint32_t>(r, 5, 6).ok);
    CHECK(check<std::uint32_t>(r, 5, 105).ok);
}

TEST_CASE("monotonic non_decreasing: 감소는 위반")
{
    auto r = scalar(PredRule::monotonic);
    r.prediction.direction = Direction::non_decreasing;
    CHECK(violated(check<std::uint32_t>(r, 5, 4), "monotonic"));
}

TEST_CASE("monotonic: 부호 있는 값과 나머지 방향")
{
    auto r = scalar(PredRule::monotonic, 4, true);
    r.prediction.direction = Direction::non_increasing;
    CHECK(check<std::int32_t>(r, 0, -3).ok);
    CHECK(violated(check<std::int32_t>(r, -3, 0), "monotonic"));
    r.prediction.direction = Direction::decreasing;
    CHECK(check<std::int32_t>(r, 2, 1).ok);
}

TEST_CASE("immutable: 값이 바뀌면 위반 (바뀌지 않으면 엔진이 부르지 않음)")
{
    auto r = scalar(PredRule::immutable);
    CHECK(violated(check<std::uint32_t>(r, 1, 2), "immutable"));
    CHECK(check<std::uint32_t>(r, 1, 1).ok);  /* 계약상 오지 않지만 같으면 ok */
}

TEST_CASE("per_element_monotonic open_only: 닫힌 칸 열기 정상")
{
    auto r = array_rule(Direction::open_only);
    CHECK(check_bytes(r, {0, 0, 1, 0}, {1, 0, 1, 1}).ok);
}

TEST_CASE("per_element_monotonic open_only: 열린 칸 닫기는 위반")
{
    auto r = array_rule(Direction::open_only);
    CHECK(violated(check_bytes(r, {1, 0, 1, 0}, {1, 1, 0, 0}), "per_element_monotonic"));
}

TEST_CASE("per_element_monotonic: 배열 크기가 요소 크기의 배수가 아니면 위반")
{
    auto r = array_rule(Direction::non_decreasing);
    r.value.element_size = 2;
    CHECK(violated(check_bytes(r, {0, 0, 0}, {1, 0, 0}), "per_element_monotonic"));
}

TEST_CASE("enum_transition: playing→won·playing→lost 정상")
{
    auto r = scalar(PredRule::enum_transition);
    r.prediction.transitions = {{0, 1}, {0, 2}};
    CHECK(check<std::uint32_t>(r, 0, 1).ok);
    CHECK(check<std::uint32_t>(r, 0, 2).ok);
}

TEST_CASE("enum_transition: 허용 안 된 전이는 위반 (won→playing, lost→won, 모르는 코드)")
{
    auto r = scalar(PredRule::enum_transition);
    r.prediction.transitions = {{0, 1}, {0, 2}};
    CHECK(violated(check<std::uint32_t>(r, 1, 0), "enum_transition"));
    CHECK(violated(check<std::uint32_t>(r, 2, 1), "enum_transition"));
    CHECK(violated(check<std::uint32_t>(r, 0, 7), "enum_transition"));
}

TEST_CASE("스칼라 규칙인데 크기가 다르면 위반 (해석 불가)")
{
    auto r = scalar(PredRule::delta_eq, 4);
    r.prediction.delta = 1;
    CHECK(violated(check<std::uint64_t>(r, 1, 2), "delta_eq"));
}

/* ---------- (2) 엔진 경유: game/rules.json ---------- */

namespace {

struct Engine {
    Collector col;
    Engine()
    {
        mtd_config cfg = col.config(10);
        REQUIRE(mtd_init(MTD_GAME_RULES, &cfg) == MTD_OK);
    }
    ~Engine() { mtd_shutdown(); }

    /* 정상: alert 0 (검사 스레드 몇 주기 기다림) */
    void expect_none()
    {
        sleep_ms(60);
        CHECK(col.count() == 0);
    }
    /* 위반: prediction alert 정확히 1 */
    void expect_prediction(const char *name, const char *rule)
    {
        REQUIRE(col.wait_for(1, 500));
        sleep_ms(60);
        auto items = col.items();
        REQUIRE(items.size() == 1);
        CHECK(items[0].name == name);
        CHECK(items[0].kind == MTD_KIND_PREDICTION);
        CHECK(items[0].rule == rule);
    }
};

}  // namespace

TEST_CASE("엔진: timer — tick 안 +1은 alert 0, +5는 prediction delta_eq 1")
{
    Engine e;
    std::uint32_t timer = 0;
    REQUIRE(mtd_watch(&timer, sizeof timer, "timer") == MTD_OK);
    { mtd::Scope s{"tick"}; timer += 1; }
    e.expect_none();
    { mtd::Scope s{"tick"}; timer += 5; }
    e.expect_prediction("timer", "delta_eq");
    CHECK(e.col.items()[0].expected == 1);
    CHECK(e.col.items()[0].actual == 6);
    mtd_unwatch(&timer);
}

TEST_CASE("엔진: mines_remaining — flag 안 -1은 alert 0, 999는 prediction delta_in 1")
{
    Engine e;
    std::int32_t mines = 99;
    REQUIRE(mtd_watch(&mines, sizeof mines, "mines_remaining") == MTD_OK);
    { mtd::Scope s{"flag"}; mines -= 1; }
    e.expect_none();
    { mtd::Scope s{"flag"}; mines = 999; }
    e.expect_prediction("mines_remaining", "delta_in");
    mtd_unwatch(&mines);
}

TEST_CASE("엔진: cells_opened — input 뒤 reveal 안 증가는 alert 0, 감소는 prediction monotonic 1")
{
    Engine e;
    std::uint32_t opened = 10;
    REQUIRE(mtd_watch(&opened, sizeof opened, "cells_opened") == MTD_OK);
    mtd_event("input");
    { mtd::Scope s{"reveal"}; opened += 7; }
    e.expect_none();
    mtd_event("input");
    { mtd::Scope s{"reveal"}; opened -= 1; }
    e.expect_prediction("cells_opened", "monotonic");
    mtd_unwatch(&opened);
}

TEST_CASE("엔진: cell_state — input 뒤 reveal 안 칸 열기는 alert 0, 칸 닫기는 prediction per_element_monotonic 1")
{
    Engine e;
    std::uint8_t cells[480] = {};
    REQUIRE(mtd_watch(cells, sizeof cells, "cell_state") == MTD_OK);
    mtd_event("input");
    { mtd::Scope s{"reveal"}; cells[3] = 1; cells[4] = 1; }
    e.expect_none();
    mtd_event("input");
    { mtd::Scope s{"reveal"}; cells[3] = 0; }
    e.expect_prediction("cell_state", "per_element_monotonic");
    mtd_unwatch(cells);
}

TEST_CASE("엔진: game_state — win-check 안 playing→won은 alert 0, won→playing은 prediction enum_transition 1")
{
    Engine e;
    std::uint32_t state = 0;
    REQUIRE(mtd_watch(&state, sizeof state, "game_state") == MTD_OK);
    { mtd::Scope s{"win-check"}; state = 1; }
    e.expect_none();
    { mtd::Scope s{"win-check"}; state = 0; }
    e.expect_prediction("game_state", "enum_transition");
    mtd_unwatch(&state);
}

TEST_CASE("엔진: immutable — 허용 구간 안에서 바뀌면 prediction immutable 1, 안 바뀌면 0")
{
    /* game/rules.json의 mine_field는 mutable=false라 구간에 들어가지 않는다(아래 테스트).
     * 예측 경로의 immutable은 mutable=true + 허용 tag가 있는 규칙으로 확인한다. */
    RuleSet rs;
    VarRule r;
    r.name = "frozen";
    r.value.size = 4;
    r.behavior.allowed_tags = {"t"};
    r.behavior.event_window_ms = 50;
    r.prediction.rule = PredRule::immutable;
    rs.add(r);
    Collector col;
    mtd_config cfg = col.config(10);
    REQUIRE(test_init_with_rules(rs, &cfg) == MTD_OK);
    std::uint32_t v = 3;
    REQUIRE(mtd_watch(&v, sizeof v, "frozen") == MTD_OK);
    { mtd::Scope s{"t"}; }
    sleep_ms(60);
    CHECK(col.count() == 0);
    { mtd::Scope s{"t"}; v = 4; }
    REQUIRE(col.wait_for(1, 500));
    sleep_ms(60);
    REQUIRE(col.count() == 1);
    CHECK(col.items()[0].kind == MTD_KIND_PREDICTION);
    CHECK(col.items()[0].rule == "immutable");
    mtd_unwatch(&v);
    mtd_shutdown();
}

TEST_CASE("엔진: mine_field(mutable=false)는 구간 안에서 바뀌어도 behavior immutable (11-16 결정 1-3)")
{
    Engine e;
    std::uint8_t field[480] = {};
    field[10] = 1;
    REQUIRE(mtd_watch(field, sizeof field, "mine_field") == MTD_OK);
    { mtd::Scope s{"reveal"}; field[10] = 0; sleep_ms(30); }
    REQUIRE(e.col.wait_for(1, 500));
    sleep_ms(60);
    auto items = e.col.items();
    REQUIRE(items.size() == 1);
    CHECK(items[0].kind == MTD_KIND_BEHAVIOR);
    CHECK(items[0].rule == "immutable");
    mtd_unwatch(field);
}
