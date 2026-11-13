/* platform_posix.cpp — platform.hpp의 POSIX 구현 */
#include "platform.hpp"

#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace mtd::detail {

std::uint64_t now_ns() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

void *page_alloc(std::size_t size, std::size_t *out_mapped) noexcept
{
    if (size == 0) return nullptr;
    long ps = sysconf(_SC_PAGESIZE);
    std::size_t page = ps > 0 ? static_cast<std::size_t>(ps) : 4096u;
    std::size_t mapped = (size + page - 1) / page * page;
    void *p = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    if (out_mapped) *out_mapped = mapped;
    return p;
}

void page_free(void *p, std::size_t mapped) noexcept
{
    if (p != nullptr && mapped != 0) munmap(p, mapped);
}

}  // namespace mtd::detail
