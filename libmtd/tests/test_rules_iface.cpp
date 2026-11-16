#include "doctest.h"

#include "prediction.hpp"
#include "rules.hpp"

#include <cstring>

/* S2-13 내부 인터페이스: 규칙 조회와 예측 검사 호출 계약이 컴파일·링크되는지 확인. */
using namespace mtd::detail;

TEST_CASE("RuleSet: 이름으로 변수 규칙 조회")
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

    VarRule cells;
    cells.name = "cell_state";
    cells.value.is_array = true;
    cells.behavior.allowed_tags = {"reveal"};
    cells.behavior.required_event = "input";
    cells.behavior.event_window_ms = 50;
    cells.prediction.rule = PredRule::per_element_monotonic;
    cells.prediction.direction = Direction::open_only;
    rs.add(cells);

    CHECK(rs.size() == 2);
    const VarRule *t = rs.find("timer");
    REQUIRE(t != nullptr);
    CHECK(t->allows_tag("tick"));
    CHECK_FALSE(t->allows_tag("reveal"));
    CHECK_FALSE(t->allows_tag(nullptr));
    CHECK(t->behavior.required_event.empty());

    const VarRule *c = rs.find("cell_state");
    REQUIRE(c != nullptr);
    CHECK(c->behavior.required_event == "input");
    CHECK(c->value.is_array);

    CHECK(rs.find("nope") == nullptr);
    CHECK(rs.find(nullptr) == nullptr);
}

TEST_CASE("check_prediction 호출 계약 (stub은 항상 ok)")
{
    VarRule r;
    r.name = "timer";
    r.value.size = 4;
    r.prediction.rule = PredRule::delta_eq;
    r.prediction.delta = 1;
    std::uint32_t before = 5, after = 6;
    std::uint8_t b[4], a[4];
    std::memcpy(b, &before, 4);
    std::memcpy(a, &after, 4);
    PredictionResult res = check_prediction(r, b, a, 4);
    (void)res;  // stub: ok. S2-22에서 실제 판정으로 교체되면 그쪽 테스트가 검사한다.
    CHECK(std::strcmp(pred_rule_name(PredRule::delta_eq), "delta_eq") == 0);
    CHECK(pred_rule_name(PredRule::none) == nullptr);
}

TEST_CASE("load_rules: NULL 경로는 빈 규칙, 없는 파일은 실패")
{
    RuleSet rs;
    std::string err;
    CHECK(load_rules(nullptr, rs, err));
    CHECK(rs.empty());
    CHECK_FALSE(load_rules("/nonexistent/rules.json", rs, err));
    CHECK_FALSE(err.empty());
}
