# libmtd: 메모리 변조 탐지 라이브러리. 정적/공유 둘 다 빌드 (plan.md §2).
#   mtd.cpp              공개 API + 섀도 감시 엔진(L1·L2) + 행위 탐지(L3)  — S2-11·S2-12
#   platform_posix.cpp   OS 의존 기능(시계·mmap) 격리                      — monitoring-design §6
#   rules.cpp            규칙 조회 (S2-13 내부 인터페이스)
#   rules_parser.cpp     규칙 파서(L5, nlohmann/json)                      — S2-21
#   prediction.cpp       예측 검사 6종(L4)                                 — S2-22

set(MTD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mtd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_posix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rules.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rules_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/prediction.cpp)
set(MTD_THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)  # nlohmann/json, SYSTEM으로 경고 제외

add_library(mtd STATIC ${MTD_SOURCES})
target_include_directories(mtd PUBLIC include)
target_include_directories(mtd SYSTEM PRIVATE ${MTD_THIRD_PARTY})
set_target_properties(mtd PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mtd_shared SHARED ${MTD_SOURCES})
target_include_directories(mtd_shared PUBLIC include)
target_include_directories(mtd_shared SYSTEM PRIVATE ${MTD_THIRD_PARTY})
set_target_properties(mtd_shared PROPERTIES OUTPUT_NAME mtd)

# C++ 표준 라이브러리·pthread만 의존 (plan.md §2)
find_package(Threads REQUIRED)
target_link_libraries(mtd        PUBLIC Threads::Threads)
target_link_libraries(mtd_shared PUBLIC Threads::Threads)

# 테스트 전용: AddressSanitizer로 계측한 정적 라이브러리 (S2-11 "해제 후 접근 0건").
# 감시 주소를 읽는 쪽이 라이브러리 코드이므로 라이브러리 자체를 계측해야 한다.
option(MTD_ASAN_TESTS "ASan으로 계측한 libmtd 테스트도 빌드" ON)
if(MTD_ASAN_TESTS AND UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(MTD_ASAN_FLAGS -fsanitize=address -fno-omit-frame-pointer)
  add_library(mtd_asan STATIC ${MTD_SOURCES})
  target_include_directories(mtd_asan PUBLIC include)
  target_include_directories(mtd_asan SYSTEM PRIVATE ${MTD_THIRD_PARTY})
  target_compile_options(mtd_asan PUBLIC ${MTD_ASAN_FLAGS})
  target_link_options(mtd_asan PUBLIC ${MTD_ASAN_FLAGS})
  target_link_libraries(mtd_asan PUBLIC Threads::Threads)
endif()

add_subdirectory(tests)
