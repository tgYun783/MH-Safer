/* mtd.cpp — libmtd lifecycle, rule loading, prediction, and shadow monitoring. */
#include "mtd.h"

#include "prediction.hpp"
#include "rules.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/mman.h>

namespace {

using Clock = std::chrono::steady_clock;

struct Watch {
    std::string name;
    std::uint8_t *address;
    std::size_t size;
    std::vector<std::uint8_t> shadow;
};

struct PendingAlert {
    std::string name;
    std::string rule;
    mtd_kind kind;
    std::uint64_t expected;
    std::uint64_t actual;
    std::uint64_t detect_ns;
};

struct Engine {
    std::mutex mutex;
    std::condition_variable changed;
    bool initialized = false;
    bool stopping = false;
    bool scope_active = false;
    std::string active_tag;
    Clock::time_point active_scope_started;
    std::unordered_map<void *, Watch> watches;
    std::unordered_map<std::string, Clock::time_point> events;
    std::unordered_map<void *, std::size_t> allocations;
    mtd_internal::RuleSet rules;
    bool has_rules = false;
    unsigned period_ms = MTD_DEFAULT_PERIOD_MS;
    mtd_alert_cb on_alert = nullptr;
    void *user = nullptr;
    std::thread worker;
};

Engine engine;

std::uint64_t monotonic_ns()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
}

bool tag_allowed(const mtd_internal::VariableRule &rule, const std::string &tag)
{
    return std::find(rule.behavior.allowed_tags.begin(),
                     rule.behavior.allowed_tags.end(),
                     tag) != rule.behavior.allowed_tags.end();
}

bool event_is_recent(const mtd_internal::VariableRule &rule,
                     const Engine &state,
                     Clock::time_point now)
{
    if (!rule.behavior.required_event) {
        return true;
    }
    const auto event = state.events.find(*rule.behavior.required_event);
    if (event == state.events.end() || now < event->second) {
        return false;
    }
    return now - event->second <=
           std::chrono::milliseconds(rule.behavior.event_window_ms);
}

void append_behavior_alert(std::vector<PendingAlert> &alerts,
                           const Watch &watch,
                           const std::vector<std::uint8_t> &shadow,
                           const mtd_internal::VariableRule *rule,
                           const char *reason)
{
    std::uint64_t expected = 0;
    std::uint64_t actual = 0;
    if (rule == nullptr ? watch.size <= sizeof(std::uint64_t)
                        : rule->value.scalar) {
        std::memcpy(&expected, shadow.data(), watch.size);
        std::memcpy(&actual, watch.address, watch.size);
    }
    alerts.push_back({watch.name,
                      rule != nullptr && !rule->behavior.mutable_value
                          ? "immutable"
                          : reason,
                      MTD_KIND_BEHAVIOR,
                      expected,
                      actual,
                      monotonic_ns()});
}

void dispatch_alerts(const std::vector<PendingAlert> &alerts,
                     mtd_alert_cb callback,
                     void *user)
{
    for (const auto &pending : alerts) {
        const mtd_alert alert{pending.name.c_str(),
                              pending.kind,
                              pending.rule.c_str(),
                              pending.expected,
                              pending.actual,
                              pending.detect_ns};
        if (callback != nullptr) {
            callback(&alert, user);
        } else {
            std::fprintf(stderr,
                         "libmtd alert: name=%s kind=%s rule=%s\n",
                         pending.name.c_str(),
                         pending.kind == MTD_KIND_BEHAVIOR ? "behavior" : "prediction",
                         pending.rule.c_str());
        }
    }
}

