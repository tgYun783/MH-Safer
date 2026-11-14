/* rules.hpp — libmtd 내부 인터페이스: 변수별 규칙 조회 (S2-13)
 *
 * 공개 API가 아니다. libmtd/src 안에서만 쓴다(C++ 타입을 써도 된다 — 경계 밖으로 안 나감).
 *
 * 역할 분담
 *   - 규칙 파서(L5, 전유정 S2-21): load_rules()를 구현해 RuleSet을 채운다.
 *   - 감시 엔진·행위 탐지(L1~L3, 우진 S2-11/12): RuleSet::find()로 변수 규칙을 조회한다.
 *   - 예측 탐지(L4, 전유정 S2-22): VarRule::prediction을 읽어 검사한다(prediction.hpp).
 *
 * 필드는 docs/rules-format.md의 스키마 v0과 1:1. 의미론 결정은 같은 문서 "S2-13 결정" 절.
 */
#ifndef MTD_RULES_HPP
#define MTD_RULES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mtd::detail {

/* value.* — 자료형·크기 힌트 */
struct ValueSpec {
    bool        is_array     = false;  /* type == "array" */
    std::size_t size         = 0;      /* 스칼라 바이트 수(type == "scalar") */
    std::size_t element_size = 1;      /* 배열 요소 바이트 수 */
    bool        is_signed    = false;  /* signed */
    /* enum.codes는 prediction.transitions에 코드값으로 풀어 넣는다(파서 담당). */
};

/* behavior.* — 행위(타이밍) 규칙. 엔진(우진)이 검사한다. */
struct BehaviorSpec {
    bool                     is_mutable = true;  /* mutable=false면 어떤 변화든 행위 위반 */
    std::vector<std::string> allowed_tags;       /* 변화가 합법인 구간 tag */
    std::string              required_event;     /* 빈 문자열 = 없음(null) */
    std::uint32_t            event_window_ms = 0; /* 파서가 null이면 default_event_window_ms로 채운다 */
};

enum class PredRule {
    none,                   /* prediction 없음 */
    delta_eq,               /* params.delta */
    delta_in,               /* params.deltas[] */
    monotonic,              /* params.direction (스칼라) */
    immutable,              /* - */
    per_element_monotonic,  /* params.direction (배열 요소별) */
    enum_transition         /* params.allowed[][] → transitions (코드값 쌍) */
};

enum class Direction {
    non_decreasing,
    non_increasing,
    increasing,
    decreasing,
    open_only  /* cell_state: 요소가 닫힘(0) → 열림(≠0)만 허용 */
};

/* prediction.* — 예측(값) 규칙. 예측 탐지(전유정)가 검사한다. */
struct PredictionSpec {
    PredRule                                         rule = PredRule::none;
    std::int64_t                                     delta = 0;   /* delta_eq */
    std::vector<std::int64_t>                        deltas;      /* delta_in */
    Direction                                        direction = Direction::non_decreasing;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> transitions; /* enum_transition: (from, to) 코드값 */
};

/* variables[] 한 항목 */
struct VarRule {
    std::string    name;  /* mtd_watch 이름과 같은 키 */
    ValueSpec      value;
    BehaviorSpec   behavior;
    PredictionSpec prediction;

    /* 이 tag 구간 안 변화가 합법인가 */
    bool allows_tag(const char *tag) const;
};

/* 규칙 파일 전체. 엔진이 mtd_init 시 한 번 받아 읽기 전용으로 쓴다. */
class RuleSet {
public:
    std::string   schema_version;
    std::string   program;
    std::uint32_t detect_window_ms        = 200;
    std::uint32_t default_event_window_ms = 50;

    void add(VarRule rule) { vars_.push_back(std::move(rule)); }

    /* 이름으로 조회. 없으면 nullptr(규칙 없는 변수 → 엔진은 '아무 구간이나 합법'으로 처리). */
    const VarRule *find(const char *name) const;

    std::size_t size() const { return vars_.size(); }
    bool        empty() const { return vars_.empty(); }

private:
    std::vector<VarRule> vars_;
};

/* 규칙 파일 로드 (L5, 전유정 S2-21이 구현).
 *   성공: true, out 채움. 실패: false, err에 사람이 읽을 이유.
 *   예외를 던지지 않는다(noexcept) — mtd_init이 실패를 MTD_ERR로 바꿔 반환한다. */
bool load_rules(const char *path, RuleSet &out, std::string &err) noexcept;

}  // namespace mtd::detail

#endif  // MTD_RULES_HPP
