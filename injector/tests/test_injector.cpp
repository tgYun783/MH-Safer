/* test_injector.cpp — S3-11 최소 주입기
 *   단위: 주소 로그 추적(§3.1 규칙), 시나리오 값 계산
 *   통합: 실제 게임(봇, libmtd 켬)을 자식 프로세스로 띄우고 T-timer 100건 주입
 *         → 완료 기준 "라벨 100건, 해제된 주소에 쓴 건수 0" + 라벨마다 W=200ms 안 alert 확인
 * 통합 테스트는 /proc/<child>/mem에 쓴다. Yama ptrace_scope=1에서도 부모는 자식에 쓸 수 있다.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "address_tracker.hpp"
#include "injector.hpp"
#include "scenario.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace injector;

namespace {

const char *kAlloc =
    R"({"ns":100,"event":"state_alloc","addr":"0x1000","size":1472,"fields":[{"name":"timer","offset":0,"size":4},)"
    R"({"name":"mines_remaining","offset":4,"size":4},{"name":"cell_state","offset":488,"size":480},)"
    R"({"name":"cells_opened","offset":968,"size":4},{"name":"game_state","offset":972,"size":4}]})"
    "\n";
const char *kField = R"({"ns":200,"event":"field_watch","addr":"0x1000","fields":[{"name":"mine_field","offset":8,"size":480}]})"
                     "\n";
const char *kFree = R"({"ns":300,"event":"state_free","addr":"0x1000"})"
                    "\n";

}  // namespace

TEST_CASE("주소 로그: state_alloc → field_watch → state_free 따라 대상이 바뀐다")
{
    AddressTracker t;
    CHECK(t.current() == nullptr);
    CHECK(t.feed(kAlloc) == 0);
    REQUIRE(t.current() != nullptr);
    CHECK(t.current()->addr == 0x1000);
    CHECK(t.current()->size == 1472);
    REQUIRE(t.field("timer") != nullptr);
    CHECK(t.field("cells_opened")->offset == 968);
    CHECK(t.field("mine_field") == nullptr);  // 배치 전에는 모른다

    CHECK(t.feed(kField) == 0);
    REQUIRE(t.field("mine_field") != nullptr);
    CHECK(t.field("mine_field")->size == 480);

    t.watch(t.current()->id);
    std::uint64_t id = t.current()->id;
    CHECK(t.free_ns(id) == 0);
    CHECK(t.feed(kFree) == 0);
    CHECK(t.current() == nullptr);  // 해제된 주소엔 쓰지 않는다
    CHECK(t.field("timer") == nullptr);
    CHECK(t.free_ns(id) == 300);
    CHECK(t.order_errors() == 0);
}

TEST_CASE("주소 로그: 줄이 조각나 들어와도 처리, 잘못된 줄은 센다")
{
    AddressTracker t;
    std::string a = kAlloc;
    CHECK(t.feed(a.substr(0, 37)) == 0);
    CHECK(t.current() == nullptr);
    CHECK(t.feed(a.substr(37)) == 0);
    CHECK(t.current() != nullptr);
    CHECK(t.feed("not json\n") == 1);
    CHECK(t.feed(R"({"ns":1,"event":"what","addr":"0x1"})" "\n") == 1);
}

TEST_CASE("주소 로그: 순서 위반은 센다 (free 없이 새 alloc, 다른 주소의 free)")
{
    AddressTracker t;
    t.feed(kAlloc);
    t.feed(kAlloc);
    CHECK(t.order_errors() == 1);
    t.feed(R"({"ns":5,"event":"state_free","addr":"0x2000"})" "\n");
    CHECK(t.order_errors() == 2);
    CHECK(t.current() != nullptr);
}

TEST_CASE("시나리오 파싱: 최소 버전 지원 목록")
{
    std::string err;
    auto s = parse_scenario("T-timer", err);
    REQUIRE(s);
    CHECK(s->type == 'T');
    CHECK(s->var == "timer");
    CHECK_FALSE(s->hidden);
    CHECK(parse_scenario("V-mine_field", err));
    CHECK_FALSE(parse_scenario("H-timer", err));  // S3-12
    CHECK_FALSE(parse_scenario("T-cell_state", err));
    CHECK_FALSE(parse_scenario("bogus", err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("시나리오 값 계산")
{
    std::mt19937 rng(1);
    std::string err;
    auto le32 = [](std::uint32_t v) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                         static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
    };

    auto p = plan_write(*parse_scenario("T-timer", err), le32(41), rng);
    REQUIRE(p);
    CHECK(p->scalar);
    CHECK(p->from == 41);
    CHECK(p->to == 42);
    CHECK(p->bytes == le32(42));

    CHECK_FALSE(plan_write(*parse_scenario("V-timer", err), le32(0), rng));  // 이미 0 → no-op
    p = plan_write(*parse_scenario("V-timer", err), le32(7), rng);
    REQUIRE(p);
    CHECK(p->to == 0);

    p = plan_write(*parse_scenario("T-mines_remaining", err), le32(10), rng);
    REQUIRE(p);
    CHECK((p->to == 9 || p->to == 11));
    p = plan_write(*parse_scenario("T-mines_remaining", err), le32(0), rng);
    REQUIRE(p);
    CHECK((p->to == 1 || p->to == 0xffffffffu));  // -1은 원시 바이트(0xffffffff)

    p = plan_write(*parse_scenario("V-cells_opened", err), le32(5), rng);
    REQUIRE(p);
    CHECK(p->to == 105);

    CHECK_FALSE(plan_write(*parse_scenario("V-game_state", err), le32(1), rng));  // 이미 끝난 판
    p = plan_write(*parse_scenario("V-game_state", err), le32(0), rng);
    REQUIRE(p);
    CHECK(p->to == 1);

    std::vector<std::uint8_t> field(480, 0);
    CHECK_FALSE(plan_write(*parse_scenario("V-mine_field", err), field, rng));  // 지뢰 없음
    field[17] = 1;
    p = plan_write(*parse_scenario("V-mine_field", err), field, rng);
    REQUIRE(p);
    CHECK_FALSE(p->scalar);
    CHECK(p->offset == 17);
    CHECK(p->bytes == std::vector<std::uint8_t>{0});
}

/* ---------- 통합: 실제 게임 ---------- */
namespace {

std::vector<nlohmann::json> read_jsonl(const std::string &path)
{
    std::vector<nlohmann::json> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) out.push_back(nlohmann::json::parse(line));
    return out;
}

