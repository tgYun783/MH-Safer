#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "mtd.h"
#include "mtd.hpp"

/* S1-01 스모크 테스트: 공개 API가 링크되고 기본 호출이 도는지만 확인.
 * 실제 탐지 동작 테스트는 S2에서 엔진 구현과 함께 추가한다. */
TEST_CASE("mtd C API 링크·기본 호출")
{
    mtd_config cfg{};
    cfg.period_ms = MTD_DEFAULT_PERIOD_MS;
    CHECK(mtd_init(nullptr, &cfg) == MTD_OK);

    long v = 0;
    CHECK(mtd_watch(&v, sizeof v, "v") == MTD_OK);

    mtd_begin("tick");
    v++;
    mtd_end();
    mtd_event("input");

    mtd_shutdown();
    CHECK(v == 1);
}

TEST_CASE("mtd C++ RAII 래퍼")
{
    mtd_config cfg{};
    mtd::Session s{nullptr, cfg};
    int timer = 0;
    CHECK(s.watch(timer, "timer") == MTD_OK);
    {
        mtd::Scope tick{"tick"};  // 소멸자에서 mtd_end
        timer++;
    }
    s.event("input");
    CHECK(timer == 1);
}

TEST_CASE("상수 확인")
{
    CHECK(MTD_OK == 0);
    CHECK(MTD_DEFAULT_PERIOD_MS == 50u);
    CHECK(MTD_KIND_BEHAVIOR == 0);
    CHECK(MTD_KIND_PREDICTION == 1);
}
