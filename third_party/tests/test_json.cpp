// S2-08 확인: 벤더링한 nlohmann/json이 프로젝트 경고 설정에서 빌드되고
// interface.md §3.1 주소 로그 한 줄을 파싱할 수 있다.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <nlohmann/json.hpp>

TEST_CASE("nlohmann/json: 주소 로그(interface.md §3.1) 한 줄 파싱") {
    const char *line =
        R"({"ns":120100000,"event":"field_watch","addr":"0x55d0c3a1e2a0",)"
        R"("fields":[{"name":"mine_field","offset":8,"size":480}]})";
    auto j = nlohmann::json::parse(line);
    CHECK(j.at("event") == "field_watch");
    CHECK(j.at("ns").get<unsigned long long>() == 120100000ULL);
    CHECK(j.at("fields").at(0).at("size") == 480);
}

TEST_CASE("nlohmann/json: 잘못된 입력은 예외(호출 쪽에서 잡아야 함)") {
    CHECK_THROWS_AS(nlohmann::json::parse("{not json"), nlohmann::json::parse_error);
}
