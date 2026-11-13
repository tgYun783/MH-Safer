/* test_behavior.cpp — S2-12 행위 기반 탐지(L3) 완료 기준 + S2-13 의미론
 *   1) 구간 안 정상 변경 1,000회 → alert 0
 *   2) 구간 밖 변경 100회(간격 ≥ 2T = 100ms) → behavior alert 100
 *   3) 변조값이 그대로 남아도 alert 1회 (ack)
 * 규칙은 파서(S2-21) 대신 test_init_with_rules로 직접 넣는다.
 */
#include "doctest.h"

#include "engine_test.hpp"
#include "mtd.h"
#include "mtd.hpp"
#include "mtd_test.h"
#include "test_util.hpp"

#include <cstring>

using namespace mtdtest;
using namespace mtd::detail;

namespace {

/* game/rules.json과 같은 모양의 규칙 (지뢰찾기 6종 중 테스트에 쓰는 것) */
RuleSet minesweeper_rules()
{
    RuleSet rs;
    VarRule timer;
    timer.name = "timer";
    timer.value.size = 4;
    timer.behavior.allowed_tags = {"tick"};
    timer.behavior.event_window_ms = 50;
    timer.prediction.rule = PredRule::delta_eq;
    timer.prediction.delta = 1;
    rs.add(timer);

    VarRule mines;
    mines.name = "mines_remaining";
    mines.value.size = 4;
    mines.value.is_signed = true;
    mines.behavior.allowed_tags = {"flag"};
    mines.behavior.event_window_ms = 50;
    mines.prediction.rule = PredRule::delta_in;
    mines.prediction.deltas = {-1, 1};
    rs.add(mines);

    VarRule field;
    field.name = "mine_field";
    field.value.is_array = true;
    field.behavior.is_mutable = false;
    field.behavior.event_window_ms = 50;
    field.prediction.rule = PredRule::immutable;
    rs.add(field);

    VarRule cells;
    cells.name = "cell_state";
    cells.value.is_array = true;
    cells.behavior.allowed_tags = {"reveal"};
    cells.behavior.required_event = "input";
    cells.behavior.event_window_ms = 50;
    cells.prediction.rule = PredRule::per_element_monotonic;
    cells.prediction.direction = Direction::open_only;
    rs.add(cells);
    return rs;
}

/* 예측 검사 가짜 구현: delta_eq만 실제로 본다 (S2-22 전까지 stub 대체) */
PredictionResult fake_predict(const VarRule &r, const std::uint8_t *before, const std::uint8_t *after,
                              std::size_t size) noexcept
{
    PredictionResult res;
    if (r.prediction.rule == PredRule::delta_eq && size == 4) {
        std::uint32_t b, a;
        std::memcpy(&b, before, 4);
        std::memcpy(&a, after, 4);
        if (static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b) != r.prediction.delta) {
            res.ok = false;
            res.rule_id = "delta_eq";
        }
    }
    return res;
}

struct Fixture {
    Collector col;
    Fixture(unsigned period_ms = 0)
    {
        mtd_config cfg = col.config(period_ms);
        REQUIRE(test_init_with_rules(minesweeper_rules(), &cfg) == MTD_OK);
    }
    ~Fixture()
    {
        mtd_shutdown();
        test_set_predictor(nullptr);
    }
};

}  // namespace

TEST_CASE("S2-12 완료 기준 1: 구간 안 정상 변경 1,000회 → alert 0")
{
    Fixture f;  // T = 50ms
    std::uint32_t timer = 0;
    REQUIRE(mtd_watch(&timer, 4, "timer") == MTD_OK);
    for (int i = 0; i < 1000; ++i) {
        mtd::Scope tick{"tick"};
        timer++;
        if (i % 20 == 0) sleep_ms(3);  // 검사 스레드와 구간이 겹치게 섞는다
    }
    sleep_ms(150);  // 3주기
    CHECK(f.col.count() == 0);
    CHECK(timer == 1000u);
    mtd_unwatch(&timer);
}

