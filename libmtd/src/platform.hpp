/* platform.hpp — OS 의존 기능 격리 (monitoring-design §6)
 * 섀도 엔진(mtd.cpp)은 이 파일의 함수만 통해 OS를 만난다. POSIX 구현은 platform_posix.cpp.
 */
#ifndef MTD_PLATFORM_HPP
#define MTD_PLATFORM_HPP

#include <cstddef>
#include <cstdint>

namespace mtd::detail {

/* CLOCK_MONOTONIC 나노초 (interface.md 공통 규약) */
std::uint64_t now_ns() noexcept;

/* 감시 전용 페이지 할당·반환 (POSIX mmap / Windows VirtualAlloc). 실패 시 nullptr.
 * out_mapped: 실제로 잡은 바이트 수(페이지 단위 올림) — page_free에 그대로 넘긴다. */
void *page_alloc(std::size_t size, std::size_t *out_mapped) noexcept;
void  page_free(void *p, std::size_t mapped) noexcept;

}  // namespace mtd::detail

#endif  // MTD_PLATFORM_HPP
