/* mtd_test.h — libmtd 테스트 전용 조회 (공개 API 아님)
 *
 * 통합 테스트(S2-06 봇 10분 실행)와 단위 테스트에서 감시 상태를 확인하는 용도.
 * 시그니처 안정성을 보장하지 않는다. 제품 코드에서 쓰지 말 것.
 */
#ifndef MTD_TEST_H
#define MTD_TEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 현재 감시 테이블에 등록된 변수 수. 초기화 전이면 0. */
size_t mtd_test_watch_count(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MTD_TEST_H */