TEST_CASE("S2-12 완료 기준 2: 구간 밖 변경 100회(간격 100ms) → behavior alert 100")
{
    Fixture f;  // T = 50ms, 간격 2T
    volatile std::uint32_t timer = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&timer), 4, "timer") == MTD_OK);
    for (int i = 0; i < 100; ++i) {
        timer = timer + 7;  // 구간 밖(외부 변조 흉내)
        sleep_ms(100);
    }
    REQUIRE(f.col.wait_for(100, 500));
    sleep_ms(100);
    auto items = f.col.items();
    CHECK(items.size() == 100);
    for (const auto &a : items) {
        CHECK(a.kind == MTD_KIND_BEHAVIOR);
        CHECK(a.name == "timer");
        CHECK(a.rule == "allowed_in_tags");
        CHECK(a.actual == a.expected + 7);
    }
    mtd_unwatch(const_cast<std::uint32_t *>(&timer));
}

TEST_CASE("S2-12 완료 기준 3: 변조값이 그대로 남아도 alert 1회 (ack)")
{
    Fixture f;
    std::uint32_t timer = 5;
    REQUIRE(mtd_watch(&timer, 4, "timer") == MTD_OK);
    timer = 999;
    sleep_ms(500);  // 10주기
    auto items = f.col.items();
    REQUIRE(items.size() == 1);
    CHECK(items[0].expected == 5);
    CHECK(items[0].actual == 999);
    mtd_unwatch(&timer);
}

TEST_CASE("tag가 맞지 않는 구간 안 변화는 구간 밖 변화")
{
    Fixture f(10);
    std::uint32_t timer = 0;
    REQUIRE(mtd_watch(&timer, 4, "timer") == MTD_OK);
    {
        mtd::Scope flag{"flag"};  // timer는 tick만 허용
        timer++;
    }
    REQUIRE(f.col.wait_for(1, 300));
    CHECK(f.col.items()[0].rule == "allowed_in_tags");
    mtd_unwatch(&timer);
}

TEST_CASE("mutable=false 변수(mine_field)는 어떤 구간 안에서 바뀌어도 위반")
{
    Fixture f(10);
    std::uint8_t field[480] = {};
    field[3] = 1;
    REQUIRE(mtd_watch(field, sizeof field, "mine_field") == MTD_OK);
    {
        mtd::Scope r{"reveal"};
        field[7] = 1;
        sleep_ms(40);  // 구간이 열린 동안에도 검사 스레드가 잡아야 한다
    }
    REQUIRE(f.col.wait_for(1, 300));
    auto items = f.col.items();
    CHECK(items.size() == 1);
    CHECK(items[0].rule == "immutable");
    CHECK(items[0].expected == 0);  // 배열은 0
    mtd_unwatch(field);
}

TEST_CASE("구간 시작 전 변조는 mtd_begin에서 잡는다(구간이 덮지 않음)")
{
    Fixture f;  // T = 50ms: 검사보다 begin이 먼저 오게
    std::uint32_t timer = 0;
    REQUIRE(mtd_watch(&timer, 4, "timer") == MTD_OK);
    timer = 500;  // 변조
    {
        mtd::Scope tick{"tick"};  // 곧바로 구간 시작
        timer++;
    }
    REQUIRE(f.col.wait_for(1, 300));
    sleep_ms(100);
    auto items = f.col.items();
    REQUIRE(items.size() == 1);
    CHECK(items[0].kind == MTD_KIND_BEHAVIOR);
    CHECK(items[0].expected == 0);
    CHECK(items[0].actual == 500);
    mtd_unwatch(&timer);
}

