/* mtd.hpp — libmtd C++17 RAII 래퍼 (v1, 헤더 전용)
 * C API(mtd.h)를 감싸 구간/세션을 스코프로 관리한다. plan.md §2의 mtd::Session/Scope.
 * 예외를 던지지 않는다. mtd_init 실패는 Session::ok()로 확인한다.
 */
#ifndef MTD_HPP
#define MTD_HPP

#include "mtd.h"
#include <cstddef>

namespace mtd {

/* 세션: 생성 시 mtd_init, 소멸 시 mtd_shutdown(초기화에 성공했을 때만) */
class Session {
public:
    Session(const char *rules_path, const mtd_config &cfg)
        : status_(mtd_init(rules_path, &cfg)) {}
    ~Session() { if (ok()) mtd_shutdown(); }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    /* mtd_init 결과. false면 다른 호출은 모두 MTD_ERR/무시된다. */
    bool ok() const { return status_ == MTD_OK; }
    int  status() const { return status_; }
    explicit operator bool() const { return ok(); }

    int  watch(void *addr, std::size_t size, const char *name) { return mtd_watch(addr, size, name); }
    template <class T>
    int  watch(T &var, const char *name) { return mtd_watch(&var, sizeof(T), name); }

    int  unwatch(void *addr) { return mtd_unwatch(addr); }
    template <class T>
    int  unwatch(T &var) { return mtd_unwatch(&var); }

    void event(const char *name) { mtd_event(name); }

private:
    int status_;
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
