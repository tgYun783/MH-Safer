#ifndef MTD_RULES_HPP
#define MTD_RULES_HPP

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mtd_internal {

enum class PredictionKind {
    delta_eq,
    delta_in,
    monotonic,
    immutable,
    per_element_monotonic,
    enum_transition
};

struct ValueRule {
    bool scalar = false;
    std::size_t size = 0;
    std::size_t element_size = 0;
    std::optional<std::size_t> count;
    bool is_signed = false;
    std::string encoding;
    std::unordered_map<std::string, std::uint64_t> enum_codes;
};

struct BehaviorRule {
    bool mutable_value = true;
    std::vector<std::string> allowed_tags;
    std::optional<std::string> required_event;
    std::uint64_t event_window_ms = 0;
};

struct PredictionRule {
    PredictionKind kind = PredictionKind::immutable;
    std::string direction;
    std::int64_t delta = 0;
    std::vector<std::int64_t> deltas;
    std::set<std::pair<std::uint64_t, std::uint64_t>> allowed_transitions;
};

struct VariableRule {
    std::string name;
    ValueRule value;
    BehaviorRule behavior;
    PredictionRule prediction;

    bool accepts_size(std::size_t actual_size) const
    {
        if (value.scalar) {
            return actual_size == value.size;
        }
        return value.element_size != 0 &&
               actual_size % value.element_size == 0 &&
               (!value.count ||
                actual_size / value.element_size == *value.count);
    }
};

inline const nlohmann::json &required(const nlohmann::json &object,
                                     const char *key)
{
    if (!object.is_object() || !object.contains(key)) {
        throw std::runtime_error(std::string("missing field: ") + key);
    }
    return object.at(key);
}

inline std::uint64_t as_u64(const nlohmann::json &value, const char *field)
{
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if (number >= 0) {
            return static_cast<std::uint64_t>(number);
        }
    }
    throw std::runtime_error(std::string("expected nonnegative integer: ") + field);
}

inline std::int64_t as_i64(const nlohmann::json &value, const char *field)
{
    if (value.is_number_integer()) {
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number <= static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())) {
                return static_cast<std::int64_t>(number);
            }
        } else {
            return value.get<std::int64_t>();
        }
    }
    throw std::runtime_error(std::string("expected signed 64-bit integer: ") + field);
}

inline std::size_t as_size(const nlohmann::json &value, const char *field)
{
    const auto number = as_u64(value, field);
    if (number > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error(std::string("integer too large: ") + field);
    }
    return static_cast<std::size_t>(number);
}

inline std::string as_string(const nlohmann::json &value, const char *field)
{
    if (!value.is_string()) {
        throw std::runtime_error(std::string("expected string: ") + field);
    }
    const auto result = value.get<std::string>();
    if (result.empty()) {
        throw std::runtime_error(std::string("empty string: ") + field);
    }
    return result;
}

inline PredictionKind parse_prediction_kind(const std::string &name)
{
    if (name == "delta_eq") {
        return PredictionKind::delta_eq;
    }
    if (name == "delta_in") {
        return PredictionKind::delta_in;
    }
    if (name == "monotonic") {
        return PredictionKind::monotonic;
    }
    if (name == "immutable") {
        return PredictionKind::immutable;
    }
    if (name == "per_element_monotonic") {
        return PredictionKind::per_element_monotonic;
    }
    if (name == "enum_transition") {
        return PredictionKind::enum_transition;
    }
    throw std::runtime_error("unknown prediction rule: " + name);
}

struct RuleSet {
    std::uint64_t detect_window_ms = 0;
    std::uint64_t default_event_window_ms = 0;
    std::unordered_map<std::string, VariableRule> variables;

    const VariableRule *find(const std::string &name) const
    {
        const auto found = variables.find(name);
        return found == variables.end() ? nullptr : &found->second;
    }

