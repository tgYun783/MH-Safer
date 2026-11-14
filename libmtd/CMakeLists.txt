# libmtd: 메모리 변조 탐지 라이브러리. 정적/공유 둘 다 빌드 (plan.md §2).
#   rules.cpp            규칙 조회 (S2-13 내부 인터페이스)
#   rules_stub.cpp       규칙 파서 stub → S2-21에서 교체
#   prediction_stub.cpp  예측 검사 stub → S2-22에서 교체

set(MTD_SOURCES
    src/mtd.cpp
    src/rules.cpp
    src/rules_stub.cpp
    src/prediction_stub.cpp)

add_library(mtd STATIC ${MTD_SOURCES})
target_include_directories(mtd PUBLIC include)
set_target_properties(mtd PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mtd_shared SHARED ${MTD_SOURCES})
target_include_directories(mtd_shared PUBLIC include)
set_target_properties(mtd_shared PROPERTIES OUTPUT_NAME mtd)

# C++ 표준 라이브러리·pthread만 의존 (plan.md §2)
find_package(Threads REQUIRED)
target_link_libraries(mtd        PUBLIC Threads::Threads)
target_link_libraries(mtd_shared PUBLIC Threads::Threads)

add_subdirectory(tests)
