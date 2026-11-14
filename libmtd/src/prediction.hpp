#ifndef MTD_PREDICTION_HPP
#define MTD_PREDICTION_HPP

#include "rules.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace mtd_internal {

struct PredictionResult {
    bool violation = false;
    std::uint64_t expected = 0;
    std::uint64_t actual = 0;
};

PredictionResult check_prediction(const VariableRule &rule,
                                  const std::uint8_t *before,
                                  const std::uint8_t *after,
                                  std::size_t size);

}  // namespace mtd_internal

#endif  // MTD_PREDICTION_HPP