void monitor_loop()
{
    std::unique_lock<std::mutex> lock(engine.mutex);
    while (!engine.stopping) {
        engine.changed.wait_for(lock, std::chrono::milliseconds(engine.period_ms));
        if (engine.stopping) {
            break;
        }
        if (engine.scope_active) {
            continue;
        }

        const auto now = Clock::now();
        std::vector<PendingAlert> alerts;
        for (auto &entry : engine.watches) {
            auto &watch = entry.second;
            if (std::memcmp(watch.address, watch.shadow.data(), watch.size) == 0) {
                continue;
            }
            const auto *rule = engine.has_rules ? engine.rules.find(watch.name) : nullptr;
            const bool accepted_event =
                rule != nullptr && rule->behavior.mutable_value &&
                rule->behavior.required_event &&
                event_is_recent(*rule, engine, now);
            if (rule == nullptr || !accepted_event) {
                append_behavior_alert(alerts,
                                      watch,
                                      watch.shadow,
                                      rule,
                                      "outside_allowed_window");
            }
            std::memcpy(watch.shadow.data(), watch.address, watch.size);
        }
        const auto callback = engine.on_alert;
        void *const user = engine.user;
        lock.unlock();
        dispatch_alerts(alerts, callback, user);
        lock.lock();
    }
}

void monitor()
{
    try {
        monitor_loop();
    } catch (const std::bad_alloc &error) {
        std::fprintf(stderr, "libmtd: monitor stopped after allocation failure: %s\n",
                     error.what());
    } catch (const std::length_error &error) {
        std::fprintf(stderr, "libmtd: monitor stopped after size failure: %s\n",
                     error.what());
    } catch (const std::system_error &error) {
        std::fprintf(stderr, "libmtd: monitor stopped after synchronization failure: %s\n",
                     error.what());
    }
}

void end_scope()
{
    std::vector<PendingAlert> alerts;
    mtd_alert_cb callback = nullptr;
    void *user = nullptr;
    {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (!engine.initialized || !engine.scope_active) {
            return;
        }
        const auto tag = engine.active_tag;
        const auto scope_started = engine.active_scope_started;
        for (auto &entry : engine.watches) {
            auto &watch = entry.second;
            if (std::memcmp(watch.address, watch.shadow.data(), watch.size) == 0) {
                continue;
            }
            const auto *rule = engine.has_rules ? engine.rules.find(watch.name) : nullptr;
            if (rule != nullptr) {
                const bool allowed =
                    tag_allowed(*rule, tag) &&
                    event_is_recent(*rule, engine, scope_started);
                if (allowed) {
                    const auto prediction = mtd_internal::check_prediction(
                        *rule, watch.shadow.data(), watch.address, watch.size);
                    if (prediction.violation) {
                        alerts.push_back({watch.name,
                                          [&]() {
                                              switch (rule->prediction.kind) {
                                              case mtd_internal::PredictionKind::delta_eq:
                                                  return std::string("delta_eq");
                                              case mtd_internal::PredictionKind::delta_in:
                                                  return std::string("delta_in");
                                              case mtd_internal::PredictionKind::monotonic:
                                                  return std::string("monotonic");
                                              case mtd_internal::PredictionKind::immutable:
                                                  return std::string("immutable");
                                              case mtd_internal::PredictionKind::
                                                  per_element_monotonic:
                                                  return std::string("per_element_monotonic");
                                              case mtd_internal::PredictionKind::enum_transition:
                                                  return std::string("enum_transition");
                                              }
                                              return std::string("unknown");
                                          }(),
                                          MTD_KIND_PREDICTION,
                                          prediction.expected,
                                          prediction.actual,
                                          monotonic_ns()});
                    }
                } else if (rule->prediction.kind ==
                           mtd_internal::PredictionKind::immutable) {
                    const auto prediction = mtd_internal::check_prediction(
                        *rule, watch.shadow.data(), watch.address, watch.size);
                    if (prediction.violation) {
                        alerts.push_back({watch.name,
                                          "immutable",
                                          MTD_KIND_PREDICTION,
                                          prediction.expected,
                                          prediction.actual,
                                          monotonic_ns()});
                    }
                } else {
                    append_behavior_alert(alerts,
                                          watch,
                                          watch.shadow,
                                          rule,
                                          "outside_allowed_tag");
                }
            }
            std::memcpy(watch.shadow.data(), watch.address, watch.size);
        }
        engine.scope_active = false;
        engine.active_tag.clear();
        callback = engine.on_alert;
        user = engine.user;
        engine.changed.notify_all();
    }
    dispatch_alerts(alerts, callback, user);
}

}  // namespace

