/* mtd.cpp — libmtd 공개 API 구현: 섀도 감시 엔진(L1·L2) + 행위 탐지(L3)
 *
 * 설계: docs/monitoring-design.md §2~§4, 규칙 의미론: docs/rules-format.md "S2-13 결정".
 *
 *   감시 테이블  주소 → {크기, 이름, 섀도 사본, 규칙, 구간 깊이}. 한 뮤텍스로 보호.
 *   검사 스레드  주기 T마다 구간 밖 변수의 실제값을 섀도와 바이트 비교.
 *                다르면 behavior alert 후 섀도 = 실제값(ack, 변화 1건당 alert 1건).
 *   mtd_begin    tag가 허용된 변수를 '구간 안'으로. 들어가기 직전에 섀도와 다르면
 *                (지난 검사 이후 구간 밖에서 바뀐 것) behavior alert.
 *   mtd_end      구간 안에서 바뀐 변수마다 required_event 게이트 → 예측 검사 → 섀도 동기화.
 *                여기서 난 alert는 큐에 넣고 검사 스레드가 전달한다(콜백은 항상 검사 스레드 문맥).
 *
 * 메모리 안전: 감시 주소는 락을 잡은 상태에서만 읽는다. mtd_unwatch는 같은 락으로
 * 테이블에서 지우므로, 반환 후 검사 스레드가 그 주소를 읽는 일은 없다.
 * 알림 콜백은 락을 놓고 부른다(콜백이 느려도 게임 스레드를 막지 않게).
 *
 * 경계: 모든 extern "C" 함수는 예외를 밖으로 내보내지 않는다.
 */
#include "mtd.h"
#include "mtd_test.h"

#include "engine_test.hpp"
#include "platform.hpp"
#include "prediction.hpp"
#include "rules.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mtd::detail {
namespace {

/* 감시 변수 1개 */
struct Var {
    void                     *addr = nullptr;
    std::size_t               size = 0;
    std::string               name;
    std::vector<std::uint8_t> shadow;   /* 마지막으로 합법이라고 인정한 값 */
    const VarRule            *rule = nullptr;  /* 없으면 '아무 구간이나 합법' */
    int                       window_depth = 0;
    std::uint64_t             window_begin_ns = 0;
};

/* 열린 구간 1개 (중첩 가능, 스택) */
struct Window {
    std::string   tag;
    std::uint64_t begin_ns = 0;
};

/* 전달 대기 중인 alert. 문자열은 복사해 둔다(감시 해제와 무관하게 유효). */
struct PendingAlert {
    std::string   name;
    mtd_kind      kind = MTD_KIND_BEHAVIOR;
    const char   *rule = nullptr;  /* 정적 문자열 */
    std::uint64_t expected = 0;
    std::uint64_t actual = 0;
    std::uint64_t detect_ns = 0;
    bool          scalar = true;   /* size ≤ 8 */
};

constexpr const char *kRuleAllowedTags  = "allowed_in_tags";
constexpr const char *kRuleImmutable    = "immutable";
constexpr const char *kRuleRequiredEvt  = "required_event";

std::uint64_t to_u64(const std::uint8_t *p, std::size_t size) noexcept
{
    if (size > 8) return 0;  /* 비스칼라: interface.md 공통 규약대로 0(null) */
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < size; ++i)  /* little-endian */
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

class Engine {
public:
    ~Engine() { shutdown(); }

    int init(const RuleSet &rules, const mtd_config *cfg)
    {
        std::unique_lock<std::mutex> lk(mu_);
        if (running_) return MTD_ERR;
        if (cfg != nullptr && cfg->enable_mprotect != 0) {
            std::fprintf(stderr, "[libmtd] enable_mprotect: mprotect backend not implemented yet\n");
            return MTD_ERR;
        }
        cfg_ = cfg ? *cfg : mtd_config{};
        period_ms_ = cfg_.period_ms ? cfg_.period_ms : MTD_DEFAULT_PERIOD_MS;
        rules_ = rules;
        vars_.clear();
        windows_.clear();
        last_event_.clear();
        queue_.clear();
        stop_ = false;
        th_ = std::thread([this] { run(); });
        running_ = true;
        return MTD_OK;
    }

