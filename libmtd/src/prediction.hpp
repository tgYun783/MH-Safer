/* prediction.hpp — libmtd 내부 인터페이스: mtd_end의 예측 검사 호출 지점 (S2-13)
 *
 * 공개 API가 아니다. 엔진(우진)이 mtd_end에서 '구간 안에서 값이 바뀐 변수'마다 한 번 부르고,
 * 예측 탐지(L4, 전유정 S2-22)가 구현한다.
 *
 * 호출 계약
 *   - 호출 시점: mtd_end, 엔진 락을 잡은 상태. 순수 함수로 짤 것(락·할당·블로킹 호출 금지 권장,
 *     최소한 다른 mtd_* API를 부르면 안 된다 — 교착).
 *   - before: 섀도(구간 시작 시점의 값) 바이트, after: 실제값 스냅샷 바이트. 둘 다 size 바이트.
 *     배열(mine_field·cell_state)도 같은 방식 — 요소별 규칙은 element_size 단위로 비교한다(배열 섀도 접근).
 *   - 스칼라 값 해석: little-endian, rule.value.size 바이트, rule.value.is_signed로 부호 확장.
 *   - 값이 안 바뀐 변수는 부르지 않는다(before == after면 호출 없음).
 *   - 반환 후 엔진이 섀도를 after로 동기화한다(ack). 판정 결과와 무관.
 */
#ifndef MTD_PREDICTION_HPP
#define MTD_PREDICTION_HPP

#include <cstddef>
#include <cstdint>

#include "rules.hpp"

namespace mtd::detail {

struct PredictionResult {
    bool        ok      = true;     /* false면 엔진이 prediction alert 1건을 낸다 */
    const char *rule_id = nullptr;  /* alert.rule에 들어갈 정적 문자열(예: "delta_eq"). 수명: 프로그램 전체 */
};

PredictionResult check_prediction(const VarRule &rule,
                                  const std::uint8_t *before,
                                  const std::uint8_t *after,
                                  std::size_t size) noexcept;

/* 규칙 이름 → 정적 문자열 ("delta_eq" 등). none이면 nullptr. */
const char *pred_rule_name(PredRule r) noexcept;

}  // namespace mtd::detail

#endif  // MTD_PREDICTION_HPP
