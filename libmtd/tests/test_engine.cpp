/* test_engine.cpp — S2-11 감시 엔진 코어(L1·L2) 완료 기준
 *   1) 다른 스레드에서 쓴 변조 100건 중 100건을 100ms 안에 탐지
 *   2) 등록·해제 10만 회 후 감시 테이블 0개
 *   3) (ASan 빌드: test_engine_asan) 해제 후 접근 0건 — 아래 "수명" 케이스가 ASan으로도 돈다
 */
#include "doctest.h"

#include "mtd.h"
#include "mtd.hpp"
#include "mtd_test.h"
#include "test_util.hpp"

#include <atomic>
#include <cstring>
#include <memory>

using namespace mtdtest;

TEST_CASE("초기화: 이중 init 거부, init 전 호출은 실패, shutdown 후 재사용")
{
    long v = 0;
    CHECK(mtd_watch(&v, sizeof v, "v") == MTD_ERR);  // init 전
    CHECK(mtd_test_watch_count() == 0);

    mtd_config cfg{};
    REQUIRE(mtd_init(nullptr, &cfg) == MTD_OK);
    CHECK(mtd_init(nullptr, &cfg) == MTD_ERR);  // 이중 init
    CHECK(mtd_watch(&v, sizeof v, "v") == MTD_OK);
    CHECK(mtd_watch(&v, sizeof v, "v") == MTD_ERR);  // 중복 주소
    CHECK(mtd_watch(nullptr, 4, "x") == MTD_ERR);
    CHECK(mtd_watch(&v, 0, "x") == MTD_ERR);
    CHECK(mtd_watch(&v, 4, nullptr) == MTD_ERR);
    CHECK(mtd_test_watch_count() == 1);
    CHECK(mtd_unwatch(&v) == MTD_OK);
    CHECK(mtd_unwatch(&v) == MTD_ERR);  // 미등록
    mtd_shutdown();
    mtd_shutdown();  // 두 번 불러도 안전

    CHECK(mtd_init(nullptr, nullptr) == MTD_OK);  // cfg NULL = 기본값
    mtd_shutdown();
}

TEST_CASE("init 실패: 없는 규칙 파일, mprotect 미구현")
{
    mtd_config cfg{};
    CHECK(mtd_init("/nonexistent/rules.json", &cfg) == MTD_ERR);
    cfg.enable_mprotect = 1;
    CHECK(mtd_init(nullptr, &cfg) == MTD_ERR);

    mtd::Session bad{"/nonexistent/rules.json", mtd_config{}};
    CHECK_FALSE(bad.ok());
    CHECK_FALSE(static_cast<bool>(bad));
}

TEST_CASE("S2-11 완료 기준 1: 다른 스레드의 변조 100건을 100ms 안에 모두 탐지")
{
    Collector col;
    mtd_config cfg = col.config();  // 기본 T = 50ms
    REQUIRE(mtd_init(nullptr, &cfg) == MTD_OK);

    volatile std::uint32_t target = 0;
    REQUIRE(mtd_watch(const_cast<std::uint32_t *>(&target), sizeof target, "target") == MTD_OK);

    constexpr int N = 100;
    std::vector<std::uint64_t> inject(N, 0);
    std::atomic<int> missed{0};
    std::thread writer([&] {
        for (int i = 0; i < N; ++i) {
            inject[i] = now_ns();          // 쓰기 직전 시각(보수적)
            target = 1000u + static_cast<std::uint32_t>(i);
            if (!col.wait_for(static_cast<std::size_t>(i) + 1, 500)) missed++;
            sleep_ms(5);  // 다음 변조까지 위상을 흩뜨린다
        }
    });
    writer.join();
    mtd_unwatch(const_cast<std::uint32_t *>(&target));
    mtd_shutdown();

    auto items = col.items();
    CHECK(missed.load() == 0);
    REQUIRE(items.size() == static_cast<std::size_t>(N));
    std::uint64_t worst = 0;
    int within = 0;
    for (int i = 0; i < N; ++i) {
        CHECK(items[i].name == "target");
        CHECK(items[i].kind == MTD_KIND_BEHAVIOR);
        CHECK(items[i].actual == 1000u + static_cast<std::uint64_t>(i));
        std::uint64_t lat = items[i].detect_ns - inject[i];
        if (lat <= 100000000ull) within++;
        if (lat > worst) worst = lat;
    }
    MESSAGE("탐지 100ms 이내: " << within << "/" << N << ", 최대 지연 " << worst / 1000000.0 << " ms");
    CHECK(within == N);
}

TEST_CASE("S2-11 완료 기준 2: 등록·해제 10만 회 후 감시 테이블 0개")
{
    mtd_config cfg{};
    cfg.period_ms = 1;  // 검사 스레드가 계속 돌게
    REQUIRE(mtd_init(nullptr, &cfg) == MTD_OK);
    std::uint32_t vars[8] = {};
    for (int i = 0; i < 100000; ++i) {
        std::uint32_t *p = &vars[i % 8];
        REQUIRE(mtd_watch(p, sizeof *p, "v") == MTD_OK);
        REQUIRE(mtd_unwatch(p) == MTD_OK);
    }
    CHECK(mtd_test_watch_count() == 0);
    mtd_shutdown();
    CHECK(mtd_test_watch_count() == 0);
}

