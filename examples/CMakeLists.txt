# T4 이식성 검증: 2048.c (mevdschee/2048.c, MIT) — examples/README.md
# 원본은 저장소에 복사하지 않는다. 고정 커밋을 받아 build 폴더에 복사한 뒤 mtd.patch를 적용해 빌드한다.
# 패치 줄 수 = 이식 수정량(측정값). 원본은 서드파티라 -Werror에서 뺀다(경고는 그대로 보임).
option(MTD_EXAMPLE_2048 "T4 2048.c 예제 빌드 (구성 시 GitHub에서 받아 옴)" ON)
if(NOT MTD_EXAMPLE_2048)
  return()
endif()

include(FetchContent)
FetchContent_Declare(src2048
  GIT_REPOSITORY https://github.com/mevdschee/2048.c.git
  GIT_TAG        90343eb209e84fa9687d596f5d5eecfdf7d4df51)
FetchContent_GetProperties(src2048)
if(NOT src2048_POPULATED)
  FetchContent_Populate(src2048)
endif()

find_package(Git REQUIRED)
set(PATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/2048-patched)
file(REMOVE_RECURSE ${PATCHED_DIR})
file(COPY ${src2048_SOURCE_DIR}/2048.c DESTINATION ${PATCHED_DIR})
execute_process(
  COMMAND ${GIT_EXECUTABLE} apply --whitespace=nowarn ${CMAKE_CURRENT_SOURCE_DIR}/2048/mtd.patch
  WORKING_DIRECTORY ${PATCHED_DIR}
  RESULT_VARIABLE patch_result)
if(NOT patch_result EQUAL 0)
  message(FATAL_ERROR "examples/2048/mtd.patch failed to apply")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/2048/mtd.patch)

add_executable(game2048 ${PATCHED_DIR}/2048.c)
target_compile_options(game2048 PRIVATE -Wno-error)
target_link_libraries(game2048 PRIVATE mtd)
set_target_properties(game2048 PROPERTIES LINKER_LANGUAGE CXX OUTPUT_NAME 2048)

# 원본의 자체 테스트 모드(`2048 test`): 패치 후에도 게임 규칙(슬라이드·합치기)이 그대로인지 확인
add_test(NAME example_2048 COMMAND game2048 test)
