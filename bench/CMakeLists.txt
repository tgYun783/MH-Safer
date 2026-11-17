# T5 자원 측정. 측정 프로그램은 ctest에 넣지 않는다(실행 시간·환경 의존). 수동 실행.
#   S2-14: bench_array_compare — 배열 바이트 비교 오버헤드 (docs/poc/array-compare.md)
add_executable(bench_array_compare array_compare.cpp)
target_link_libraries(bench_array_compare PRIVATE mtd)