/* 지뢰찾기 GameState와 비슷한 크기·모양 (스칼라 + 480바이트 배열 2개) */
struct FakeState {
    std::uint32_t timer;
    std::int32_t  mines_remaining;
    std::uint8_t  mine_field[480];
    std::uint8_t  cell_state[480];
    std::uint32_t cells_opened;
    std::uint32_t game_state;
};

TEST_CASE("수명: 판마다 new/delete + watch/unwatch (ASan 빌드에서 해제 후 접근 0건)")
{
    mtd_config cfg{};
    cfg.period_ms = 1;  // 해제 직후 검사가 끼어들 기회를 최대로
    REQUIRE(mtd_init(nullptr, &cfg) == MTD_OK);
    for (int round = 0; round < 20000; ++round) {
        auto *s = new FakeState{};
        REQUIRE(mtd_watch(&s->timer, 4, "timer") == MTD_OK);
        REQUIRE(mtd_watch(&s->mines_remaining, 4, "mines_remaining") == MTD_OK);
        REQUIRE(mtd_watch(s->cell_state, sizeof s->cell_state, "cell_state") == MTD_OK);
        REQUIRE(mtd_watch(&s->cells_opened, 4, "cells_opened") == MTD_OK);
        REQUIRE(mtd_watch(&s->game_state, 4, "game_state") == MTD_OK);
        CHECK(mtd_test_watch_count() <= 5);
        REQUIRE(mtd_watch(s->mine_field, sizeof s->mine_field, "mine_field") == MTD_OK);
        CHECK(mtd_test_watch_count() <= 6);
        mtd_unwatch(&s->timer);
        mtd_unwatch(&s->mines_remaining);
        mtd_unwatch(s->mine_field);
        mtd_unwatch(s->cell_state);
        mtd_unwatch(&s->cells_opened);
        mtd_unwatch(&s->game_state);
        delete s;
    }
    CHECK(mtd_test_watch_count() == 0);
    mtd_shutdown();
}

TEST_CASE("mtd_alloc/mtd_free: mmap 영역, free가 영역 안 감시를 정리")
{
    mtd_config cfg{};
    cfg.period_ms = 1;
    REQUIRE(mtd_init(nullptr, &cfg) == MTD_OK);
    CHECK(mtd_alloc(0) == nullptr);
    for (int i = 0; i < 2000; ++i) {
        auto *s = static_cast<FakeState *>(mtd_alloc(sizeof(FakeState)));
        REQUIRE(s != nullptr);
        std::memset(s, 0, sizeof *s);
        REQUIRE(mtd_watch(&s->timer, 4, "timer") == MTD_OK);
        REQUIRE(mtd_watch(s->cell_state, sizeof s->cell_state, "cell_state") == MTD_OK);
        if (i % 2 == 0) {  // 정상 순서: unwatch → free
            mtd_unwatch(&s->timer);
            mtd_unwatch(s->cell_state);
        }                  // 홀수: unwatch를 빠뜨려도 free가 정리해야 한다(안 하면 SIGSEGV)
        mtd_free(s);
        CHECK(mtd_test_watch_count() == 0);
    }
    mtd_free(nullptr);
    int on_stack = 0;
    mtd_free(&on_stack);  // 모르는 포인터는 무시
    void *leftover = mtd_alloc(100);
    CHECK(leftover != nullptr);
    mtd_shutdown();  // 남은 영역 반환
}

TEST_CASE("스칼라 값 비교와 배열 바이트 비교")
{
    Collector col;
    mtd_config cfg = col.config(10);
    REQUIRE(mtd_init(nullptr, &cfg) == MTD_OK);

    std::uint64_t big = 0x1122334455667788ull;
    std::uint8_t arr[480] = {};
    REQUIRE(mtd_watch(&big, sizeof big, "big") == MTD_OK);
    REQUIRE(mtd_watch(arr, sizeof arr, "arr") == MTD_OK);

    big = 0x1122334455667789ull;
    REQUIRE(col.wait_for(1, 500));
    arr[479] = 1;  // 마지막 1바이트만 바뀌어도 잡아야 한다
    REQUIRE(col.wait_for(2, 500));
    sleep_ms(50);

    mtd_unwatch(&big);
    mtd_unwatch(arr);
    mtd_shutdown();

    auto items = col.items();
    REQUIRE(items.size() == 2);
    CHECK(items[0].name == "big");
    CHECK(items[0].expected == 0x1122334455667788ull);
    CHECK(items[0].actual == 0x1122334455667789ull);
    CHECK(items[1].name == "arr");
    CHECK(items[1].expected == 0);  // 비스칼라는 0
    CHECK(items[1].actual == 0);
}
