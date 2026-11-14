/* prediction_stub.cpp — check_prediction 임시 구현 (S2-13 stub)
 *
 * S2-22(전유정, 예측 탐지)가 이 파일을 실제 구현으로 교체한다.
 * 그 전까지는 모든 변화를 합법으로 본다(prediction alert 없음).
 */
#include "prediction.hpp"

namespace mtd::detail {

PredictionResult check_prediction(const VarRule & /*rule*/,
                                  const std::uint8_t * /*before*/,
                                  const std::uint8_t * /*after*/,
                                  std::size_t /*size*/) noexcept
{
    return PredictionResult{};
}

}  // namespace mtd::detail