extern "C" {

int mtd_init(const char *rules_path, const mtd_config *cfg)
{
    mtd_internal::RuleSet loaded_rules;
    const bool load_rules = rules_path != nullptr;
    if (load_rules) {
        try {
            loaded_rules = mtd_internal::RuleSet::load(rules_path);
        } catch (const nlohmann::json::exception &error) {
            std::fprintf(stderr, "libmtd: %s\n", error.what());
            return MTD_ERR;
        } catch (const std::runtime_error &error) {
            std::fprintf(stderr, "libmtd: %s\n", error.what());
            return MTD_ERR;
        } catch (const std::length_error &error) {
            std::fprintf(stderr, "libmtd: %s\n", error.what());
            return MTD_ERR;
        } catch (const std::bad_alloc &error) {
            std::fprintf(stderr, "libmtd: %s\n", error.what());
            return MTD_ERR;
        }
    }

    std::lock_guard<std::mutex> lock(engine.mutex);
    if (engine.initialized) {
        return MTD_ERR;
    }
    engine.stopping = false;
    engine.scope_active = false;
    engine.active_tag.clear();
    engine.watches.clear();
    engine.events.clear();
    engine.allocations.clear();
    engine.rules = std::move(loaded_rules);
    engine.has_rules = load_rules;
    engine.period_ms =
        cfg == nullptr || cfg->period_ms == 0 ? MTD_DEFAULT_PERIOD_MS : cfg->period_ms;
    engine.on_alert = cfg == nullptr ? nullptr : cfg->on_alert;
    engine.user = cfg == nullptr ? nullptr : cfg->user;
    try {
        engine.worker = std::thread(monitor);
    } catch (const std::system_error &error) {
        engine.has_rules = false;
        engine.rules = {};
        std::fprintf(stderr, "libmtd: cannot start monitor thread: %s\n", error.what());
        return MTD_ERR;
    } catch (const std::bad_alloc &error) {
        engine.has_rules = false;
        engine.rules = {};
        std::fprintf(stderr, "libmtd: cannot start monitor thread: %s\n", error.what());
        return MTD_ERR;
    }
    engine.initialized = true;
    return MTD_OK;
}

void mtd_shutdown(void)
{
    {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (!engine.initialized) {
            return;
        }
        engine.stopping = true;
        engine.changed.notify_all();
    }
    if (engine.worker.joinable()) {
        engine.worker.join();
    }
    std::lock_guard<std::mutex> lock(engine.mutex);
    engine.watches.clear();
    engine.events.clear();
    engine.rules = {};
    engine.has_rules = false;
    engine.scope_active = false;
    engine.initialized = false;
    engine.stopping = false;
    engine.on_alert = nullptr;
    engine.user = nullptr;
    for (const auto &allocation : engine.allocations) {
        if (munmap(allocation.first, allocation.second) != 0) {
            std::fprintf(stderr, "libmtd: munmap failed during shutdown\n");
        }
    }
    engine.allocations.clear();
}

void *mtd_alloc(size_t size)
{
    if (size == 0) {
        return nullptr;
    }
    void *memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        return nullptr;
    }
    try {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (!engine.initialized || engine.stopping) {
            munmap(memory, size);
            return nullptr;
        }
        engine.allocations.emplace(memory, size);
    } catch (const std::bad_alloc &error) {
        munmap(memory, size);
        std::fprintf(stderr, "libmtd: cannot track allocation: %s\n", error.what());
        return nullptr;
    } catch (const std::length_error &error) {
        munmap(memory, size);
        std::fprintf(stderr, "libmtd: cannot track allocation: %s\n", error.what());
        return nullptr;
    } catch (const std::system_error &error) {
        munmap(memory, size);
        std::fprintf(stderr, "libmtd: cannot track allocation: %s\n", error.what());
        return nullptr;
    }
    return memory;
}

