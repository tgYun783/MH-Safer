/* rules_parser.cpp — 규칙 파서(L5, S2-21): rules.json 스키마 v0 로드·검증
 *
 * docs/rules-format.md 스키마 v0을 읽어 RuleSet(rules.hpp, S2-13 인터페이스)을 채운다.
 * 검증 로직은 PR #15(전유정)의 파서를 S2-13 인터페이스 타입으로 옮긴 것이다.
 *
 * 경계: load_rules는 noexcept. 파싱·검증 실패(예외 포함)는 false + err로 바꾸고,
 * mtd_init이 MTD_ERR로 반환한다(예외가 공개 API 경계를 넘지 않음).
 */
#include "rules.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace mtd::detail {
namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string &msg) { throw std::runtime_error(msg); }

const json &required(const json &obj, const char *key, const std::string &where)
{
    if (!obj.is_object() || !obj.contains(key)) fail(where + ": missing field \"" + key + "\"");
    return obj.at(key);
}

std::uint64_t as_u64(const json &v, const std::string &field)
{
    if (v.is_number_unsigned()) return v.get<std::uint64_t>();
    if (v.is_number_integer()) {
        const auto n = v.get<std::int64_t>();
        if (n >= 0) return static_cast<std::uint64_t>(n);
    }
    fail(field + ": expected nonnegative integer");
}

