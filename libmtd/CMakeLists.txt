# libmtd: 메모리 변조 탐지 라이브러리. 정적/공유 둘 다 빌드 (plan.md §2).
# v0: 공개 API 스텁만. 실제 구현(섀도 엔진·mprotect 백엔드)은 S2에서.

set(MTD_SOURCES src/mtd.cpp src/prediction.cpp)

add_library(mtd STATIC ${MTD_SOURCES})
target_include_directories(mtd PUBLIC include)
target_include_directories(mtd SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
set_target_properties(mtd PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mtd_shared SHARED ${MTD_SOURCES})
target_include_directories(mtd_shared PUBLIC include)
target_include_directories(mtd_shared SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
set_target_properties(mtd_shared PROPERTIES OUTPUT_NAME mtd)

# C++ 표준 라이브러리·pthread만 의존 (plan.md §2)
find_package(Threads REQUIRED)
target_link_libraries(mtd        PUBLIC Threads::Threads)
target_link_libraries(mtd_shared PUBLIC Threads::Threads)

add_subdirectory(tests)
