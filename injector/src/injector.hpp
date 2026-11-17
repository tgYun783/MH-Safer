/* injector.hpp — T2 변조 주입기 코어 (S3-11 최소 버전)
 *
 * 대상: 이 저장소의 게임(또는 예제 앱)만. 주소는 게임의 주소 로그로만 정한다(메모리 스캔 없음).
 * 쓰기 경로: /proc/<pid>/mem (method "proc", docs/poc/write-paths.md S1-11).
 *
 * 해제된 메모리에 쓰지 않기(완료 기준 "해제된 주소에 쓴 건수 0"):
 *   쓰기 직전에 대상 프로세스를 SIGSTOP으로 멈추고(모든 스레드가 멈춘 것 확인), 주소 로그를 끝까지
 *   다시 읽은 다음 쓰고, SIGCONT로 재개한다. 게임은 state_free를 기록(flush)한 뒤에 메모리를
 *   해제하므로, 멈춘 상태에서 state_free가 안 보이면 그 상태는 아직 살아 있다.
 *   (해제된 GameState에 쓰면 malloc 메타데이터를 깨 게임이 죽을 수 있다.)
 *   사후 확인: 라벨마다 그 수명의 state_free 시각을 기록해 inject_ns ≥ state_free.ns인 건수를 센다.
 */
#ifndef INJECTOR_INJECTOR_HPP
#define INJECTOR_INJECTOR_HPP

#include "scenario.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <sys/types.h>

namespace injector {

struct Options {
    pid_t         pid = 0;
    std::string   addr_log;            /* 게임 --addr-log 파일 (따라 읽음) */
    Scenario      scenario;
    unsigned      count = 100;         /* 라벨 수 목표 */
    unsigned      interval_ms = 500;   /* 변조 간격 (experiment.md §4.2: ≥ 500ms) */
    unsigned      jitter_ms = 200;     /* 0~jitter 무작위 추가 */
    unsigned      duration_sec = 0;    /* 0 = 제한 없음 */
    std::uint32_t seed = 1;
    bool          freeze = true;       /* 쓰기 동안 SIGSTOP/SIGCONT */
    bool          verbose = false;
};

struct Result {
    unsigned      labels = 0;          /* 성공한 변조 = 라벨 줄 수 */
    unsigned      write_errors = 0;    /* pwrite 실패(라벨 없음) */
    unsigned      skipped = 0;         /* 대상 없음·no-op로 미룬 시도 */
    unsigned      freeze_timeouts = 0; /* 멈춤 확인 실패 → 그 시도는 건너뜀 */
    unsigned      freed_writes = 0;    /* 사후 확인: 해제된 수명에 쓴 라벨 수 (0이어야 함) */
    unsigned      bad_log_lines = 0;
    std::uint64_t order_errors = 0;    /* 주소 로그 순서 위반(§3.1) */
    std::uint64_t allocs = 0;          /* 본 state_alloc 수(판 수). 2 이상이면 판 교체를 지나며 주입한 것 */
    bool          target_exited = false;
};

/* 한 시나리오를 count건(또는 duration/대상 종료까지) 주입한다. labels에 JSONL(§3).
 * 진행할 수 없는 오류(파일·/proc 열기 실패)는 err에 쓰고 false. */
bool run(const Options &opt, std::ostream &labels, Result &res, std::string &err);

std::uint64_t monotonic_ns();

}  // namespace injector

#endif  // INJECTOR_INJECTOR_HPP
