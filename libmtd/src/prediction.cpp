#include "prediction.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mtd_internal {
namespace {

std::uint64_t read_unsigned(const std::uint8_t *bytes, std::size_t size)
{
    std::uint64_t value = 0;
    switch (size) {
    case 1: {
        std::uint8_t part = 0;
        std::memcpy(&part, bytes, sizeof(part));
        value = part;
        break;
    }
    case 2: {
        std::uint16_t part = 0;
        std::memcpy(&part, bytes, sizeof(part));
        value = part;
        break;
    }
    case 4: {
        std::uint32_t part = 0;
        std::memcpy(&part, bytes, sizeof(part));
        value = part;
        break;
    }
    case 8:
        std::memcpy(&value, bytes, sizeof(value));
        break;
    default:
        break;
    }
    return value;
}

std::int64_t read_signed(const std::uint8_t *bytes, std::size_t size)
{
    switch (size) {
    case 1: {
        std::int8_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    case 2: {
        std::int16_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    case 4: {
        std::int32_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    case 8: {
        std::int64_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

std::uint64_t read_scalar(const VariableRule &rule, const std::uint8_t *bytes)
{
    return read_unsigned(bytes, rule.value.size);
}

bool valid_delta(std::int64_t from, std::int64_t to, std::int64_t &delta)
{
    if ((from > 0 && to < std::numeric_limits<std::int64_t>::min() + from) ||
        (from < 0 && to > std::numeric_limits<std::int64_t>::max() + from)) {
        return false;
    }
    delta = to - from;
    return true;
}

bool signed_monotonic(std::int64_t before,
                      std::int64_t after,
                      const std::string &direction)
{
    if (direction == "non_decreasing") {
        return after >= before;
    }
    if (direction == "non_increasing") {
        return after <= before;
    }
    if (direction == "increasing") {
        return after > before;
    }
    return after < before;
}

bool unsigned_monotonic(std::uint64_t before,
                        std::uint64_t after,
                        const std::string &direction)
{
    if (direction == "non_decreasing") {
        return after >= before;
    }
    if (direction == "non_increasing") {
        return after <= before;
    }
    if (direction == "increasing") {
        return after > before;
    }
    return after < before;
}

bool scalar_monotonic(const VariableRule &rule,
                      const std::uint8_t *before,
                      const std::uint8_t *after,
                      const std::string &direction)
{
    return rule.value.is_signed
               ? signed_monotonic(read_signed(before, rule.value.size),
                                  read_signed(after, rule.value.size),
                                  direction)
               : unsigned_monotonic(read_scalar(rule, before),
                                    read_scalar(rule, after),
                                    direction);
}

}  // namespace

PredictionResult check_prediction(const VariableRule &rule,
                                  const std::uint8_t *before,
                                  const std::uint8_t *after,
                                  std::size_t size)
{
    PredictionResult result;
    if (std::memcmp(before, after, size) == 0) {
        return result;
    }
    result.expected = rule.value.scalar ? read_scalar(rule, before) : 0;
    result.actual = rule.value.scalar ? read_scalar(rule, after) : 0;

    switch (rule.prediction.kind) {
    case PredictionKind::delta_eq:
    case PredictionKind::delta_in: {
        std::int64_t delta = 0;
        bool matches = false;
        if (rule.value.is_signed) {
            matches = valid_delta(read_signed(before, size),
                                  read_signed(after, size),
                                  delta);
        } else {
            const auto old_value = read_scalar(rule, before);
            const auto new_value = read_scalar(rule, after);
            if (new_value >= old_value &&
                new_value - old_value <=
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                delta = static_cast<std::int64_t>(new_value - old_value);
                matches = true;
            } else if (old_value > new_value &&
                       old_value - new_value <= (std::uint64_t{1} << 63u)) {
                const auto magnitude = old_value - new_value;
                delta = magnitude == (std::uint64_t{1} << 63u)
                            ? std::numeric_limits<std::int64_t>::min()
                            : -static_cast<std::int64_t>(magnitude);
                matches = true;
            }
        }
        const bool allowed =
            matches &&
            (rule.prediction.kind == PredictionKind::delta_eq
                 ? delta == rule.prediction.delta
                 : std::find(rule.prediction.deltas.begin(),
                             rule.prediction.deltas.end(),
                             delta) != rule.prediction.deltas.end());
        result.violation = !allowed;
        return result;
    }
    case PredictionKind::monotonic:
        result.violation =
            !scalar_monotonic(rule, before, after, rule.prediction.direction);
        return result;
    case PredictionKind::immutable:
        result.violation = true;
        return result;
    case PredictionKind::per_element_monotonic: {
        const std::size_t element_size = rule.value.element_size;
        for (std::size_t offset = 0; offset < size; offset += element_size) {
            const auto *old_element = before + offset;
            const auto *new_element = after + offset;
            if (std::memcmp(old_element, new_element, element_size) == 0) {
                continue;
            }
            if (rule.prediction.direction == "open_only") {
                if (element_size != 1 || old_element[0] != 0 ||
                    new_element[0] != 1) {
                    result.violation = true;
                    return result;
                }
            } else if (!unsigned_monotonic(read_unsigned(old_element, element_size),
                                           read_unsigned(new_element, element_size),
                                           rule.prediction.direction)) {
                result.violation = true;
                return result;
            }
        }
        return result;
    }
    case PredictionKind::enum_transition: {
        const auto transition = std::make_pair(result.expected, result.actual);
        result.violation =
            rule.prediction.allowed_transitions.find(transition) ==
            rule.prediction.allowed_transitions.end();
        return result;
    }
    }
    result.violation = true;
    return result;
}

}  // namespace mtd_internal