    void shutdown()
    {
        std::thread th;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!running_) return;
            stop_ = true;
            th = std::move(th_);
        }
        cv_.notify_all();
        if (th.joinable()) {
            if (th.get_id() == std::this_thread::get_id())
                th.detach();  /* 콜백 안에서 shutdown — 문서상 금지, 교착만 피한다 */
            else
                th.join();
        }
        std::lock_guard<std::mutex> lk(mu_);
        vars_.clear();
        windows_.clear();
        last_event_.clear();
        queue_.clear();
        rules_ = RuleSet{};
        for (auto &a : allocs_) page_free(a.first, a.second);
        allocs_.clear();
        running_ = false;
    }

    int watch(void *addr, std::size_t size, const char *name)
    {
        if (addr == nullptr || size == 0 || name == nullptr) return MTD_ERR;
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_) return MTD_ERR;
        auto key = reinterpret_cast<std::uintptr_t>(addr);
        if (vars_.count(key) != 0) return MTD_ERR;

        Var v;
        v.addr = addr;
        v.size = size;
        v.name = name;
        v.shadow.resize(size);
        std::memcpy(v.shadow.data(), addr, size);
        v.rule = rules_.find(name);
        /* 이미 열린 구간 중 이 변수를 허용하는 것이 있으면 구간 안으로 등록 */
        for (const auto &w : windows_) {
            if (matches(v, w.tag.c_str())) {
                if (v.window_depth++ == 0) v.window_begin_ns = w.begin_ns;
            }
        }
        vars_.emplace(key, std::move(v));
        return MTD_OK;
    }

    int unwatch(void *addr)
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_) return MTD_ERR;
        return vars_.erase(reinterpret_cast<std::uintptr_t>(addr)) ? MTD_OK : MTD_ERR;
    }

    void begin(const char *tag)
    {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!running_) return;
            Window w;
            w.tag = tag ? tag : "";
            w.begin_ns = now_ns();
            for (auto &kv : vars_) {
                Var &v = kv.second;
                if (!matches(v, w.tag.c_str())) continue;
                if (v.window_depth++ == 0) {
                    /* 구간에 들어가기 전 변화 = 구간 밖 변화. 구간이 덮어쓰지 않게 여기서 잡는다. */
                    notify |= detect_outside(v, w.begin_ns);
                    v.window_begin_ns = w.begin_ns;
                }
            }
            windows_.push_back(std::move(w));
        }
        if (notify) cv_.notify_all();
    }

    void end()
    {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!running_ || windows_.empty()) return;
            Window w = std::move(windows_.back());
            windows_.pop_back();
            std::uint64_t ns = now_ns();
            for (auto &kv : vars_) {
                Var &v = kv.second;
                if (v.window_depth == 0 || !matches(v, w.tag.c_str())) continue;
                if (--v.window_depth == 0) notify |= close_window(v, ns);
            }
        }
        if (notify) cv_.notify_all();
    }

    void event(const char *name)
    {
        if (name == nullptr) return;
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_) return;
        last_event_[name] = now_ns();
    }

    void *alloc(std::size_t size)
    {
        std::size_t mapped = 0;
        void *p = page_alloc(size, &mapped);
        if (p == nullptr) return nullptr;
        std::lock_guard<std::mutex> lk(mu_);
        allocs_.emplace(p, mapped);
        return p;
    }

    void release(void *p)
    {
        if (p == nullptr) return;
        std::lock_guard<std::mutex> lk(mu_);
        auto it = allocs_.find(p);
        if (it == allocs_.end()) return;
        /* 사용자가 unwatch를 빠뜨렸어도 해제된 페이지를 읽지 않게 영역 안 감시를 지운다 */
        auto lo = reinterpret_cast<std::uintptr_t>(p);
        auto hi = lo + it->second;
        for (auto vit = vars_.begin(); vit != vars_.end();) {
            if (vit->first >= lo && vit->first < hi) vit = vars_.erase(vit);
            else ++vit;
        }
        page_free(it->first, it->second);
        allocs_.erase(it);
    }

    std::size_t watch_count()
    {
        std::lock_guard<std::mutex> lk(mu_);
        return running_ ? vars_.size() : 0;
    }

    void set_predictor(PredictFn fn)
    {
        std::lock_guard<std::mutex> lk(mu_);
        predict_ = fn ? fn : &check_prediction;
    }