bool file_exists(const std::string &p)
{
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

}  // namespace

TEST_CASE("S3-11 완료 기준: 실제 게임에 T-timer 100건 → 라벨 100, 해제된 주소에 쓴 건수 0")
{
    const std::string dir = INJECTOR_TEST_TMP;
    const std::string addr = dir + "/it_addr.jsonl";
    const std::string alerts = dir + "/it_alerts.jsonl";
    std::remove(addr.c_str());
    std::remove(alerts.c_str());

    pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDOUT_FILENO);
        execl(INJECTOR_GAME_BIN, INJECTOR_GAME_BIN, "--bot", "--difficulty", "hard", "--seed", "1",
              "--speed-ms", "50", "--duration-sec", "45", "--addr-log", addr.c_str(), "--alert-log",
              alerts.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    for (int i = 0; i < 500 && !file_exists(addr); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(file_exists(addr));

    Options opt;
    opt.pid = child;
    opt.addr_log = addr;
    std::string err;
    opt.scenario = *parse_scenario("T-timer", err);
    opt.count = 100;
    opt.interval_ms = 250;  // 테스트 시간 단축. W=200ms보다 길어 라벨끼리 alert가 섞이지 않는다
    opt.jitter_ms = 50;
    opt.duration_sec = 40;
    std::ostringstream labels;
    Result r;
    bool ok = run(opt, labels, r, err);

    int status = 0;
    waitpid(child, &status, 0);  // 게임은 --duration-sec 45로 스스로 끝난다(alert 로그 닫힘)
    REQUIRE_MESSAGE(ok, err);

    MESSAGE("labels=" << r.labels << " write_errors=" << r.write_errors << " freed_writes=" << r.freed_writes
                      << " skipped=" << r.skipped << " freeze_timeouts=" << r.freeze_timeouts
                      << " order_errors=" << r.order_errors << " bad_log_lines=" << r.bad_log_lines
                      << " allocs=" << r.allocs);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);  // 변조 중에도 게임이 죽지 않는다
    CHECK(r.labels == 100);
    CHECK(r.freed_writes == 0);
    CHECK(r.write_errors == 0);
    CHECK(r.order_errors == 0);
    CHECK(r.bad_log_lines == 0);
    CHECK(r.allocs >= 2);  // 판 교체(state_free → state_alloc)를 지나며 주입했다

    /* 라벨 형식(§3)과 탐지 확인: 같은 name의 alert가 [inject_ns, inject_ns + 200ms] 안에 */
    std::vector<nlohmann::json> lab;
    std::istringstream in(labels.str());
    std::string line;
    while (std::getline(in, line)) lab.push_back(nlohmann::json::parse(line));
    REQUIRE(lab.size() == 100);
    auto al = read_jsonl(alerts);
    int matched = 0;
    for (const auto &l : lab) {
        CHECK(l["name"] == "timer");
        CHECK(l["scenario"] == "T-timer");
        CHECK(l["method"] == "proc");
        CHECK(l["hidden"] == false);
        CHECK(l["from"].is_string());
        const std::uint64_t t = l["inject_ns"].get<std::uint64_t>();
        for (const auto &a : al) {
            const std::uint64_t d = a["detect_ns"].get<std::uint64_t>();
            if (a["name"] == "timer" && d >= t && d <= t + 200000000ull) {
                ++matched;
                break;
            }
        }
    }
    MESSAGE("탐지(W=200ms 안 alert): " << matched << "/100, alert 총 " << al.size());
    CHECK(matched >= 95);
}
