cmake_minimum_required(VERSION 3.20.0)
project(CilkBenchmarks C CXX)

set(all_tests_c
  cholesky
  cilksort
  fft
  fib
  heat
  lu
  matmul
  nqueens
  rectmul
  rectmulred
  strassen
)

set(all_tests_cpp
  blah
  conditional_sync
  fibred
  funny_malloc
  heap_reuse
  loop
  memcpy_race
  missed_race
  nqueens_racy
  qsort
  single_spawn_norace
  single_spawn_race
  single_spawn_rr_norace
  single_spawn_rw_race
  stack_reuse
  strdup
)

set(all_tests ${all_tests_c} ${all_tests_cpp})

set(all_variants
  nocilk
  notool
  cilksan
  cilkpiston
  cilkprace
)

set(all_needs_getoptions cholesky cilksort fft heat lu matmul nqueens rectmul rectmulred strassen)
set(all_needs_lm cholesky fft heat)
set(all_has_cilk notool cilksan cilkpiston cilkprace)
set(all_race_detectors cilksan cilkpiston cilkprace)

macro(list_intersect output input1 input2)
  set(${output})
  foreach(it ${${input1}})
    list(FIND ${input2} ${it} index)
    if( NOT (index EQUAL -1))
      list(APPEND ${output} ${it})
    endif()
  endforeach()
endmacro()

macro(list_difference output input1 input2)
  set(${output})
  foreach(it ${${input1}})
    list(FIND ${input2} ${it} index)
    if(index EQUAL -1)
      list(APPEND ${output} ${it})
    endif()
  endforeach()
endmacro()

set(DISABLED_TESTS "" CACHE STRING "Semicolon-separated list of tests to exclude from the build (${all_tests})")
set(DISABLED_VARIANTS "" CACHE STRING "Semicolon-separated list of variants to exclude from the build (${all_variants})")

list_intersect(disabled_tests all_tests DISABLED_TESTS)
list_intersect(disabled_variants all_variants DISABLED_VARIANTS)

set(DISABLED_TESTS ${disabled_tests} CACHE STRING "Semicolon-separated list of tests to exclude from the build (${all_tests})")
set(DISABLED_VARIANTS ${disabled_variants} CACHE STRING "Semicolon-separated list of variants to exclude from the build (${all_variants})")

list_difference(tests all_tests disabled_tests)
list_difference(variants all_variants disabled_variants)

foreach(test IN LISTS tests)
  foreach(variant IN LISTS variants)
    add_executable(${test}.${variant})
  endforeach()
endforeach()

list_intersect(needs_getoptions all_needs_getoptions tests)
add_library(getoptions OBJECT getoptions.c)
foreach(test IN LISTS needs_getoptions)
  foreach(variant IN LISTS variants)
    target_link_libraries(${test}.${variant} PRIVATE $<TARGET_OBJECTS:getoptions>)
  endforeach()
endforeach()

list_intersect(needs_lm all_needs_lm tests)
foreach(test IN LISTS needs_lm)
  foreach(variant IN LISTS variants)
    target_link_libraries(${test}.${variant} PRIVATE m)
  endforeach()
endforeach()

list_intersect(tests_c all_tests_c tests)
foreach(test IN LISTS tests_c)
  foreach(variant IN LISTS variants)
    target_sources(${test}.${variant} PRIVATE ${test}.c)
  endforeach()
endforeach()

list_intersect(tests_cpp all_tests_cpp tests)
foreach(test IN LISTS tests_cpp)
  foreach(variant IN LISTS variants)
    target_sources(${test}.${variant} PRIVATE ${test}.cpp)
  endforeach()
endforeach()

if(nocilk IN_LIST variants)
  foreach(test IN LISTS tests)
    target_compile_options(${test}.nocilk PRIVATE -ftapir=serial -includecilk/cilk_stub.h)
    target_link_options(${test}.nocilk PRIVATE -ftapir=serial)
  endforeach()
endif()

list_intersect(has_cilk all_has_cilk variants)
foreach(test IN LISTS tests)
  foreach(variant IN LISTS has_cilk)
    target_compile_options(${test}.${variant} PRIVATE -fopencilk)
    target_link_options(${test}.${variant} PRIVATE -fopencilk)
  endforeach()
endforeach()

list_intersect(race_detectors all_race_detectors variants)
foreach(test IN LISTS tests)
  foreach(variant IN LISTS race_detectors)
    target_compile_options(${test}.${variant} PRIVATE "SHELL:-mllvm -enable-static-race-detection=false")
  endforeach()
endforeach()

if(cilksan IN_LIST variants)
  foreach(test IN LISTS tests)
    target_compile_options(${test}.cilksan PRIVATE -fsanitize=cilk)
    target_link_options(${test}.cilksan PRIVATE -fsanitize=cilk)
  endforeach()
endif()

if(cilkprace IN_LIST variants)
  foreach(test IN LISTS tests)
    target_compile_options(${test}.cilkprace PRIVATE -fsanitize=cilkprace)
    target_link_options(${test}.cilkprace PRIVATE -fsanitize=cilkprace -fopencilk-enable-pedigrees)
  endforeach()
endif()

if(cilkpiston IN_LIST variants)
  foreach(test IN LISTS tests)
    target_compile_options(${test}.cilkpiston PRIVATE -fsanitize=cilkpiston)
    target_link_options(${test}.cilkpiston PRIVATE -fsanitize=cilkpiston)
  endforeach()
endif()


