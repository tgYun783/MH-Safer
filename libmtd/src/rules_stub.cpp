/* rules_stub.cpp — load_rules 임시 구현 (S2-13 stub)
 *
 * S2-21(전유정, 규칙 파서)이 이 파일을 실제 구현으로 교체한다.
 * 그 전까지는 경로가 열리는지만 확인하고 빈 RuleSet을 돌려준다
 * → 엔진은 '규칙 없는 변수 = 아무 구간이나 합법'으로 동작(행위 탐지만).
 */
#include "rules.hpp"

#include <cstdio>

namespace mtd::detail {

bool load_rules(const char *path, RuleSet &out, std::string &err) noexcept
{
    out = RuleSet{};
    if (path == nullptr) return true;
    std::FILE *f = std::fopen(path, "rb");
    if (f == nullptr) {
        err = "rules file not found";
        return false;
    }
    std::fclose(f);
    std::fprintf(stderr, "[libmtd] rules parser not implemented yet (S2-21): %s ignored\n", path);
    return true;
}

}  // namespace mtd::detail