std::int64_t as_i64(const json &v, const std::string &field)
{
    if (v.is_number_unsigned()) {
        const auto n = v.get<std::uint64_t>();
        if (n <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return static_cast<std::int64_t>(n);
    } else if (v.is_number_integer()) {
        return v.get<std::int64_t>();
    }
    fail(field + ": expected signed 64-bit integer");
}

std::uint32_t as_u32(const json &v, const std::string &field)
{
    const auto n = as_u64(v, field);
    if (n > std::numeric_limits<std::uint32_t>::max()) fail(field + ": integer too large");
    return static_cast<std::uint32_t>(n);
}

std::string as_string(const json &v, const std::string &field)
{
    if (!v.is_string()) fail(field + ": expected string");
    auto s = v.get<std::string>();
    if (s.empty()) fail(field + ": empty string");
    return s;
}

bool as_bool(const json &v, const std::string &field)
{
    if (!v.is_boolean()) fail(field + ": expected boolean");
    return v.get<bool>();
}

bool valid_width(std::size_t n) { return n == 1 || n == 2 || n == 4 || n == 8; }

PredRule parse_rule_name(const std::string &name, const std::string &where)
{
    if (name == "delta_eq") return PredRule::delta_eq;
    if (name == "delta_in") return PredRule::delta_in;
    if (name == "monotonic") return PredRule::monotonic;
    if (name == "immutable") return PredRule::immutable;
    if (name == "per_element_monotonic") return PredRule::per_element_monotonic;
    if (name == "enum_transition") return PredRule::enum_transition;
    fail(where + ": unknown prediction rule \"" + name + "\"");
}

Direction parse_direction(const std::string &d, bool per_element, const std::string &where)
{
    if (d == "non_decreasing") return Direction::non_decreasing;
    if (d == "non_increasing") return Direction::non_increasing;
    if (!per_element && d == "increasing") return Direction::increasing;
    if (!per_element && d == "decreasing") return Direction::decreasing;
    if (per_element && d == "open_only") return Direction::open_only;
    fail(where + ": invalid direction \"" + d + "\"");
}

/* value: 자료형·크기. enum이면 상태 이름 → 코드값을 돌려준다(전이 규칙 해석용). */
void parse_value(const json &value, VarRule &r, std::map<std::string, std::uint64_t> &enum_codes,
                 bool &is_enum)
{
    const std::string where = r.name + ".value";
    const auto type     = as_string(required(value, "type", where), where + ".type");
    const auto encoding = as_string(required(value, "encoding", where), where + ".encoding");
    const auto compare  = as_string(required(value, "compare", where), where + ".compare");
    is_enum = false;

    if (type == "scalar") {
        r.value.is_array = false;
        r.value.size = static_cast<std::size_t>(as_u64(required(value, "size", where), where + ".size"));
        if (!valid_width(r.value.size)) fail(where + ".size: must be 1, 2, 4 or 8");
        if (compare != "value") fail(where + ".compare: scalar requires \"value\"");
        r.value.is_signed = as_bool(required(value, "signed", where), where + ".signed");

        if (encoding == "enum") {
            if (r.value.is_signed) fail(where + ": enum must be unsigned");
            is_enum = true;
            const auto &e      = required(value, "enum", where);
            const json states = required(e, "states", where + ".enum");
            const json codes   = required(e, "codes", where + ".enum");
            if (!states.is_array() || !codes.is_array() || states.empty() || states.size() != codes.size())
                fail(where + ".enum: states and codes must be non-empty arrays of the same length");
            for (std::size_t i = 0; i < states.size(); ++i) {
                const auto state = as_string(states[i], where + ".enum.states");
                const auto code  = as_u64(codes[i], where + ".enum.codes");
                if (r.value.size < 8 && code >= (std::uint64_t{1} << (r.value.size * 8u)))
                    fail(where + ".enum: code does not fit value size");
                if (!enum_codes.emplace(state, code).second) fail(where + ".enum: duplicate state \"" + state + "\"");
                r.value.enum_values.emplace_back(state, code);
            }
        } else if (encoding == "int") {
            if (!r.value.is_signed) fail(where + ": encoding \"int\" requires signed=true");
        } else if (encoding == "uint") {
            if (r.value.is_signed) fail(where + ": encoding \"uint\" requires signed=false");
        } else {
            fail(where + ".encoding: unsupported \"" + encoding + "\"");
        }
    } else if (type == "array") {
        r.value.is_array = true;
        r.value.element_size =
            static_cast<std::size_t>(as_u64(required(value, "element_size", where), where + ".element_size"));
        if (!valid_width(r.value.element_size)) fail(where + ".element_size: must be 1, 2, 4 or 8");
        const auto &count = required(value, "count", where);  /* null = 런타임 결정 */
        if (!count.is_null() && as_u64(count, where + ".count") == 0) fail(where + ".count: must be positive or null");
        if (compare != "bytes") fail(where + ".compare: array requires \"bytes\"");
        if (encoding != "uint") fail(where + ".encoding: array requires \"uint\"");
    } else {
        fail(where + ".type: unknown \"" + type + "\"");
    }
}

void parse_behavior(const json &b, VarRule &r, std::uint32_t default_window)
{
    const std::string where = r.name + ".behavior";
    r.behavior.is_mutable = as_bool(required(b, "mutable", where), where + ".mutable");
    const auto &tags = required(b, "allowed_in_tags", where);
    if (!tags.is_array()) fail(where + ".allowed_in_tags: expected array");
    for (const auto &t : tags) r.behavior.allowed_tags.push_back(as_string(t, where + ".allowed_in_tags"));
    if (!r.behavior.is_mutable && !r.behavior.allowed_tags.empty())
        fail(where + ": mutable=false must have empty allowed_in_tags");

    const auto &ev = required(b, "required_event", where);
    if (!ev.is_null()) r.behavior.required_event = as_string(ev, where + ".required_event");

    const auto &win = required(b, "event_window_ms", where);
    r.behavior.event_window_ms = win.is_null() ? default_window : as_u32(win, where + ".event_window_ms");
    if (r.behavior.event_window_ms == 0) fail(where + ".event_window_ms: must be positive");
}

void parse_prediction(const json &p, VarRule &r, const std::map<std::string, std::uint64_t> &enum_codes,
                      bool is_enum)
{
    const std::string where = r.name + ".prediction";
    PredictionSpec &s = r.prediction;
    s.rule = parse_rule_name(as_string(required(p, "rule", where), where + ".rule"), where);
    const auto &params = required(p, "params", where);
    if (!params.is_object()) fail(where + ".params: expected object");

    switch (s.rule) {
    case PredRule::delta_eq:
        if (r.value.is_array) fail(where + ": delta_eq requires a scalar");
        s.delta = as_i64(required(params, "delta", where + ".params"), where + ".params.delta");
        break;
    case PredRule::delta_in: {
        if (r.value.is_array) fail(where + ": delta_in requires a scalar");
        const json ds = required(params, "deltas", where + ".params");
        if (!ds.is_array() || ds.empty()) fail(where + ".params.deltas: expected non-empty array");
        for (const auto &d : ds) s.deltas.push_back(as_i64(d, where + ".params.deltas"));
        break;
    }
    case PredRule::monotonic:
        if (r.value.is_array) fail(where + ": monotonic requires a scalar");
        s.direction = parse_direction(
            as_string(required(params, "direction", where + ".params"), where + ".params.direction"), false, where);
        break;
    case PredRule::immutable:
        break;
    case PredRule::per_element_monotonic:
        if (!r.value.is_array) fail(where + ": per_element_monotonic requires an array");
        s.direction = parse_direction(
            as_string(required(params, "direction", where + ".params"), where + ".params.direction"), true, where);
        if (s.direction == Direction::open_only && r.value.element_size != 1)
            fail(where + ": open_only requires element_size 1");
        break;
    case PredRule::enum_transition: {
        if (!is_enum) fail(where + ": enum_transition requires encoding \"enum\"");
        const json allowed = required(params, "allowed", where + ".params");
        if (!allowed.is_array() || allowed.empty()) fail(where + ".params.allowed: expected non-empty array");
        for (const auto &pair : allowed) {
            if (!pair.is_array() || pair.size() != 2) fail(where + ".params.allowed: each item must be a pair");
            const auto from = enum_codes.find(as_string(pair[0], where + ".params.allowed"));
            const auto to   = enum_codes.find(as_string(pair[1], where + ".params.allowed"));
            if (from == enum_codes.end() || to == enum_codes.end())
                fail(where + ".params.allowed: unknown enum state");
            s.transitions.emplace_back(from->second, to->second);
        }
        break;
    }
    case PredRule::none:
        break;
    }
}

VarRule parse_variable(const json &item, std::uint32_t default_window, std::size_t index)
{
    VarRule r;
    r.name = as_string(required(item, "name", "variables[" + std::to_string(index) + "]"), "name");
    std::map<std::string, std::uint64_t> enum_codes;
    bool is_enum = false;
    parse_value(required(item, "value", r.name), r, enum_codes, is_enum);
    parse_behavior(required(item, "behavior", r.name), r, default_window);
    parse_prediction(required(item, "prediction", r.name), r, enum_codes, is_enum);
    return r;
}

ConsistencyRule parse_consistency(const json &item, const RuleSet &rs, std::size_t index)
{
    const std::string where = "consistency[" + std::to_string(index) + "]";
    ConsistencyRule rule;
    rule.name = as_string(required(item, "name", where), where + ".name");

    const auto &when = required(item, "when", where);
    rule.when_variable = as_string(required(when, "variable", where + ".when"), where + ".when.variable");
    const VarRule *when_rule = rs.find(rule.when_variable.c_str());
    if (when_rule == nullptr || when_rule->value.is_array || when_rule->value.is_signed ||
        when_rule->value.size == 0 || when_rule->value.size > 8)
        fail(where + ".when.variable: expected a watched unsigned scalar");
    const auto &when_equals = required(when, "equals", where + ".when");
    if (when_equals.is_string()) {
        const auto state = as_string(when_equals, where + ".when.equals");
        const auto code = std::find_if(
            when_rule->value.enum_values.begin(), when_rule->value.enum_values.end(),
            [&state](const auto &entry) { return entry.first == state; });
        if (code == when_rule->value.enum_values.end())
            fail(where + ".when.equals: unknown enum state \"" + state + "\"");
        rule.when_equals = code->second;
    } else {
        rule.when_equals = as_u64(when_equals, where + ".when.equals");
    }
    if (when_rule->value.size < 8 &&
        rule.when_equals >= (std::uint64_t{1} << (when_rule->value.size * 8u)))
        fail(where + ".when.equals: value does not fit variable size");

    const auto &require = required(item, "require", where);
    rule.require_variable =
        as_string(required(require, "variable", where + ".require"), where + ".require.variable");
    const VarRule *require_rule = rs.find(rule.require_variable.c_str());
    if (require_rule == nullptr || require_rule->value.is_array || require_rule->value.is_signed ||
        require_rule->value.size == 0 || require_rule->value.size > 8)
        fail(where + ".require.variable: expected an unsigned scalar");
    const auto &equals = required(require, "equals", where + ".require");
    rule.equals_variable =
        as_string(required(equals, "variable", where + ".require.equals"), where + ".require.equals.variable");
    const VarRule *equals_rule = rs.find(rule.equals_variable.c_str());
    if (equals_rule == nullptr || equals_rule->value.is_array || equals_rule->value.is_signed ||
        equals_rule->value.size == 0 || equals_rule->value.size > 8)
        fail(where + ".require.equals.variable: expected an unsigned scalar");
    return rule;
}

RuleSet parse_root(const json &root)
{
    if (!root.is_object()) fail("root: expected object");
    RuleSet rs;
    rs.schema_version = as_string(required(root, "schema_version", "root"), "schema_version");
    if (rs.schema_version != "0") fail("schema_version: unsupported \"" + rs.schema_version + "\"");
    rs.program = as_string(required(root, "program", "root"), "program");
    rs.detect_window_ms = as_u32(required(root, "detect_window_ms", "root"), "detect_window_ms");
    rs.default_event_window_ms =
        as_u32(required(root, "default_event_window_ms", "root"), "default_event_window_ms");
    if (rs.default_event_window_ms == 0) fail("default_event_window_ms: must be positive");

    const json vars = required(root, "variables", "root");
    if (!vars.is_array() || vars.empty()) fail("variables: expected non-empty array");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < vars.size(); ++i) {
        VarRule r = parse_variable(vars[i], rs.default_event_window_ms, i);
        if (!seen.insert(r.name).second) fail("variables: duplicate name \"" + r.name + "\"");
        rs.add(std::move(r));
    }
    if (root.contains("consistency")) {
        const json checks = root.at("consistency");
        if (!checks.is_array()) fail("consistency: expected an array");
        std::set<std::string> names;
        for (std::size_t i = 0; i < checks.size(); ++i) {
            ConsistencyRule rule = parse_consistency(checks[i], rs, i);
            if (!names.insert(rule.name).second)
                fail("consistency: duplicate name \"" + rule.name + "\"");
            rs.add_consistency(std::move(rule));
        }
    }
    return rs;
}

}  // namespace

bool load_rules(const char *path, RuleSet &out, std::string &err) noexcept
{
    try {
        out = RuleSet{};
        if (path == nullptr) return true;  /* 규칙 없이 행위 기반만 (mtd.h) */
        std::ifstream in(path);
        if (!in) {
            err = std::string("cannot open rules file: ") + path;
            return false;
        }
        out = parse_root(json::parse(in));
        return true;
    } catch (const std::exception &e) {
        out = RuleSet{};
        try { err = e.what(); } catch (...) {}
        return false;
    } catch (...) {
        out = RuleSet{};
        return false;
    }
}

}  // namespace mtd::detail
