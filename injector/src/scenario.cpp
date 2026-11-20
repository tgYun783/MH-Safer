/* scenario.cpp — 시나리오 파싱과 쓸 값 계산 (scenario.hpp 표) */
#include "scenario.hpp"

namespace injector {
namespace {

std::uint64_t load(const std::vector<std::uint8_t> &b)
{
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < b.size() && i < 8; ++i) v |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    return v;
}

std::vector<std::uint8_t> store(std::uint64_t v, std::size_t n)
{
    std::vector<std::uint8_t> b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(v >> (8 * i));
    return b;
}

std::uint64_t mask(std::size_t n) { return n >= 8 ? ~0ull : ((1ull << (8 * n)) - 1); }

/* 스칼라에 delta를 더한다(크기 안에서 감기). */
std::optional<Plan> scalar_add(const std::vector<std::uint8_t> &cur, std::int64_t delta)
{
    if (cur.empty() || cur.size() > 8) return std::nullopt;
    Plan p;
    p.scalar = true;
    p.from = load(cur);
    p.to = (p.from + static_cast<std::uint64_t>(delta)) & mask(cur.size());
    p.bytes = store(p.to, cur.size());
    return p;
}

std::optional<Plan> scalar_set(const std::vector<std::uint8_t> &cur, std::uint64_t value)
{
    if (cur.empty() || cur.size() > 8) return std::nullopt;
    Plan p;
    p.scalar = true;
    p.from = load(cur);
    p.to = value & mask(cur.size());
    if (p.to == p.from) return std::nullopt;  /* 값이 안 바뀌면 변조가 아니다 */
    p.bytes = store(p.to, cur.size());
    return p;
}

std::optional<Plan> pick_one(const std::vector<std::size_t> &idx, std::uint8_t value, std::mt19937 &rng)
{
    if (idx.empty()) return std::nullopt;
    std::uniform_int_distribution<std::size_t> pick(0, idx.size() - 1);
    Plan p;
    p.offset = idx[pick(rng)];
    p.bytes = {value};
    return p;
}

/* 배열에서 값이 0이 아닌 요소 하나를 무작위로 골라 0으로 */
std::optional<Plan> clear_one(const std::vector<std::uint8_t> &cur, std::mt19937 &rng)
{
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < cur.size(); ++i)
        if (cur[i] != 0) idx.push_back(i);
    return pick_one(idx, 0, rng);
}

/* 닫힌(0) 안전 칸(mine_field 0) 하나를 무작위로 골라 열기(1) */
std::optional<Plan> open_safe_one(const std::vector<std::uint8_t> &cells, const std::vector<std::uint8_t> *mines,
                                  std::mt19937 &rng)
{
    if (mines == nullptr || mines->size() != cells.size()) return std::nullopt;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (cells[i] == 0 && (*mines)[i] == 0) idx.push_back(i);
    return pick_one(idx, 1, rng);
}

}  // namespace

const std::vector<std::string> &supported_scenarios()
{
    static const std::vector<std::string> k = {
        "T-timer", "T-mines_remaining", "T-cell_state", "T-cells_opened",
        "V-timer", "V-mines_remaining", "V-mine_field", "V-cell_state", "V-cells_opened", "V-game_state",
        "H-timer", "H-mines_remaining", "H-cell_state", "H-cells_opened",
    };
    return k;
}

std::optional<Scenario> parse_scenario(const std::string &s, std::string &err)
{
    for (const auto &k : supported_scenarios()) {
        if (s == k) {
            Scenario sc;
            sc.type = s[0];
            sc.var = s.substr(2);
            sc.label = s;
            sc.hidden = sc.type == 'H';
            sc.needs_mine_field = sc.var == "cell_state" && sc.type != 'V';
            return sc;
        }
    }
    err = "unsupported scenario '" + s + "' (supported:";
    for (const auto &k : supported_scenarios()) err += " " + k;
    err += "; '—' cells of docs/experiment.md §4.1 are not scenarios)";
    return std::nullopt;
}

std::optional<Plan> plan_write(const Scenario &sc, const std::vector<std::uint8_t> &cur, std::mt19937 &rng,
                               const std::vector<std::uint8_t> *mine_field)
{
    if (sc.type == 'T' || sc.type == 'H') {
        if (sc.var == "timer" || sc.var == "cells_opened") return scalar_add(cur, 1);
        if (sc.var == "mines_remaining") {
            std::bernoulli_distribution sign(0.5);
            return scalar_add(cur, sign(rng) ? 1 : -1);
        }
        if (sc.var == "cell_state") return open_safe_one(cur, mine_field, rng);
    } else if (sc.type == 'V') {
        if (sc.var == "timer") return scalar_set(cur, 0);
        if (sc.var == "mines_remaining") return scalar_set(cur, 999);
        if (sc.var == "cells_opened") return scalar_add(cur, 100);
        if (sc.var == "game_state") {
            if (cur.empty() || load(cur) != 0) return std::nullopt;  /* playing일 때만 */
            return scalar_set(cur, 1);
        }
        if (sc.var == "mine_field" || sc.var == "cell_state") return clear_one(cur, rng);
    }
    return std::nullopt;
}

}  // namespace injector
