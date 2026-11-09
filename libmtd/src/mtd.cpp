/* mtd.cpp — libmtd 공개 API 구현 (v0 스텁)
 *
 * S1-01 골격용. 실제 섀도 엔진·mprotect 백엔드·규칙 파서는 S2(L1~L5)에서 채운다.
 * 지금은 링크·호출이 되는지만 보장한다.
 */
#include "mtd.h"

extern "C" {

int mtd_init(const char * /*rules_path*/, const mtd_config * /*cfg*/)
{
    return MTD_OK;
}

void mtd_shutdown(void) {}

void *mtd_alloc(size_t /*size*/)
{
    return nullptr;
}

int mtd_watch(void * /*addr*/, size_t /*size*/, const char * /*name*/)
{
    return MTD_OK;
}

void mtd_begin(const char * /*tag*/) {}

void mtd_end(void) {}

void mtd_event(const char * /*name*/) {}

}  // extern "C"
