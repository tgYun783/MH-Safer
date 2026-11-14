cmake_minimum_required(VERSION 3.16)
project(MH-Safer LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 프로젝트 공통 경고 (plan.md §2). 서드파티(doctest)는 SYSTEM include로 제외.
add_compile_options(-Wall -Wextra -Werror)

enable_testing()

add_subdirectory(third_party)   # 헤더 전용 벤더링 확인 (S2-08)
add_subdirectory(libmtd)
add_subdirectory(game)

# S1-01 이후 채워지는 골격 (현재 비어 있음):
#   injector/ T2 변조 주입기
#   bench/    T5 자원 측정
#   examples/ T4 이식성 검증용 두 번째 앱
