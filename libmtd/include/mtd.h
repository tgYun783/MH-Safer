/* mtd.h — 메모리 변조 탐지 라이브러리(libmtd) 공개 API (v0)
 *
 * 계약: 게임 ↔ 라이브러리 경계. extern "C" + 순수 C 타입만 넘긴다
 * (C++ 예외·STL 타입을 경계 밖으로 내보내지 않는다 — plan.md §2 이식성 원칙).
 *
 * 감시 주력은 섀도 복사 + 주기 검사(docs/monitoring-design.md). mprotect 보조
 * 백엔드는 enable_mprotect로 켠다. 형식 규격은 docs/interface.md 참고.
 *
 * v0 상태: 시그니처·구조체 필드 확정용. 구현 없음.
 */
#ifndef MTD_H
#define MTD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MTD_OK   0
#define MTD_ERR (-1)

#define MTD_DEFAULT_PERIOD_MS 50u   /* 검사 주기 기본값 (S1-12 스윕으로 확정 예정) */

/* 판정 종류 */
typedef enum {
    MTD_KIND_BEHAVIOR   = 0,  /* 합법 변경 구간 '밖'의 변화 = 타이밍 위반 (행위 기반) */
    MTD_KIND_PREDICTION = 1   /* 합법 구간 '안'의 규칙 위반 = 값 위반 (예측 기반) */
} mtd_kind;

/* 탐지 알림 1건. 문자열 포인터는 콜백 반환 전까지만 유효(복사해 쓸 것). */
typedef struct {
    const char *name;      /* 감시 변수 이름 (mtd_watch에서 등록한 것) */
    mtd_kind    kind;      /* behavior | prediction */
    const char *rule;      /* 위반한 규칙/태그 식별자, 없으면 NULL */
    uint64_t    expected;  /* 기대값(섀도). 스칼라(≤8바이트)가 아니면 0 */
    uint64_t    actual;    /* 실제값. 스칼라가 아니면 0 */
    uint64_t    detect_ns; /* 탐지 시각 (CLOCK_MONOTONIC, ns) */
} mtd_alert;

/* 탐지 시 호출. 검사 스레드 문맥에서 불리므로 무거운 작업 금지. */
typedef void (*mtd_alert_cb)(const mtd_alert *alert, void *user);

typedef struct {
    unsigned     period_ms;       /* 검사 주기(ms). 0이면 MTD_DEFAULT_PERIOD_MS */
    int          enable_mprotect; /* 내부 쓰기 SIGSEGV 보조 백엔드 on(1)/off(0). 기본 0 */
    mtd_alert_cb on_alert;        /* 알림 콜백 (NULL이면 로그만) */
    void        *user;            /* 콜백에 그대로 전달되는 사용자 포인터 */
} mtd_config;

/* --- 생애주기 --- */
/* rules_path: 규칙 파일(docs/interface.md, S1-05). NULL이면 규칙 없이 행위 기반만. */
int  mtd_init(const char *rules_path, const mtd_config *cfg);
void mtd_shutdown(void);

/* --- 감시 --- */
/* (선택) 보조 백엔드용 감시 전용 페이지 할당. 섀도만 쓰면 일반 변수도 감시 가능. */
void *mtd_alloc(size_t size);

/* 감시 등록. 등록 시 섀도 = 현재값. 성공 MTD_OK, 실패 MTD_ERR. */
int  mtd_watch(void *addr, size_t size, const char *name);

/* --- 합법 변경 구간 (이 구간 안의 변화는 합법으로 간주, mtd_end에서 규칙 검사) --- */
void mtd_begin(const char *tag);
void mtd_end(void);

/* --- 행위 판단용 사건 통지 (예: "input" 직후에만 칸 상태가 바뀔 수 있음) --- */
void mtd_event(const char *name);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MTD_H */
