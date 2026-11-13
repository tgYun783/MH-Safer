#ifndef MINESWEEPER_ADDRESS_LOG_HPP
#define MINESWEEPER_ADDRESS_LOG_HPP

#include "minesweeper.hpp"

#include <iosfwd>

namespace minesweeper {

class AddressLogger {
public:
    explicit AddressLogger(std::ostream &output);

    StateObserver observer();
    bool failed() const;

private:
    std::ostream &output_;
    bool failed_ = false;

    void state_alloc(const GameState *state);
    void field_watch(const GameState *state);
    void state_free(const GameState *state);
    void write(const char *event, const GameState *state, bool include_fields);

    static void state_alloc_callback(void *user, const GameState *state);
    static void field_watch_callback(void *user, const GameState *state);
    static void state_free_callback(void *user, const GameState *state);
};

}  // namespace minesweeper

#endif