private:
    /* 이 tag 구간이 변수 v의 변화를 합법으로 만드는가 */
    static bool matches(const Var &v, const char *tag)
    {
        if (v.rule == nullptr) return true;  /* 규칙 없는 변수: 아무 구간이나 합법 */
        if (!v.rule->behavior.is_mutable) return false;
        return v.rule->allows_tag(tag);
    }

    /* 구간 밖 변화 검사. 바뀌었으면 behavior alert를 큐에 넣고 ack. (락 보유 상태) */
    bool detect_outside(Var &v, std::uint64_t ns)
    {
        scratch_.resize(v.size);
        std::memcpy(scratch_.data(), v.addr, v.size);
        if (std::memcmp(scratch_.data(), v.shadow.data(), v.size) == 0) return false;
        const char *rule = nullptr;
        if (v.rule != nullptr)
            rule = v.rule->behavior.is_mutable ? kRuleAllowedTags : kRuleImmutable;
        push_alert(v, MTD_KIND_BEHAVIOR, rule, ns);
        std::memcpy(v.shadow.data(), scratch_.data(), v.size);
        return true;
    }

    /* 구간 종료: required_event 게이트 → 예측 검사 → 섀도 동기화. (락 보유 상태) */
    bool close_window(Var &v, std::uint64_t ns)
    {
        scratch_.resize(v.size);
        std::memcpy(scratch_.data(), v.addr, v.size);
        if (std::memcmp(scratch_.data(), v.shadow.data(), v.size) == 0) return false;
        bool alerted = false;
        if (v.rule != nullptr) {
            const BehaviorSpec &b = v.rule->behavior;
            if (!b.required_event.empty() && !event_ok(b, v.window_begin_ns)) {
                push_alert(v, MTD_KIND_BEHAVIOR, kRuleRequiredEvt, ns);
                alerted = true;
            }
            if (v.rule->prediction.rule != PredRule::none) {
                PredictionResult r = predict_(*v.rule, v.shadow.data(), scratch_.data(), v.size);
                if (!r.ok) {
                    push_alert(v, MTD_KIND_PREDICTION,
                               r.rule_id ? r.rule_id : pred_rule_name(v.rule->prediction.rule), ns);
                    alerted = true;
                }
            }
        }
        std::memcpy(v.shadow.data(), scratch_.data(), v.size);
        return alerted;
    }

    /* 구간 시작 b 기준 event_window_ms 안(또는 구간 안)에 사건이 있었나 */
    bool event_ok(const BehaviorSpec &b, std::uint64_t begin_ns) const
    {
        auto it = last_event_.find(b.required_event);
        if (it == last_event_.end()) return false;
        std::uint64_t window_ns = static_cast<std::uint64_t>(b.event_window_ms) * 1000000ull;
        return it->second + window_ns >= begin_ns;
    }

    /* alert는 섀도 갱신 전에 만든다(expected = 섀도, actual = scratch_) */
    void push_alert(const Var &v, mtd_kind kind, const char *rule, std::uint64_t ns)
    {
        PendingAlert a;
        a.name = v.name;
        a.kind = kind;
        a.rule = rule;
        a.expected = to_u64(v.shadow.data(), v.size);
        a.actual = to_u64(scratch_.data(), v.size);
        a.detect_ns = ns;
        a.scalar = v.size <= 8;
        queue_.push_back(std::move(a));
    }

    void scan()
    {
        std::uint64_t ns = now_ns();
        for (auto &kv : vars_)
            if (kv.second.window_depth == 0) detect_outside(kv.second, ns);
    }

    void deliver(std::vector<PendingAlert> &batch)
    {
        for (const auto &a : batch) {
            mtd_alert out{};
            out.name = a.name.c_str();
            out.kind = a.kind;
            out.rule = a.rule;
            out.expected = a.expected;
            out.actual = a.actual;
            out.detect_ns = a.detect_ns;
            if (cfg_.on_alert != nullptr) {
                cfg_.on_alert(&out, cfg_.user);
            } else {  /* interface.md §2 JSONL. 비스칼라 값은 null */
                char exp_s[24] = "null", act_s[24] = "null";
                if (a.scalar) {
                    std::snprintf(exp_s, sizeof exp_s, "\"0x%llx\"", static_cast<unsigned long long>(a.expected));
                    std::snprintf(act_s, sizeof act_s, "\"0x%llx\"", static_cast<unsigned long long>(a.actual));
                }
                std::fprintf(stderr,
                             "{\"detect_ns\":%llu,\"name\":\"%s\",\"kind\":\"%s\",\"rule\":%s%s%s,"
                             "\"expected\":%s,\"actual\":%s}\n",
                             static_cast<unsigned long long>(a.detect_ns), out.name,
                             a.kind == MTD_KIND_BEHAVIOR ? "behavior" : "prediction",
                             a.rule ? "\"" : "", a.rule ? a.rule : "null", a.rule ? "\"" : "",
                             exp_s, act_s);
            }
        }
        batch.clear();
    }

    /* 검사 스레드 */
    void run()
    {
        using clock = std::chrono::steady_clock;
        std::vector<PendingAlert> batch;
        std::unique_lock<std::mutex> lk(mu_);
        auto next = clock::now() + std::chrono::milliseconds(period_ms_);
        while (!stop_) {
            bool woke = cv_.wait_until(lk, next, [this] { return stop_ || !queue_.empty(); });
            if (stop_) break;
            if (!woke || clock::now() >= next) {
                scan();
                next += std::chrono::milliseconds(period_ms_);
                auto now = clock::now();
                if (next < now) next = now + std::chrono::milliseconds(period_ms_);  /* 밀렸으면 재정렬 */
            }
            if (!queue_.empty()) {
                batch.swap(queue_);
                lk.unlock();
                deliver(batch);
                lk.lock();
            }
        }
        /* 종료 직전 남은 alert(mtd_end에서 난 것) 전달 */
        if (!queue_.empty()) {
            batch.swap(queue_);
            lk.unlock();
            deliver(batch);
        }
    }

    std::mutex                               mu_;
    std::condition_variable                  cv_;
    std::thread                              th_;
    bool                                     running_ = false;
    bool                                     stop_ = false;
    mtd_config                               cfg_{};
    unsigned                                 period_ms_ = MTD_DEFAULT_PERIOD_MS;
    RuleSet                                  rules_;
    PredictFn                                predict_ = &check_prediction;
    std::unordered_map<std::uintptr_t, Var>  vars_;
    std::vector<Window>                      windows_;
    std::unordered_map<std::string, std::uint64_t> last_event_;
    std::vector<PendingAlert>                queue_;
    std::vector<std::uint8_t>                scratch_;  /* 실제값 스냅샷 버퍼(락 보유 중에만 사용) */
    std::unordered_map<void *, std::size_t>  allocs_;   /* mtd_alloc 영역 → 매핑 크기 */
};

