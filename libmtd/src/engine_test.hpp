/* engine_test.hpp — 엔진 단위 테스트용 내부 훅 (공개 API 아님)
 *
 * 파서(S2-21)·예측 검사(S2-22)가 아직 stub이어도 행위 탐지를 테스트할 수 있게
 * 규칙을 코드로 넣고 예측 검사 함수를 바꿔 끼운다.
 */
#ifndef MTD_ENGINE_TEST_HPP
#define MTD_ENGINE_TEST_HPP

#include "mtd.h"
#include "prediction.hpp"
#include "rules.hpp"

namespace mtd::detail {

using PredictFn = PredictionResult (*)(const VarRule &, const std::uint8_t *,
                                       const std::uint8_t *, std::size_t) noexcept;

/* mtd_init과 같지만 규칙 파일 대신 RuleSet을 직접 받는다. */
int test_init_with_rules(const RuleSet &rules, const mtd_config *cfg) noexcept;

/* 예측 검사 함수 교체. nullptr이면 기본(check_prediction)으로 되돌린다.
 * mtd_init 전후 아무 때나 가능, mtd_shutdown 후에도 유지된다. */
void test_set_predictor(PredictFn fn) noexcept;

}  // namespace mtd::detail

#endif  // MTD_ENGINE_TEST_HPP