    static RuleSet load(const char *path)
    {
        if (path == nullptr || *path == '\0') {
            throw std::runtime_error("rules path is empty");
        }

        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error(std::string("cannot open rules file: ") + path);
        }
        const auto root = nlohmann::json::parse(input);
        if (!root.is_object() || as_string(required(root, "schema_version"),
                                           "schema_version") != "0") {
            throw std::runtime_error("unsupported rules schema_version");
        }
        (void)as_string(required(root, "program"), "program");
        RuleSet result;
        result.detect_window_ms =
            as_u64(required(root, "detect_window_ms"), "detect_window_ms");
        result.default_event_window_ms =
            as_u64(required(root, "default_event_window_ms"),
                   "default_event_window_ms");
        const auto &variables = required(root, "variables");
        if (!variables.is_array() || variables.empty()) {
            throw std::runtime_error("variables must be a non-empty array");
        }
        for (const auto &item : variables) {
            auto rule = parse_variable(item, result.default_event_window_ms);
            const auto name = rule.name;
            if (!result.variables.emplace(name, std::move(rule)).second) {
                throw std::runtime_error("duplicate variable: " + name);
            }
        }
        return result;
    }

private:
    static VariableRule parse_variable(const nlohmann::json &item,
                                       std::uint64_t default_event_window_ms)
    {
        VariableRule result;
        result.name = as_string(required(item, "name"), "name");

        const auto &value = required(item, "value");
        const auto type = as_string(required(value, "type"), "value.type");
        result.value.encoding =
            as_string(required(value, "encoding"), "value.encoding");
        const auto compare = as_string(required(value, "compare"), "value.compare");
        if (type == "scalar") {
            result.value.scalar = true;
            result.value.size = as_size(required(value, "size"), "value.size");
            if ((result.value.size != 1 && result.value.size != 2 &&
                 result.value.size != 4 && result.value.size != 8) ||
                compare != "value") {
                throw std::runtime_error("invalid scalar value rule: " + result.name);
            }
            const auto &signed_value = required(value, "signed");
            if (!signed_value.is_boolean()) {
                throw std::runtime_error("value.signed must be boolean");
            }
            result.value.is_signed = signed_value.get<bool>();
            if (result.value.encoding == "enum") {
                if (result.value.is_signed) {
                    throw std::runtime_error("enum values must be unsigned");
                }
                const auto &enumeration = required(value, "enum");
                const auto &states = required(enumeration, "states");
                const auto &codes = required(enumeration, "codes");
                if (!states.is_array() || !codes.is_array() ||
                    states.empty() || states.size() != codes.size()) {
                    throw std::runtime_error("invalid enum definition: " + result.name);
                }
                for (std::size_t i = 0; i < states.size(); ++i) {
                    const auto state = as_string(states[i], "enum state");
                    const auto code = as_u64(codes[i], "enum code");
                    if (result.value.size < sizeof(std::uint64_t) &&
                        code >= (std::uint64_t{1} <<
                                 (result.value.size * 8u))) {
                        throw std::runtime_error("enum code does not fit value size");
                    }
                    if (!result.value.enum_codes.emplace(state, code).second) {
                        throw std::runtime_error("duplicate enum state: " + state);
                    }
                }
            } else if ((result.value.encoding != "int" &&
                        result.value.encoding != "uint") ||
                       (result.value.encoding == "int" &&
                        !result.value.is_signed) ||
                       (result.value.encoding == "uint" &&
                        result.value.is_signed)) {
                throw std::runtime_error("unsupported scalar encoding");
            }
        } else if (type == "array") {
            result.value.scalar = false;
            result.value.element_size =
                as_size(required(value, "element_size"), "value.element_size");
            const auto &count = required(value, "count");
            if (!count.is_null()) {
                result.value.count = as_size(count, "value.count");
                if (*result.value.count == 0) {
                    throw std::runtime_error("value.count must be positive or null");
                }
            }
            if ((result.value.element_size != 1 &&
                 result.value.element_size != 2 &&
                 result.value.element_size != 4 &&
                 result.value.element_size != 8) ||
                compare != "bytes" ||
                result.value.encoding != "uint") {
                throw std::runtime_error("invalid array value rule: " + result.name);
            }
        } else {
            throw std::runtime_error("unknown value type: " + type);
        }

        const auto &behavior = required(item, "behavior");
        const auto &mutable_value = required(behavior, "mutable");
        if (!mutable_value.is_boolean()) {
            throw std::runtime_error("behavior.mutable must be boolean");
        }
        result.behavior.mutable_value = mutable_value.get<bool>();
        const auto &tags = required(behavior, "allowed_in_tags");
        if (!tags.is_array()) {
            throw std::runtime_error("behavior.allowed_in_tags must be an array");
        }
        for (const auto &tag : tags) {
            result.behavior.allowed_tags.push_back(as_string(tag, "allowed tag"));
        }
        const auto &event = required(behavior, "required_event");
        if (!event.is_null()) {
            result.behavior.required_event = as_string(event, "required_event");
        }
        const auto &event_window = required(behavior, "event_window_ms");
        result.behavior.event_window_ms =
            event_window.is_null()
                ? default_event_window_ms
                : as_u64(event_window, "event_window_ms");

        const auto &prediction = required(item, "prediction");
        result.prediction.kind = parse_prediction_kind(
            as_string(required(prediction, "rule"), "prediction.rule"));
        const auto &params = required(prediction, "params");
        if (!params.is_object()) {
            throw std::runtime_error("prediction.params must be an object");
        }
        switch (result.prediction.kind) {
        case PredictionKind::delta_eq:
            result.prediction.delta =
                as_i64(required(params, "delta"), "params.delta");
            if (!result.value.scalar) {
                throw std::runtime_error("delta_eq requires a scalar variable");
            }
            break;
        case PredictionKind::delta_in: {
            const auto &deltas = required(params, "deltas");
            if (!deltas.is_array() || deltas.empty() || !result.value.scalar) {
                throw std::runtime_error("delta_in requires scalar and deltas[]");
            }
            for (const auto &delta : deltas) {
                result.prediction.deltas.push_back(as_i64(delta, "params.deltas"));
            }
            break;
        }
        case PredictionKind::monotonic:
            result.prediction.direction =
                as_string(required(params, "direction"), "params.direction");
            if (!result.value.scalar ||
                (result.prediction.direction != "non_decreasing" &&
                 result.prediction.direction != "non_increasing" &&
                 result.prediction.direction != "increasing" &&
                 result.prediction.direction != "decreasing")) {
                throw std::runtime_error("invalid monotonic rule");
            }
            break;
        case PredictionKind::immutable:
            break;
        case PredictionKind::per_element_monotonic:
            result.prediction.direction =
                as_string(required(params, "direction"), "params.direction");
            if (result.value.scalar ||
                (result.prediction.direction != "open_only" &&
                 result.prediction.direction != "non_decreasing" &&
                 result.prediction.direction != "non_increasing") ||
                (result.prediction.direction == "open_only" &&
                 result.value.element_size != 1)) {
                throw std::runtime_error("invalid per_element_monotonic rule");
            }
            break;
        case PredictionKind::enum_transition: {
            if (result.value.encoding != "enum") {
                throw std::runtime_error("enum_transition requires enum encoding");
            }
            const auto &allowed = required(params, "allowed");
            if (!allowed.is_array() || allowed.empty()) {
                throw std::runtime_error("enum_transition requires allowed pairs");
            }
            for (const auto &transition : allowed) {
                if (!transition.is_array() || transition.size() != 2) {
                    throw std::runtime_error("enum transition must be a pair");
                }
                const auto from_name = as_string(transition[0], "transition state");
                const auto to_name = as_string(transition[1], "transition state");
                const auto from = result.value.enum_codes.find(from_name);
                const auto to = result.value.enum_codes.find(to_name);
                if (from == result.value.enum_codes.end() ||
                    to == result.value.enum_codes.end()) {
                    throw std::runtime_error("enum transition names unknown state");
                }
                result.prediction.allowed_transitions.emplace(from->second, to->second);
            }
            break;
        }
        }
        return result;
    }
};

}  // namespace mtd_internal

#endif  // MTD_RULES_HPP
