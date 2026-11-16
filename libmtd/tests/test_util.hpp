/* test_util.hpp — libmtd 엔진 테스트 공용 도구: alert 수집기, 시간 */
#ifndef MTD_TEST_UTIL_HPP
#define MTD_TEST_UTIL_HPP

#include "mtd.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

namespace mtdtest {

inline std::uint64_t now_ns()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

inline void sleep_ms(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Recorded {
    std::string   name;
    mtd_kind      kind;
    std::string   rule;  /* NULL이면 "" */
    std::uint64_t expected;
    std::uint64_t actual;
    std::uint64_t detect_ns;
};

/* 콜백으로 받은 alert를 복사해 쌓는다 (콜백은 검사 스레드 문맥) */
class Collector {
public:
    static void cb(const mtd_alert *a, void *user)
    {
        auto *self = static_cast<Collector *>(user);
        std::lock_guard<std::mutex> lk(self->mu_);
        self->items_.push_back({a->name ? a->name : "", a->kind, a->rule ? a->rule : "",
                                a->expected, a->actual, a->detect_ns});
        self->cv_.notify_all();
    }

    mtd_config config(unsigned period_ms = 0)
    {
        mtd_config c{};
        c.period_ms = period_ms;
        c.on_alert = &Collector::cb;
        c.user = this;
        return c;
    }

    std::size_t count()
    {
        std::lock_guard<std::mutex> lk(mu_);
        return items_.size();
    }

    /* alert가 n개 이상 될 때까지 최대 timeout_ms 대기. 도달하면 true */
    bool wait_for(std::size_t n, unsigned timeout_ms)
    {
        std::unique_lock<std::mutex> lk(mu_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return items_.size() >= n; });
    }

    std::vector<Recorded> items()
    {
        std::lock_guard<std::mutex> lk(mu_);
        return items_;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(mu_);
        items_.clear();
    }

private:
    std::mutex              mu_;
    std::condition_variable cv_;
    std::vector<Recorded>   items_;
};

}  // namespace mtdtest

#endif  // MTD_TEST_UTIL_HPP
