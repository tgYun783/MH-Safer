/* address_tracker.hpp — 게임 주소 로그(docs/interface.md §3.1)를 따라 현재 대상 상태를 추적
 *
 * 주입기 규칙(§3.1): 가장 최근 state_alloc의 addr + 알려진 변수 offset에만 쓴다.
 * field_watch 전 mine_field처럼 아직 알려지지 않은 변수엔 쓰지 않는다.
 * state_free를 본 주소엔 쓰지 않는다.
 *
 * 줄 단위로 들어오지 않을 수 있으므로(파일을 따라 읽는 중) feed()는 조각을 받아 '\n'까지 모은다.
 */
#ifndef INJECTOR_ADDRESS_TRACKER_HPP
#define INJECTOR_ADDRESS_TRACKER_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace injector {

struct Field {
    std::size_t offset = 0;
    std::size_t size = 0;
};

/* 살아 있는 상태 구조체 하나(state_alloc ~ state_free) */
struct Lifetime {
    std::uint64_t id = 0;         /* state_alloc마다 1씩 증가 */
    std::uint64_t addr = 0;
    std::size_t   size = 0;
    std::uint64_t alloc_ns = 0;
    std::map<std::string, Field> fields;  /* 지금까지 알려진 변수 (state_alloc + field_watch) */
};

class AddressTracker {
public:
    /* 주소 로그 조각을 넣는다. 완성된 줄마다 처리. 형식 오류 줄 수를 돌려준다. */
    std::size_t feed(const char *data, std::size_t n);
    std::size_t feed(const std::string &s) { return feed(s.data(), s.size()); }

    /* 지금 쓸 수 있는 대상(해제되지 않은 가장 최근 state_alloc). 없으면 nullptr. */
    const Lifetime *current() const { return has_current_ ? &current_ : nullptr; }

    /* current()의 변수 하나. 아직 모르면 nullptr. */
    const Field *field(const std::string &name) const;

    /* 이 수명이 끝났으면(state_free) 그 시각, 아직이면 0. watch()로 등록한 id만 기억한다. */
    void          watch(std::uint64_t lifetime_id) { watched_.insert(lifetime_id); }
    std::uint64_t free_ns(std::uint64_t lifetime_id) const;

    std::uint64_t allocs() const { return next_id_; }
    std::uint64_t frees() const { return frees_; }
    std::uint64_t order_errors() const { return order_errors_; }  /* 열린 수명이 있는데 새 state_alloc 등 */

private:
    bool handle_line(const std::string &line);

    std::string   pending_;
    Lifetime      current_;
    bool          has_current_ = false;
    std::uint64_t next_id_ = 0;
    std::uint64_t frees_ = 0;
    std::uint64_t order_errors_ = 0;
    std::unordered_set<std::uint64_t>                 watched_;
    std::unordered_map<std::uint64_t, std::uint64_t>  freed_at_;  /* watched id → state_free ns */
};

/* "0x55d0c3a1e2a0" → 정수. 실패하면 false. */
bool parse_hex_addr(const std::string &s, std::uint64_t &out);

}  // namespace injector

#endif  // INJECTOR_ADDRESS_TRACKER_HPP
