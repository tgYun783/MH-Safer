/* scenario.hpp — 변조 시나리오(docs/experiment.md §4.1)와 쓸 값 계산
 *
 * 라벨 형식은 `<유형>-<변수>`(§7, 11-16). §4.1 표에서 "—"가 아닌 칸 14개를 모두 지원한다(S3-12).
 *
 * | 시나리오            | 쓰는 값                                              |
 * |---------------------|------------------------------------------------------|
 * | T-timer             | +1                                                   |
 * | T-mines_remaining   | ±1 (무작위)                                          |
 * | T-cell_state        | 닫힌 안전 칸 1개 열기(0 → 1, mine_field가 알려진 뒤)  |
 * | T-cells_opened      | +1                                                   |
 * | V-timer             | 0 (이미 0이면 건너뜀)                                |
 * | V-mines_remaining   | 999 (이미 999면 건너뜀)                              |
 * | V-mine_field        | 지뢰 1개 제거(무작위 1 → 0)                          |
 * | V-cell_state        | 열린 칸 1개 닫기(무작위 1 → 0)                       |
 * | V-cells_opened      | +100                                                 |
 * | V-game_state        | 1 = won (playing(0)일 때만)                          |
 * | H-timer             | 정상 tick 직후 +1                                    |
 * | H-mines_remaining   | 정상 flag 직후 ±1                                    |
 * | H-cell_state        | 정상 reveal 직후 닫힌 안전 칸 1개 열기               |
 * | H-cells_opened      | 정상 reveal 직후 +1                                  |
 *
 * H(은닉형)는 hidden=true. "직후"는 주입기가 같은 변수를 폴링해 값이 바뀐 것을 본 순간이다
 * (experiment.md §7: timer 변화 폴링, S3-12에서 나머지 변수도 같은 방식으로 결정).
 * T-cell_state·H-cell_state의 칸 위치: §4.1은 "이웃 칸"이지만 주소 로그에 보드 폭이 없어
 * 위치는 무작위 닫힌 안전 칸으로 한다. 탐지 규칙(구간 밖 변화 / 요소별 open_only)은 위치와 무관하다.
 * 보드가 배열 전체(480칸)를 채운다고 가정한다 — 실험 난이도는 hard 하나(§7).
 *
 * 계산은 순수 함수(plan_write)라 프로세스 없이 테스트한다.
 */
#ifndef INJECTOR_SCENARIO_HPP
#define INJECTOR_SCENARIO_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace injector {

struct Scenario {
    char        type = 0;   /* 'T' | 'V' | 'H' */
    std::string var;        /* 변수 이름(주소 로그 fields[].name) */
    std::string label;      /* "T-timer" */
    bool        hidden = false;              /* H면 true */
    bool        needs_mine_field = false;    /* 칸을 열 때 안전 칸을 고르려고 mine_field를 읽는다 */
};

/* "T-timer" → Scenario. 지원하지 않으면 nullopt, err에 이유. */
std::optional<Scenario> parse_scenario(const std::string &s, std::string &err);

/* 지원하는 시나리오 전부(§4.1 표 순서) */
const std::vector<std::string> &supported_scenarios();

/* 변수 하나에 쓸 내용 */
struct Plan {
    std::size_t               offset = 0;  /* 변수 시작에서의 바이트 오프셋 */
    std::vector<std::uint8_t> bytes;       /* 쓸 바이트 */
    bool                      scalar = false;
    std::uint64_t             from = 0;    /* scalar일 때만 의미(little-endian 원시값) */
    std::uint64_t             to = 0;
};

/* 현재 변수 바이트(current)를 보고 쓸 내용을 정한다. 지금 쓸 수 없으면(no-op 등) nullopt.
 * mine_field: needs_mine_field인 시나리오에서 같은 크기의 지뢰 배치(없으면 nullptr → nullopt). */
std::optional<Plan> plan_write(const Scenario &sc, const std::vector<std::uint8_t> &current, std::mt19937 &rng,
                               const std::vector<std::uint8_t> *mine_field = nullptr);

}  // namespace injector

#endif  // INJECTOR_SCENARIO_HPP
