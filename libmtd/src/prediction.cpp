/* prediction.cpp — 예측 기반 탐지(L4, S2-22): mtd_end의 규칙 6종 검사
 *
 * 호출 계약은 prediction.hpp(S2-13). 엔진 락을 잡은 채로 불리므로 순수 함수로 짠다:
 * 할당·락·블로킹 호출·다른 mtd_* 호출 없음. 판정 로직은 PR #15(전유정)를 옮긴 것이다.
 *
 * 스칼라는 little-endian, rule.value.size 바이트, is_signed면 부호 확장(prediction.hpp).
 */
#include "prediction.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mtd::detail {
namespace {

std::uint64_t read_unsigned(const std::uint8_t *p, std::size_t n) noexcept
{
    switch (n) {
    case 1: { std::uint8_t v;  std::memcpy(&v, p, 1); return v; }
    case 2: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
    case 4: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
    case 8: { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
    default: return 0;
    }
}

std::int64_t read_signed(const std::uint8_t *p, std::size_t n) noexcept
{
    switch (n) {
    case 1: { std::int8_t v;  std::memcpy(&v, p, 1); return v; }
    case 2: { std::int16_t v; std::memcpy(&v, p, 2); return v; }
    case 4: { std::int32_t v; std::memcpy(&v, p, 4); return v; }
    case 8: { std::int64_t v; std::memcpy(&v, p, 8); return v; }
    default: return 0;
    }
}

/* after - before를 int64로. 표현할 수 없으면 false(→ 어떤 delta 규칙과도 불일치). */
bool scalar_delta(const VarRule &r, const std::uint8_t *before, const std::uint8_t *after,
                  std::int64_t &delta) noexcept
{
    const std::size_t n = r.value.size;
    if (r.value.is_signed) {
        const std::int64_t b = read_signed(before, n), a = read_signed(after, n);
        if ((b > 0 && a < std::numeric_limits<std::int64_t>::min() + b) ||
            (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b))
            return false;
        delta = a - b;
        return true;
    }
    const std::uint64_t b = read_unsigned(before, n), a = read_unsigned(after, n);
    constexpr std::uint64_t kMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (a >= b) {
        if (a - b > kMax) return false;
        delta = static_cast<std::int64_t>(a - b);
    } else {
        const std::uint64_t m = b - a;
        if (m > kMax + 1) return false;
        delta = (m == kMax + 1) ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(m);
    }
    return true;
}

template <class T>
bool ordered(T before, T after, Direction d) noexcept
{
    switch (d) {
    case Direction::non_decreasing: return after >= before;
    case Direction::non_increasing: return after <= before;
    case Direction::increasing:     return after > before;
    case Direction::decreasing:     return after < before;
    case Direction::open_only:      return before == 0 && after != 0;
    }
    return false;
}

bool check_scalar_monotonic(const VarRule &r, const std::uint8_t *before, const std::uint8_t *after) noexcept
{
    const std::size_t n = r.value.size;
    return r.value.is_signed ? ordered(read_signed(before, n), read_signed(after, n), r.prediction.direction)
                             : ordered(read_unsigned(before, n), read_unsigned(after, n), r.prediction.direction);
}

/* 바뀐 요소마다 방향 규칙 검사. open_only: 닫힘(0) → 열림(≠0)만 허용. */
bool check_per_element(const VarRule &r, const std::uint8_t *before, const std::uint8_t *after,
                       std::size_t size) noexcept
{
    const std::size_t es = r.value.element_size;
    if (es == 0 || size % es != 0) return false;
    for (std::size_t off = 0; off < size; off += es) {
        if (std::memcmp(before + off, after + off, es) == 0) continue;
        if (!ordered(read_unsigned(before + off, es), read_unsigned(after + off, es), r.prediction.direction))
            return false;
    }
    return true;
}

bool check_enum_transition(const VarRule &r, const std::uint8_t *before, const std::uint8_t *after) noexcept
{
    const auto from = read_unsigned(before, r.value.size);
    const auto to   = read_unsigned(after, r.value.size);
    const auto &t   = r.prediction.transitions;
    return std::find(t.begin(), t.end(), std::make_pair(from, to)) != t.end();
}

}  // namespace

PredictionResult check_prediction(const VarRule &rule, const std::uint8_t *before, const std::uint8_t *after,
                                  std::size_t size) noexcept
{
    PredictionResult res;
    if (before == nullptr || after == nullptr || size == 0) return res;
    if (std::memcmp(before, after, size) == 0) return res;  /* 계약상 오지 않지만 안전하게 */

    /* 스칼라 규칙은 규칙 크기와 실제 크기가 같아야 해석할 수 있다(mtd_watch가 보장, #24). */
    const bool scalar_ok = !rule.value.is_array && rule.value.size == size;
    bool ok = true;
    switch (rule.prediction.rule) {
    case PredRule::none:
        return res;
    case PredRule::delta_eq: {
        std::int64_t d = 0;
        ok = scalar_ok && scalar_delta(rule, before, after, d) && d == rule.prediction.delta;
        break;
    }
    case PredRule::delta_in: {
        std::int64_t d = 0;
        const auto &ds = rule.prediction.deltas;
        ok = scalar_ok && scalar_delta(rule, before, after, d) && std::find(ds.begin(), ds.end(), d) != ds.end();
        break;
    }
    case PredRule::monotonic:
        ok = scalar_ok && check_scalar_monotonic(rule, before, after);
        break;
    case PredRule::immutable:
        ok = false;  /* 바뀐 것 자체가 위반 */
        break;
    case PredRule::per_element_monotonic:
        ok = rule.value.is_array && check_per_element(rule, before, after, size);
        break;
    case PredRule::enum_transition:
        ok = scalar_ok && check_enum_transition(rule, before, after);
        break;
    }
    if (!ok) {
        res.ok = false;
        res.rule_id = pred_rule_name(rule.prediction.rule);
    }
    return res;
}

}  // namespace mtd::detail
