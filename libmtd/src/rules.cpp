/* rules.cpp — RuleSet 조회 (S2-13). 파서(load_rules)는 rules_stub.cpp → S2-21에서 교체. */
#include "rules.hpp"
#include "prediction.hpp"

namespace mtd::detail {

bool VarRule::allows_tag(const char *tag) const
{
    if (tag == nullptr) return false;
    for (const auto &t : behavior.allowed_tags)
        if (t == tag) return true;
    return false;
}

const VarRule *RuleSet::find(const char *name) const
{
    if (name == nullptr) return nullptr;
    for (const auto &v : vars_)
        if (v.name == name) return &v;
    return nullptr;
}

const char *pred_rule_name(PredRule r) noexcept
{
    switch (r) {
    case PredRule::delta_eq:              return "delta_eq";
    case PredRule::delta_in:              return "delta_in";
    case PredRule::monotonic:             return "monotonic";
    case PredRule::immutable:             return "immutable";
    case PredRule::per_element_monotonic: return "per_element_monotonic";
    case PredRule::enum_transition:       return "enum_transition";
    case PredRule::none:                  break;
    }
    return nullptr;
}

}  // namespace mtd::detail
