/* mtd.hpp — libmtd C++17 RAII 래퍼 (v0, 헤더 전용)
 * C API(mtd.h)를 감싸 구간/세션을 스코프로 관리한다. plan.md §2의 mtd::Session/Scope.
 */
#ifndef MTD_HPP
#define MTD_HPP

#include "mtd.h"
#include <cstddef>
#include <stdexcept>

namespace mtd {

/* 세션: 생성 시 mtd_init, 소멸 시 mtd_shutdown */
class Session {
public:
    Session(const char *rules_path, const mtd_config &cfg)
    {
        if (mtd_init(rules_path, &cfg) != MTD_OK) {
            throw std::runtime_error("mtd_init failed");
        }
    }
    ~Session() { mtd_shutdown(); }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    int  watch(void *addr, std::size_t size, const char *name) { return mtd_watch(addr, size, name); }
    template <class T>
    int  watch(T &var, const char *name) { return mtd_watch(&var, sizeof(T), name); }
    int  unwatch(void *addr) { return mtd_unwatch(addr); }
    void event(const char *name) { mtd_event(name); }
};

/* 구간: 생성 시 mtd_begin, 소멸 시 mtd_end */
class Scope {
public:
    explicit Scope(const char *tag) { mtd_begin(tag); }
    ~Scope() { mtd_end(); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

}  // namespace mtd

#endif  // MTD_HPP
