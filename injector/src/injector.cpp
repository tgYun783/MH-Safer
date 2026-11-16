/* injector.cpp — T2 변조 주입기 코어 (injector.hpp 설명 참고) */
#include "injector.hpp"

#include "address_tracker.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <random>
#include <signal.h>
#include <thread>
#include <utility>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace injector {

std::uint64_t monotonic_ns()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

namespace {

void sleep_ms(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::string hex(std::uint64_t v)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

/* /proc/<pid>/stat 3번째 필드(상태). 읽지 못하면 0. */
char proc_state(const std::string &stat_path)
{
    std::FILE *f = std::fopen(stat_path.c_str(), "r");
    if (f == nullptr) return 0;
    char buf[512];
    std::size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    const char *rp = std::strrchr(buf, ')');  /* comm에 공백·괄호가 있을 수 있어 마지막 ')' 뒤 */
    if (rp == nullptr || rp[1] != ' ') return 0;
    return rp[2];
}

bool target_alive(pid_t pid)
{
    char s = proc_state("/proc/" + std::to_string(pid) + "/stat");
    return s != 0 && s != 'Z' && s != 'X';
}

/* 모든 스레드가 멈췄나(T: stopped, t: tracing stop) */
bool all_threads_stopped(pid_t pid)
{
    std::string dir = "/proc/" + std::to_string(pid) + "/task";
    DIR *d = opendir(dir.c_str());
    if (d == nullptr) return false;
    bool all = true, any = false;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        any = true;
        char s = proc_state(dir + "/" + e->d_name + "/stat");
        if (s != 'T' && s != 't') {
            all = false;
            break;
        }
    }
    closedir(d);
    return any && all;
}

/* SIGSTOP 후 모든 스레드가 멈출 때까지 최대 200ms 대기 */
bool freeze(pid_t pid)
{
    if (kill(pid, SIGSTOP) != 0) return false;
    for (int i = 0; i < 2000; ++i) {
        if (all_threads_stopped(pid)) return true;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return false;
}

void thaw(pid_t pid) { kill(pid, SIGCONT); }

/* 주소 로그를 따라 읽는다. 파일이 아직 없으면 다음에 다시 연다. */
class Tail {
public:
    explicit Tail(std::string path) : path_(std::move(path)) {}
    ~Tail()
    {
        if (fd_ >= 0) close(fd_);
    }
    Tail(const Tail &) = delete;
    Tail &operator=(const Tail &) = delete;

    /* 새로 붙은 내용을 모두 tracker에 넣는다. 형식 오류 줄 수를 돌려준다. */
    std::size_t poll(AddressTracker &t)
    {
        if (fd_ < 0) {
            fd_ = open(path_.c_str(), O_RDONLY);
            if (fd_ < 0) return 0;
        }
        std::size_t bad = 0;
        char buf[65536];
        for (;;) {
            ssize_t n = read(fd_, buf, sizeof buf);
            if (n <= 0) break;
            bad += t.feed(buf, static_cast<std::size_t>(n));
        }
        return bad;
    }

private:
    std::string path_;
    int         fd_ = -1;
};

struct PendingCheck {
    std::uint64_t lifetime_id;
    std::uint64_t inject_ns;
};

}  // namespace

bool run(const Options &opt, std::ostream &labels, Result &res, std::string &err)
{
    res = Result{};
    if (opt.pid <= 0) {
        err = "invalid pid";
        return false;
    }
    std::string mem_path = "/proc/" + std::to_string(opt.pid) + "/mem";
    int mem = open(mem_path.c_str(), O_RDWR);
    if (mem < 0) {
        err = "open " + mem_path + ": " + std::strerror(errno) +
              " (SYS_PTRACE 권한 또는 대상의 부모 프로세스여야 함)";
        return false;
    }

    AddressTracker tracker;
    Tail tail(opt.addr_log);
    std::mt19937 rng(opt.seed);
    std::uniform_int_distribution<unsigned> jitter(0, opt.jitter_ms);
    std::vector<PendingCheck> checks;

    const std::uint64_t start = monotonic_ns();
    const std::uint64_t deadline = opt.duration_sec ? start + opt.duration_sec * 1000000000ull : 0;
    auto schedule = [&] { return monotonic_ns() + (opt.interval_ms + jitter(rng)) * 1000000ull; };
    std::uint64_t next_at = schedule();

    while (res.labels < opt.count) {
        res.bad_log_lines += static_cast<unsigned>(tail.poll(tracker));
        std::uint64_t now = monotonic_ns();
        if (deadline && now >= deadline) break;
        if (!target_alive(opt.pid)) {
            res.target_exited = true;
            break;
        }
        if (now < next_at) {
            std::uint64_t wait = (next_at - now) / 1000000ull;
            sleep_ms(wait > 5 ? 5u : (wait == 0 ? 1u : static_cast<unsigned>(wait)));
            continue;
        }

        /* --- 한 번 시도: 멈춤 → 로그 따라잡기 → 대상 확인 → 읽기 → 쓰기 → 재개 --- */
        if (opt.freeze && !freeze(opt.pid)) {
            thaw(opt.pid);
            ++res.freeze_timeouts;
            sleep_ms(10);
            continue;
        }
        res.bad_log_lines += static_cast<unsigned>(tail.poll(tracker));
        const Lifetime *lt = tracker.current();
        const Field *fd = lt ? tracker.field(opt.scenario.var) : nullptr;
        std::optional<Plan> plan;
        std::uint64_t base = 0;
        if (fd != nullptr) {
            base = lt->addr + fd->offset;
            std::vector<std::uint8_t> cur(fd->size);
            ssize_t n = pread(mem, cur.data(), cur.size(), static_cast<off_t>(base));
            if (n == static_cast<ssize_t>(cur.size())) plan = plan_write(opt.scenario, cur, rng);
        }
        if (!plan) {
            if (opt.freeze) thaw(opt.pid);
            ++res.skipped;  /* 대상 없음(판 사이·배치 전) 또는 지금은 의미 없는 값 → 곧 다시 */
            sleep_ms(10);
            continue;
        }
        const std::uint64_t addr = base + plan->offset;
        const std::uint64_t lifetime_id = lt->id;
        const std::uint64_t inject_ns = monotonic_ns();
        ssize_t w = pwrite(mem, plan->bytes.data(), plan->bytes.size(), static_cast<off_t>(addr));
        if (opt.freeze) thaw(opt.pid);

        if (w != static_cast<ssize_t>(plan->bytes.size())) {
            ++res.write_errors;  /* 라벨을 남기지 않는다(injector/AGENTS.md) */
            std::fprintf(stderr, "[injector] write failed at %s: %s\n", hex(addr).c_str(), std::strerror(errno));
            next_at = schedule();
            continue;
        }

        nlohmann::ordered_json j;  /* docs/interface.md §3 필드 순서 */
        j["inject_ns"] = inject_ns;
        j["name"] = opt.scenario.var;
        j["addr"] = hex(addr);
        j["method"] = "proc";
        j["scenario"] = opt.scenario.label;
        if (plan->scalar) {
            j["from"] = hex(plan->from);
            j["to"] = hex(plan->to);
        } else {
            j["from"] = nullptr;
            j["to"] = nullptr;
        }
        j["hidden"] = opt.scenario.hidden;
        labels << j.dump() << '\n';
        labels.flush();
        ++res.labels;
        tracker.watch(lifetime_id);
        checks.push_back({lifetime_id, inject_ns});
        if (opt.verbose)
            std::fprintf(stderr, "[injector] %u %s %s\n", res.labels, opt.scenario.label.c_str(), hex(addr).c_str());
        next_at = schedule();
    }

    /* 사후 확인: 남은 로그를 읽고 해제된 수명에 쓴 라벨을 센다 */
    sleep_ms(50);
    res.bad_log_lines += static_cast<unsigned>(tail.poll(tracker));
    for (const auto &c : checks) {
        std::uint64_t f = tracker.free_ns(c.lifetime_id);
        if (f != 0 && c.inject_ns >= f) ++res.freed_writes;
    }
    res.order_errors = tracker.order_errors();
    close(mem);
    return true;
}

}  // namespace injector
