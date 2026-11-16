/* test_rules.cpp — S2-21 규칙 파서 완료 기준
 *   1) game/rules.json 로드 성공
 *   2) 6개 변수 규칙 조회
 *   3) 잘못된 파일 5종 이상 거부 (mtd_init은 MTD_ERR, 예외가 경계를 넘지 않음)
 */
#include "doctest.h"

#include "mtd.h"
#include "rules.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace mtd::detail;

namespace {

/* 최소 유효 규칙 파일. 변형해서 잘못된 파일을 만든다. */
const char *kValidVar = R"({"name":"v","value":{"type":"scalar","size":4,"signed":false,"encoding":"uint","compare":"value"},
 "behavior":{"mutable":true,"allowed_in_tags":["t"],"required_event":null,"event_window_ms":null},
 "prediction":{"rule":"delta_eq","params":{"delta":1}}})";

std::string wrap(const std::string &vars, const char *version = "\"0\"")
{
    return std::string(R"({"schema_version":)") + version +
           R"(,"program":"p","detect_window_ms":200,"default_event_window_ms":50,"variables":[)" + vars + "]}";
}

std::string write_tmp(const char *name, const std::string &body)
{
    std::string path = std::string(MTD_TEST_TMP) + "/" + name + ".json";
    std::ofstream(path) << body;
    return path;
}

std::string replace(std::string s, const std::string &from, const std::string &to)
{
    auto pos = s.find(from);
    REQUIRE(pos != std::string::npos);
    return s.replace(pos, from.size(), to);
}

}  // namespace

TEST_CASE("S2-21 완료 기준 1·2: game/rules.json 로드, 6개 변수 규칙 조회")
{
    RuleSet rs;
    std::string err;
    REQUIRE_MESSAGE(load_rules(MTD_GAME_RULES, rs, err), err);
    CHECK(rs.schema_version == "0");
    CHECK(rs.program == "minesweeper");
    CHECK(rs.detect_window_ms == 200);
    CHECK(rs.default_event_window_ms == 50);
    CHECK(rs.size() == 6);

    const VarRule *timer = rs.find("timer");
    REQUIRE(timer != nullptr);
    CHECK_FALSE(timer->value.is_array);
    CHECK(timer->value.size == 4);
    CHECK_FALSE(timer->value.is_signed);
    CHECK(timer->allows_tag("tick"));
    CHECK(timer->behavior.required_event.empty());
    CHECK(timer->behavior.event_window_ms == 50);  /* null → default */
    CHECK(timer->prediction.rule == PredRule::delta_eq);
    CHECK(timer->prediction.delta == 1);

    const VarRule *mines = rs.find("mines_remaining");
    REQUIRE(mines != nullptr);
    CHECK(mines->value.is_signed);
    CHECK(mines->allows_tag("flag"));
    CHECK(mines->prediction.rule == PredRule::delta_in);
    CHECK(mines->prediction.deltas == std::vector<std::int64_t>{-1, 1});

    const VarRule *field = rs.find("mine_field");
    REQUIRE(field != nullptr);
    CHECK(field->value.is_array);
    CHECK(field->value.element_size == 1);
    CHECK_FALSE(field->behavior.is_mutable);
    CHECK(field->behavior.allowed_tags.empty());
    CHECK(field->prediction.rule == PredRule::immutable);

    const VarRule *cells = rs.find("cell_state");
    REQUIRE(cells != nullptr);
    CHECK(cells->value.is_array);
    CHECK(cells->allows_tag("reveal"));
    CHECK(cells->behavior.required_event == "input");
    CHECK(cells->behavior.event_window_ms == 50);
    CHECK(cells->prediction.rule == PredRule::per_element_monotonic);
    CHECK(cells->prediction.direction == Direction::open_only);

    const VarRule *opened = rs.find("cells_opened");
    REQUIRE(opened != nullptr);
    CHECK(opened->behavior.required_event == "input");
    CHECK(opened->prediction.rule == PredRule::monotonic);
    CHECK(opened->prediction.direction == Direction::non_decreasing);

    const VarRule *state = rs.find("game_state");
    REQUIRE(state != nullptr);
    CHECK(state->allows_tag("win-check"));
    CHECK(state->prediction.rule == PredRule::enum_transition);
    using T = std::pair<std::uint64_t, std::uint64_t>;
    CHECK(state->prediction.transitions == std::vector<T>{{0, 1}, {0, 2}});  /* playing→won, playing→lost */
}

TEST_CASE("유효한 최소 파일은 로드된다 (잘못된 파일 테스트의 기준)")
{
    RuleSet rs;
    std::string err;
    CHECK_MESSAGE(load_rules(write_tmp("valid", wrap(kValidVar)).c_str(), rs, err), err);
    CHECK(rs.size() == 1);
}

TEST_CASE("S2-21 완료 기준 3: 잘못된 규칙 파일 거부 → mtd_init MTD_ERR")
{
    const std::string v = kValidVar;
    struct Case { const char *name; std::string body; };
    const Case cases[] = {
        {"json_syntax",        wrap(v) + "}"},                                          /* JSON 문법 오류 */
        {"missing_field",      wrap(replace(v, R"("compare":"value")", R"("x":1)"))},  /* 필드 누락 */
        {"type_error",         wrap(replace(v, R"("delta":1)", R"("delta":"1")"))},     /* 타입 오류 */
        {"unknown_rule",       wrap(replace(v, "delta_eq", "delta_maybe"))},            /* 모르는 규칙 */
        {"bad_version",        wrap(v, "\"9\"")},                                       /* 스키마 버전 */
        {"duplicate_name",     wrap(v + "," + v)},                                      /* 이름 중복 */
        {"bad_size",           wrap(replace(v, R"("size":4)", R"("size":3)"))},         /* 크기 */
        {"rule_type_mismatch", wrap(replace(v,                                          /* 배열에 delta_eq */
                                    R"("type":"scalar","size":4,"signed":false,"encoding":"uint","compare":"value")",
                                    R"("type":"array","element_size":1,"count":null,"encoding":"uint","compare":"bytes")"))},
        {"empty_variables",    wrap("")},
        {"enum_unknown_state", wrap(replace(replace(replace(v, R"("encoding":"uint")",
                                    R"("encoding":"enum","enum":{"states":["a","b"],"codes":[0,1]})"),
                                    "delta_eq", "enum_transition"),
                                    R"({"delta":1})", R"({"allowed":[["a","c"]]})"))},
    };
    for (const auto &c : cases) {
        CAPTURE(c.name);
        RuleSet rs;
        std::string err;
        const std::string path = write_tmp(c.name, c.body);
        CHECK_FALSE(load_rules(path.c_str(), rs, err));
        CHECK_FALSE(err.empty());
        CHECK(rs.empty());

        mtd_config cfg{};
        CHECK(mtd_init(path.c_str(), &cfg) == MTD_ERR);
        mtd_shutdown();  /* 실패한 init 뒤 shutdown도 안전해야 한다 */
    }
}

TEST_CASE("없는 파일·NULL 경로")
{
    RuleSet rs;
    std::string err;
    CHECK_FALSE(load_rules("/nonexistent/rules.json", rs, err));
    CHECK(load_rules(nullptr, rs, err));
    CHECK(rs.empty());
}

TEST_CASE("mtd_init: game/rules.json으로 초기화 성공")
{
    mtd_config cfg{};
    CHECK(mtd_init(MTD_GAME_RULES, &cfg) == MTD_OK);
    mtd_shutdown();
}
