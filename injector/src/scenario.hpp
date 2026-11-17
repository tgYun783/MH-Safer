/* scenario.hpp — 변조 시나리오(docs/experiment.md §4.1)와 쓸 값 계산
 *
 * 라벨 형식은 `<유형>-<변수>`(§7, 11-16). 최소 버전(S3-11)은 T·V 중 아래 표만 지원하고,
 * 나머지(T-cell_state, H 은닉형 전부)는 S3-12에서 추가한다.
 *
 * | 시나리오            | 쓰는 값                                   |
 * |---------------------|-------------------------------------------|
 * | T-timer             | +1                                        |
 * | T-mines_remaining   | ±1 (무작위)                               |
 * | T-cells_opened      | +1                                        |
 * | V-timer             | 0 (이미 0이면 건너뜀)                     |
 * | V-mines_remaining   | 999 (이미 999면 건너뜀)                   |
 * | V-cells_opened      | +100                                      |
 * | V-game_state        | 1 = won (playing(0)일 때만)               |
 * | V-mine_field        | 지뢰 1개 제거(무작위 1 → 0)               |
 * | V-cell_state        | 열린 칸 1개 닫기(무작위 1 → 0)            |
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
    bool        hidden = false;
};

/* "T-timer" → Scenario. 지원하지 않으면 nullopt, err에 이유. */
std::optional<Scenario> parse_scenario(const std::string &s, std::string &err);

/* 변수 하나에 쓸 내용 */
struct Plan {
    std::size_t               offset = 0;  /* 변수 시작에서의 바이트 오프셋 */
    std::vector<std::uint8_t> bytes;       /* 쓸 바이트 */
    bool                      scalar = false;
    std::uint64_t             from = 0;    /* scalar일 때만 의미(little-endian 원시값) */
    std::uint64_t             to = 0;
};

/* 현재 변수 바이트(current)를 보고 쓸 내용을 정한다. 지금 쓸 수 없으면(no-op 등) nullopt. */
std::optional<Plan> plan_write(const Scenario &sc, const std::vector<std::uint8_t> &current, std::mt19937 &rng);

}  // namespace injector

#endif  // INJECTOR_SCENARIO_HPP