void mtd_free(void *memory)
{
    if (memory == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(engine.mutex);
    const auto allocation = engine.allocations.find(memory);
    if (allocation == engine.allocations.end()) {
        std::fprintf(stderr, "libmtd: mtd_free called for an unknown allocation\n");
        return;
    }
    if (engine.watches.find(memory) != engine.watches.end()) {
        std::fprintf(stderr, "libmtd: mtd_unwatch must precede mtd_free\n");
        return;
    }
    if (munmap(memory, allocation->second) != 0) {
        std::fprintf(stderr, "libmtd: munmap failed\n");
        return;
    }
    engine.allocations.erase(allocation);
}

int mtd_watch(void *address, size_t size, const char *name)
{
    if (address == nullptr || size == 0 || name == nullptr || *name == '\0') {
        return MTD_ERR;
    }
    try {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (!engine.initialized || engine.stopping ||
            engine.watches.find(address) != engine.watches.end()) {
            return MTD_ERR;
        }
        if (engine.has_rules) {
            const auto *rule = engine.rules.find(name);
            if (rule == nullptr || !rule->accepts_size(size)) {
                return MTD_ERR;
            }
        }
        Watch watch;
        watch.name = name;
        watch.address = static_cast<std::uint8_t *>(address);
        watch.size = size;
        watch.shadow.resize(size);
        std::memcpy(watch.shadow.data(), address, size);
        engine.watches.emplace(address, std::move(watch));
    } catch (const std::bad_alloc &error) {
        std::fprintf(stderr, "libmtd: cannot register watch: %s\n", error.what());
        return MTD_ERR;
    } catch (const std::length_error &error) {
        std::fprintf(stderr, "libmtd: cannot register watch: %s\n", error.what());
        return MTD_ERR;
    } catch (const std::system_error &error) {
        std::fprintf(stderr, "libmtd: cannot register watch: %s\n", error.what());
        return MTD_ERR;
    }
    return MTD_OK;
}

int mtd_unwatch(void *address)
{
    if (address == nullptr) {
        return MTD_ERR;
    }
    std::lock_guard<std::mutex> lock(engine.mutex);
    if (!engine.initialized) {
        return MTD_ERR;
    }
    const auto found = engine.watches.find(address);
    if (found == engine.watches.end()) {
        return MTD_ERR;
    }
    engine.watches.erase(found);
    return MTD_OK;
}

void mtd_begin(const char *tag)
{
    if (tag == nullptr || *tag == '\0') {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (!engine.initialized || engine.scope_active) {
            return;
        }
        engine.active_tag = tag;
        engine.active_scope_started = Clock::now();
        engine.scope_active = true;
    } catch (const std::bad_alloc &error) {
        std::fprintf(stderr, "libmtd: cannot begin scope: %s\n", error.what());
    } catch (const std::length_error &error) {
        std::fprintf(stderr, "libmtd: cannot begin scope: %s\n", error.what());
    } catch (const std::system_error &error) {
        std::fprintf(stderr, "libmtd: cannot begin scope: %s\n", error.what());
    }
}

void mtd_end(void)
{
    try {
        end_scope();
    } catch (const std::bad_alloc &error) {
        std::fprintf(stderr, "libmtd: cannot finish scope: %s\n", error.what());
        std::lock_guard<std::mutex> lock(engine.mutex);
        engine.scope_active = false;
        engine.active_tag.clear();
    } catch (const std::length_error &error) {
        std::fprintf(stderr, "libmtd: cannot finish scope: %s\n", error.what());
        std::lock_guard<std::mutex> lock(engine.mutex);
        engine.scope_active = false;
        engine.active_tag.clear();
    } catch (const std::system_error &error) {
        std::fprintf(stderr, "libmtd: cannot finish scope: %s\n", error.what());
    }
}

void mtd_event(const char *name)
{
    if (name == nullptr || *name == '\0') {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(engine.mutex);
        if (engine.initialized) {
            engine.events[name] = Clock::now();
        }
    } catch (const std::bad_alloc &error) {
        std::fprintf(stderr, "libmtd: cannot record event: %s\n", error.what());
    } catch (const std::length_error &error) {
        std::fprintf(stderr, "libmtd: cannot record event: %s\n", error.what());
    } catch (const std::system_error &error) {
        std::fprintf(stderr, "libmtd: cannot record event: %s\n", error.what());
    }
}

}  // extern "C"

namespace mtd_internal {

std::size_t mtd_test_watch_count()
{
    std::lock_guard<std::mutex> lock(engine.mutex);
    return engine.watches.size();
}

}  // namespace mtd_internal