Engine &engine()
{
    static Engine e;
    return e;
}

}  // namespace

int test_init_with_rules(const RuleSet &rules, const mtd_config *cfg) noexcept
{
    try {
        return engine().init(rules, cfg);
    } catch (...) {
        return MTD_ERR;
    }
}

void test_set_predictor(PredictFn fn) noexcept
{
    try {
        engine().set_predictor(fn);
    } catch (...) {
    }
}

}  // namespace mtd::detail

using mtd::detail::engine;

extern "C" {

int mtd_init(const char *rules_path, const mtd_config *cfg)
{
    try {
        mtd::detail::RuleSet rules;
        std::string err;
        if (!mtd::detail::load_rules(rules_path, rules, err)) {
            std::fprintf(stderr, "[libmtd] mtd_init: rules load failed: %s\n", err.c_str());
            return MTD_ERR;
        }
        return engine().init(rules, cfg);
    } catch (...) {
        return MTD_ERR;
    }
}

void mtd_shutdown(void)
{
    try {
        engine().shutdown();
    } catch (...) {
    }
}

void *mtd_alloc(size_t size)
{
    try {
        return engine().alloc(size);
    } catch (...) {
        return nullptr;
    }
}

void mtd_free(void *p)
{
    try {
        engine().release(p);
    } catch (...) {
    }
}

int mtd_watch(void *addr, size_t size, const char *name)
{
    try {
        return engine().watch(addr, size, name);
    } catch (...) {
        return MTD_ERR;
    }
}

int mtd_unwatch(void *addr)
{
    try {
        return engine().unwatch(addr);
    } catch (...) {
        return MTD_ERR;
    }
}

void mtd_begin(const char *tag)
{
    try {
        engine().begin(tag);
    } catch (...) {
    }
}

void mtd_end(void)
{
    try {
        engine().end();
    } catch (...) {
    }
}

void mtd_event(const char *name)
{
    try {
        engine().event(name);
    } catch (...) {
    }
}

size_t mtd_test_watch_count(void)
{
    try {
        return engine().watch_count();
    } catch (...) {
        return 0;
    }
}

}  // extern "C"
