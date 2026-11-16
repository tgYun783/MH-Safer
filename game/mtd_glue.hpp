#ifndef MINESWEEPER_MTD_GLUE_HPP
#define MINESWEEPER_MTD_GLUE_HPP

// libmtd integration for the minesweeper game (S3-02).
//
// Maps the game's StateObserver hooks to libmtd:
//   state_alloc  -> mtd_watch x5 (all but mine_field), then the next observer (address log)
//   field_watch  -> mtd_watch mine_field, then the next observer
//   state_free   -> next observer first, then mtd_unwatch (docs/interface.md §3.1 order)
//   scope_begin/scope_end -> mtd_begin/mtd_end, event -> mtd_event
// Alerts go to an optional JSONL file (docs/interface.md §2).

#include "minesweeper.hpp"
#include "mtd.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace minesweeper {

class MtdMonitor {
public:
    // Calls mtd_init. Throws std::runtime_error if libmtd or the alert log cannot start.
    MtdMonitor(const std::string &rules_path, const std::string &alert_log_path,
               unsigned period_ms = 0);
    ~MtdMonitor();  // stop() if not yet stopped. Destroy every Game first.

    // mtd_shutdown and close the alert log. Counters stay readable. Idempotent.
    void stop();

    MtdMonitor(const MtdMonitor &) = delete;
    MtdMonitor &operator=(const MtdMonitor &) = delete;

    // Observer for Game. `next` (e.g. the address logger) is called at the same points.
    StateObserver observer(StateObserver next = {});

    std::uint64_t alerts() const { return alerts_.load(); }
    std::uint64_t scopes() const { return scopes_; }  // mtd_end calls (FP denominator, experiment.md §7)
    std::uint64_t watch_errors() const { return watch_errors_; }
    bool alert_log_failed() const { return log_failed_.load(); }

private:
    StateObserver next_{};
    std::FILE *alert_log_ = nullptr;
    std::mutex log_mutex_;
    std::atomic<std::uint64_t> alerts_{0};
    std::atomic<bool> log_failed_{false};
    std::uint64_t scopes_ = 0;        // game thread only
    std::uint64_t watch_errors_ = 0;  // game thread only
    bool running_ = false;

    void on_alert(const mtd_alert *alert);
    static void alert_callback(const mtd_alert *alert, void *user);

    static void state_alloc_cb(void *user, const GameState *state);
    static void field_watch_cb(void *user, const GameState *state);
    static void state_free_cb(void *user, const GameState *state);
    static void scope_begin_cb(void *user, const char *tag);
    static void scope_end_cb(void *user);
    static void event_cb(void *user, const char *name);
};

}  // namespace minesweeper

#endif
