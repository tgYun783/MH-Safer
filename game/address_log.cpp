#include "address_log.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace minesweeper {
namespace {

std::uint64_t monotonic_ns()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string address_string(const void *address)
{
    std::ostringstream result;
    result << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(address);
    return result.str();
}

nlohmann::json state_fields()
{
    return nlohmann::json::array(
        {{{"name", "timer"},
          {"offset", offsetof(GameState, timer_seconds)},
          {"size", sizeof(GameState::timer_seconds)}},
         {{"name", "mines_remaining"},
          {"offset", offsetof(GameState, mines_remaining)},
          {"size", sizeof(GameState::mines_remaining)}},
         {{"name", "cell_state"},
          {"offset", offsetof(GameState, cell_state)},
          {"size", sizeof(GameState::cell_state)}},
         {{"name", "cells_opened"},
          {"offset", offsetof(GameState, cells_opened)},
          {"size", sizeof(GameState::cells_opened)}},
         {{"name", "game_state"},
          {"offset", offsetof(GameState, game_state)},
          {"size", sizeof(GameState::game_state)}}});
}

nlohmann::json field_watch_fields()
{
    return nlohmann::json::array(
        {{{"name", "mine_field"},
          {"offset", offsetof(GameState, mine_field)},
          {"size", sizeof(GameState::mine_field)}}});
}

}  // namespace

AddressLogger::AddressLogger(std::ostream &output) : output_(output) {}

StateObserver AddressLogger::observer()
{
    return {this,
            state_alloc_callback,
            field_watch_callback,
            state_free_callback};
}

bool AddressLogger::failed() const
{
    return failed_;
}

void AddressLogger::state_alloc(const GameState *state)
{
    write("state_alloc", state, true);
}

void AddressLogger::field_watch(const GameState *state)
{
    write("field_watch", state, true);
}

void AddressLogger::state_free(const GameState *state)
{
    write("state_free", state, false);
}

void AddressLogger::write(const char *event,
                          const GameState *state,
                          bool include_fields)
{
    nlohmann::json record{
        {"ns", monotonic_ns()},
        {"event", event},
        {"addr", address_string(state)}};
    if (include_fields) {
        if (event[0] == 's') {
            record["size"] = sizeof(GameState);
            record["fields"] = state_fields();
        } else {
            record["fields"] = field_watch_fields();
        }
    }
    output_ << record.dump() << '\n';
    output_.flush();
    failed_ = failed_ || !output_;
}

void AddressLogger::state_alloc_callback(void *user, const GameState *state)
{
    static_cast<AddressLogger *>(user)->state_alloc(state);
}

void AddressLogger::field_watch_callback(void *user, const GameState *state)
{
    static_cast<AddressLogger *>(user)->field_watch(state);
}

void AddressLogger::state_free_callback(void *user, const GameState *state)
{
    static_cast<AddressLogger *>(user)->state_free(state);
}

}  // namespace minesweeper
