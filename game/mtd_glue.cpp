#include "mtd_glue.hpp"

#include <cstring>
#include <stdexcept>

namespace minesweeper {
namespace {

GameState *writable(const GameState *state)
{
    // The game owns the state; libmtd only reads it, but mtd_watch takes void*.
    return const_cast<GameState *>(state);
}

bool is_array_variable(const char *name)
{
    return name != nullptr &&
           (std::strcmp(name, "mine_field") == 0 || std::strcmp(name, "cell_state") == 0);
}

}  // namespace

MtdMonitor::MtdMonitor(const std::string &rules_path, const std::string &alert_log_path,
                       unsigned period_ms)
{
    if (!alert_log_path.empty()) {
        alert_log_ = std::fopen(alert_log_path.c_str(), "w");
        if (alert_log_ == nullptr) {
            throw std::runtime_error("cannot open alert log: " + alert_log_path);
        }
    }
    mtd_config cfg{};
    cfg.period_ms = period_ms;
    cfg.enable_mprotect = 0;
    cfg.on_alert = &MtdMonitor::alert_callback;
    cfg.user = this;
    if (mtd_init(rules_path.empty() ? nullptr : rules_path.c_str(), &cfg) != MTD_OK) {
        if (alert_log_ != nullptr) {
            std::fclose(alert_log_);
        }
        throw std::runtime_error("mtd_init failed (rules: " + rules_path + ")");
    }
    running_ = true;
}

MtdMonitor::~MtdMonitor()
{
    stop();
}

void MtdMonitor::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    mtd_shutdown();  // no alert callback after this returns
    if (alert_log_ != nullptr) {
        if (std::fclose(alert_log_) != 0) {
            log_failed_ = true;
        }
        alert_log_ = nullptr;
    }
}

StateObserver MtdMonitor::observer(StateObserver next)
{
    next_ = next;
    StateObserver o{};
    o.user = this;
    o.state_alloc = &MtdMonitor::state_alloc_cb;
    o.field_watch = &MtdMonitor::field_watch_cb;
    o.state_free = &MtdMonitor::state_free_cb;
    o.scope_begin = &MtdMonitor::scope_begin_cb;
    o.scope_end = &MtdMonitor::scope_end_cb;
    o.event = &MtdMonitor::event_cb;
    return o;
}

/* Called on the libmtd checker thread: count, then one JSONL line (docs/interface.md §2). */
void MtdMonitor::on_alert(const mtd_alert *a)
{
    ++alerts_;
    if (alert_log_ == nullptr || a == nullptr) {
        return;
    }
    char expected[24] = "null";
    char actual[24] = "null";
    if (!is_array_variable(a->name)) {
        std::snprintf(expected, sizeof expected, "\"0x%llx\"", static_cast<unsigned long long>(a->expected));
        std::snprintf(actual, sizeof actual, "\"0x%llx\"", static_cast<unsigned long long>(a->actual));
    }
    std::lock_guard<std::mutex> lock(log_mutex_);
    const int written = std::fprintf(
        alert_log_,
        "{\"detect_ns\":%llu,\"name\":\"%s\",\"kind\":\"%s\",\"rule\":%s%s%s,\"expected\":%s,\"actual\":%s}\n",
        static_cast<unsigned long long>(a->detect_ns), a->name != nullptr ? a->name : "",
        a->kind == MTD_KIND_BEHAVIOR ? "behavior" : "prediction", a->rule != nullptr ? "\"" : "",
        a->rule != nullptr ? a->rule : "null", a->rule != nullptr ? "\"" : "", expected, actual);
    if (written < 0 || std::fflush(alert_log_) != 0) {
        log_failed_ = true;
    }
}

void MtdMonitor::alert_callback(const mtd_alert *alert, void *user)
{
    static_cast<MtdMonitor *>(user)->on_alert(alert);
}

void MtdMonitor::state_alloc_cb(void *user, const GameState *state)
{
    auto *self = static_cast<MtdMonitor *>(user);
    GameState *s = writable(state);
    int failures = 0;
    failures += mtd_watch(&s->timer_seconds, sizeof s->timer_seconds, "timer") != MTD_OK;
    failures += mtd_watch(&s->mines_remaining, sizeof s->mines_remaining, "mines_remaining") != MTD_OK;
    failures += mtd_watch(s->cell_state.data(), sizeof s->cell_state, "cell_state") != MTD_OK;
    failures += mtd_watch(&s->cells_opened, sizeof s->cells_opened, "cells_opened") != MTD_OK;
    failures += mtd_watch(&s->safe_cells, sizeof s->safe_cells, "safe_cells") != MTD_OK;
    failures += mtd_watch(&s->game_state, sizeof s->game_state, "game_state") != MTD_OK;
    self->watch_errors_ += static_cast<std::uint64_t>(failures);
    if (self->next_.state_alloc != nullptr) {
        self->next_.state_alloc(self->next_.user, state);
    }
}

void MtdMonitor::field_watch_cb(void *user, const GameState *state)
{
    auto *self = static_cast<MtdMonitor *>(user);
    GameState *s = writable(state);
    if (mtd_watch(s->mine_field.data(), sizeof s->mine_field, "mine_field") != MTD_OK) {
        ++self->watch_errors_;
    }
    if (self->next_.field_watch != nullptr) {
        self->next_.field_watch(self->next_.user, state);
    }
}

void MtdMonitor::state_free_cb(void *user, const GameState *state)
{
    auto *self = static_cast<MtdMonitor *>(user);
    if (self->next_.state_free != nullptr) {
        self->next_.state_free(self->next_.user, state);
    }
    GameState *s = writable(state);
    mtd_unwatch(&s->timer_seconds);
    mtd_unwatch(&s->mines_remaining);
    mtd_unwatch(s->cell_state.data());
    mtd_unwatch(&s->cells_opened);
    mtd_unwatch(&s->safe_cells);
    mtd_unwatch(&s->game_state);
    if (s->mines_placed) {
        mtd_unwatch(s->mine_field.data());
    }
}

void MtdMonitor::scope_begin_cb(void *, const char *tag)
{
    mtd_begin(tag);
}

void MtdMonitor::scope_end_cb(void *user)
{
    mtd_end();
    ++static_cast<MtdMonitor *>(user)->scopes_;
}

void MtdMonitor::event_cb(void *, const char *name)
{
    mtd_event(name);
}

}  // namespace minesweeper
