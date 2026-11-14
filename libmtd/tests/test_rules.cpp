#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "mtd.h"
#include "rules.hpp"
#include "testing.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

constexpr const char *rules_path = MTD_TEST_RULES_PATH;

struct CapturedAlert {
    std::string name;
    std::string rule;
    mtd_kind kind;
};

struct AlertCapture {
    std::vector<CapturedAlert> alerts;
};

void on_alert(const mtd_alert *alert, void *user)
{
    auto *capture = static_cast<AlertCapture *>(user);
    capture->alerts.push_back(
        {alert->name, alert->rule == nullptr ? "" : alert->rule, alert->kind});
}

struct Session {
    AlertCapture capture;

    Session()
    {
        mtd_config config{};
        config.period_ms = 10000;
        config.on_alert = on_alert;
        config.user = &capture;
        REQUIRE(mtd_init(rules_path, &config) == MTD_OK);
    }

    ~Session()
    {
        mtd_shutdown();
    }

    bool has_prediction(const char *name, const char *rule) const
    {
        for (const auto &alert : capture.alerts) {
            if (alert.name == name && alert.rule == rule &&
                alert.kind == MTD_KIND_PREDICTION) {
                return true;
            }
        }
        return false;
    }

    bool has_behavior(const char *name) const
    {
        for (const auto &alert : capture.alerts) {
            if (alert.name == name && alert.kind == MTD_KIND_BEHAVIOR) {
                return true;
            }
        }
        return false;
    }
};

bool write_invalid_rules(const nlohmann::json &rules, std::string &path)
{
    path = "/tmp/mtd-rules-" + std::to_string(getpid()) + "-" +
           std::to_string(reinterpret_cast<std::uintptr_t>(&rules)) + ".json";
    std::ofstream output(path);
    output << rules.dump();
    return output.good();
}

nlohmann::json read_rules()
{
    std::ifstream input(rules_path);
    return nlohmann::json::parse(input);
}

}  // namespace

TEST_CASE("규칙 파일이 로드되고 6개 변수 규칙을 조회한다")
{
    const auto rules = mtd_internal::RuleSet::load(rules_path);
    CHECK(rules.detect_window_ms == 200);
    CHECK(rules.default_event_window_ms == 50);
    CHECK(rules.find("timer") != nullptr);
    CHECK(rules.find("mines_remaining") != nullptr);
    CHECK(rules.find("mine_field") != nullptr);
    CHECK(rules.find("cell_state") != nullptr);
    CHECK(rules.find("cells_opened") != nullptr);
    CHECK(rules.find("game_state") != nullptr);
    CHECK(rules.find("missing") == nullptr);
    CHECK(rules.find("cell_state")->accepts_size(480));
    CHECK(rules.find("cell_state")->accepts_size(479));
}

TEST_CASE("mtd_init은 잘못된 규칙 파일 다섯 종류를 거부한다")
{
    const auto source = read_rules();
    std::vector<nlohmann::json> invalid;

    auto missing_field = source;
    missing_field.erase("program");
    invalid.push_back(missing_field);

    auto invalid_type = source;
    invalid_type["variables"][0]["behavior"]["mutable"] = "yes";
    invalid.push_back(invalid_type);

    auto unknown_rule = source;
    unknown_rule["variables"][0]["prediction"]["rule"] = "unrecognized";
    invalid.push_back(unknown_rule);

    auto invalid_delta = source;
    invalid_delta["variables"][0]["prediction"]["params"]["delta"] = "one";
    invalid.push_back(invalid_delta);

    auto bad_enum_transition = source;
    bad_enum_transition["variables"][5]["prediction"]["params"]["allowed"][0][1] =
        "unknown";
    invalid.push_back(bad_enum_transition);

    for (const auto &rules : invalid) {
        std::string path;
        REQUIRE(write_invalid_rules(rules, path));
        CHECK(mtd_init(path.c_str(), nullptr) == MTD_ERR);
        std::remove(path.c_str());
    }
}

TEST_CASE("mtd_init rejects missing rule files and invalid watch names or sizes")
{
    CHECK(mtd_init("/does/not/exist/rules.json", nullptr) == MTD_ERR);
    Session session;
    std::uint32_t timer = 0;
    CHECK(mtd_watch(&timer, sizeof(timer), "unknown") == MTD_ERR);
    CHECK(mtd_watch(&timer, sizeof(timer) - 1, "timer") == MTD_ERR);
    CHECK(mtd_watch(&timer, sizeof(timer), "timer") == MTD_OK);
    CHECK(mtd_unwatch(&timer) == MTD_OK);
    CHECK(mtd_unwatch(&timer) == MTD_ERR);
}

TEST_CASE("100000회 감시 등록·해제 후 테이블이 비어 있다")
{
    Session session;
    std::uint32_t timer = 0;
    bool succeeded = true;
    for (std::size_t i = 0; i < 100000; ++i) {
        if (mtd_watch(&timer, sizeof(timer), "timer") != MTD_OK ||
            mtd_unwatch(&timer) != MTD_OK) {
            succeeded = false;
            break;
        }
    }
    CHECK(succeeded);
    CHECK(mtd_internal::mtd_test_watch_count() == 0);
}