TEST_CASE("S2-13 required_event 게이트: 사건 없음 / 오래된 사건 / 직전 사건 / 구간 안 사건")
{
    Fixture f(10);
    std::uint8_t cells[480] = {};
    REQUIRE(mtd_watch(cells, sizeof cells, "cell_state") == MTD_OK);

    SUBCASE("사건 없이 reveal → required_event 위반")
    {
        { mtd::Scope r{"reveal"}; cells[0] = 1; }
        REQUIRE(f.col.wait_for(1, 300));
        CHECK(f.col.items()[0].rule == "required_event");
        CHECK(f.col.items()[0].kind == MTD_KIND_BEHAVIOR);
    }
    SUBCASE("사건이 event_window_ms(50)보다 오래됨 → 위반")
    {
        mtd_event("input");
        sleep_ms(120);
        { mtd::Scope r{"reveal"}; cells[1] = 1; }
        REQUIRE(f.col.wait_for(1, 300));
        CHECK(f.col.items()[0].rule == "required_event");
    }
    SUBCASE("사건 직후 reveal → 합법")
    {
        mtd_event("input");
        { mtd::Scope r{"reveal"}; cells[2] = 1; }
        sleep_ms(100);
        CHECK(f.col.count() == 0);
    }
    SUBCASE("구간 안에서 사건 → 합법")
    {
        { mtd::Scope r{"reveal"}; mtd_event("input"); cells[3] = 1; }
        sleep_ms(100);
        CHECK(f.col.count() == 0);
    }
    SUBCASE("사건 직후라도 구간 밖 변화 → 위반(비동기 수용 없음)")
    {
        mtd_event("input");
        cells[4] = 1;
        REQUIRE(f.col.wait_for(1, 300));
        CHECK(f.col.items()[0].rule == "allowed_in_tags");
    }
    mtd_unwatch(cells);
}

TEST_CASE("mtd_end 예측 검사: 실패하면 prediction alert (검사 스레드가 전달)")
{
    test_set_predictor(&fake_predict);
    Fixture f;
    std::uint32_t timer = 10;
    REQUIRE(mtd_watch(&timer, 4, "timer") == MTD_OK);
    { mtd::Scope t{"tick"}; timer += 1; }  // 정상 +1
    { mtd::Scope t{"tick"}; timer += 5; }  // 위반
    REQUIRE(f.col.wait_for(1, 300));
    sleep_ms(100);
    auto items = f.col.items();
    REQUIRE(items.size() == 1);
    CHECK(items[0].kind == MTD_KIND_PREDICTION);
    CHECK(items[0].rule == "delta_eq");
    CHECK(items[0].expected == 11);
    CHECK(items[0].actual == 16);
    mtd_unwatch(&timer);
}

TEST_CASE("열린 구간 중에 watch한 변수도 그 구간 안으로 들어간다")
{
    Fixture f(10);
    std::uint8_t cells[480] = {};
    std::uint8_t field[480] = {};
    mtd_event("input");
    {
        mtd::Scope r{"reveal"};
        field[9] = 1;  // 지뢰 배치(첫 클릭) — 아직 감시 전
        REQUIRE(mtd_watch(field, sizeof field, "mine_field") == MTD_OK);  // field_watch
        REQUIRE(mtd_watch(cells, sizeof cells, "cell_state") == MTD_OK);
        cells[5] = 1;  // reveal 구간 안 합법 변화
        sleep_ms(30);
    }
    sleep_ms(80);
    CHECK(f.col.count() == 0);  // 첫 클릭 배치에 mine_field alert 0
    mtd_unwatch(field);
    mtd_unwatch(cells);
}

TEST_CASE("중첩 구간: 바깥 구간이 닫힐 때 검사")
{
    test_set_predictor(&fake_predict);
    Fixture f(10);
    std::uint32_t timer = 0;
    REQUIRE(mtd_watch(&timer, 4, "timer") == MTD_OK);
    mtd_begin("tick");
    mtd_begin("tick");
    timer += 1;
    mtd_end();
    timer += 0;
    mtd_end();
    mtd_end();  // 짝 없는 end는 무시
    sleep_ms(60);
    CHECK(f.col.count() == 0);
    mtd_unwatch(&timer);
}

TEST_CASE("규칙 없는 변수: 아무 구간이나 합법, 구간 밖은 rule=null")
{
    Fixture f(10);
    std::uint32_t other = 0;
    REQUIRE(mtd_watch(&other, 4, "unknown_var") == MTD_OK);
    { mtd::Scope s{"anything"}; other = 3; }
    sleep_ms(50);
    CHECK(f.col.count() == 0);
    other = 4;
    REQUIRE(f.col.wait_for(1, 300));
    CHECK(f.col.items()[0].rule.empty());
    mtd_unwatch(&other);
}