TEST_CASE("delta_eq 정상 변화는 허용한다")
{
    Session session;
    std::uint32_t timer = 4;
    REQUIRE(mtd_watch(&timer, sizeof(timer), "timer") == MTD_OK);
    mtd_begin("tick");
    ++timer;
    mtd_end();
    CHECK(session.capture.alerts.empty());
}

TEST_CASE("delta_eq 위반은 prediction alert를 낸다")
{
    Session session;
    std::uint32_t timer = 4;
    REQUIRE(mtd_watch(&timer, sizeof(timer), "timer") == MTD_OK);
    mtd_begin("tick");
    timer += 2;
    mtd_end();
    CHECK(session.has_prediction("timer", "delta_eq"));
}

TEST_CASE("delta_in 정상 변화는 허용한다")
{
    Session session;
    std::int32_t remaining = 4;
    REQUIRE(mtd_watch(&remaining, sizeof(remaining), "mines_remaining") == MTD_OK);
    mtd_begin("flag");
    --remaining;
    mtd_end();
    CHECK(session.capture.alerts.empty());
}

TEST_CASE("delta_in 위반은 prediction alert를 낸다")
{
    Session session;
    std::int32_t remaining = 4;
    REQUIRE(mtd_watch(&remaining, sizeof(remaining), "mines_remaining") == MTD_OK);
    mtd_begin("flag");
    remaining -= 2;
    mtd_end();
    CHECK(session.has_prediction("mines_remaining", "delta_in"));
}

TEST_CASE("monotonic 정상 변화는 허용한다")
{
    Session session;
    std::uint32_t opened = 3;
    REQUIRE(mtd_watch(&opened, sizeof(opened), "cells_opened") == MTD_OK);
    mtd_event("input");
    mtd_begin("reveal");
    opened += 2;
    mtd_end();
    CHECK(session.capture.alerts.empty());
}

TEST_CASE("monotonic 위반은 prediction alert를 낸다")
{
    Session session;
    std::uint32_t opened = 3;
    REQUIRE(mtd_watch(&opened, sizeof(opened), "cells_opened") == MTD_OK);
    mtd_event("input");
    mtd_begin("reveal");
    --opened;
    mtd_end();
    CHECK(session.has_prediction("cells_opened", "monotonic"));
}

TEST_CASE("required_event는 허용 tag의 시간창 게이트이며 검사 주기와 독립적이다")
{
    Session session;
    std::uint32_t opened = 3;
    REQUIRE(mtd_watch(&opened, sizeof(opened), "cells_opened") == MTD_OK);
    mtd_event("input");
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    mtd_begin("reveal");
    ++opened;
    mtd_end();
    CHECK_FALSE(session.has_prediction("cells_opened", "monotonic"));
    CHECK(session.has_behavior("cells_opened"));
}

TEST_CASE("immutable 값이 유지되면 alert가 없다")
{
    Session session;
    std::array<std::uint8_t, 480> field{};
    REQUIRE(mtd_watch(field.data(), field.size(), "mine_field") == MTD_OK);
    mtd_begin("tick");
    mtd_end();
    CHECK(session.capture.alerts.empty());
}

TEST_CASE("immutable 위반은 prediction alert를 낸다")
{
    Session session;
    std::array<std::uint8_t, 480> field{};
    REQUIRE(mtd_watch(field.data(), field.size(), "mine_field") == MTD_OK);
    mtd_begin("tick");
    field[7] = 1;
    mtd_end();
    CHECK(session.has_prediction("mine_field", "immutable"));
}

TEST_CASE("per_element_monotonic 정상 열림은 허용한다")
{
    Session session;
    std::array<std::uint8_t, 480> cells{};
    REQUIRE(mtd_watch(cells.data(), cells.size(), "cell_state") == MTD_OK);
    mtd_event("input");
    mtd_begin("reveal");
    cells[3] = 1;
    mtd_end();
    CHECK(session.capture.alerts.empty());
}

TEST_CASE("per_element_monotonic 닫힘 위반은 prediction alert를 낸다")
{
    Session session;
    std::array<std::uint8_t, 480> cells{};
    cells[3] = 1;
    REQUIRE(mtd_watch(cells.data(), cells.size(), "cell_state") == MTD_OK);
    mtd_event("input");
    mtd_begin("reveal");
    cells[3] = 0;
    mtd_end();
    CHECK(session.has_prediction("cell_state", "per_element_monotonic"));
}

TEST_CASE("enum_transition 허용 전이는 alert가 없다")
{
    Session session;
    std::uint32_t game_state = 0;
    REQUIRE(mtd_watch(&game_state, sizeof(game_state), "game_state") == MTD_OK);
    mtd_begin("win-check");
    game_state = 1;
    mtd_end();
    CHECK(session.capture.alerts.empty());
}

TEST_CASE("enum_transition 위반 전이는 prediction alert를 낸다")
{
    Session session;
    std::uint32_t game_state = 1;
    REQUIRE(mtd_watch(&game_state, sizeof(game_state), "game_state") == MTD_OK);
    mtd_begin("win-check");
    game_state = 2;
    mtd_end();
    CHECK(session.has_prediction("game_state", "enum_transition"));
}
