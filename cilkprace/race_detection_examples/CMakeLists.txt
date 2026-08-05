cmake_minimum_required(VERSION 3.20.0)
project(CilkBenchmarks C CXX)

include(ExternalProject)

#load_cache(${LLVM_CMAKE_DIR} READ_WITH_PREFIX OPENCILK_ LLVM_SOURCE_DIRECTORY)

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
  label_test
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

execute_process(
  COMMAND bash -c "${CMAKE_C_COMPILER} --version | awk '/Target: /{ print $2 }'"
  OUTPUT_VARIABLE default_arch
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(ARCH ${default_arch} CACHE STRING "Native architecture, should match opencilk build architecture")
mark_as_advanced(ARCH)

set(DISABLED_TESTS "" CACHE STRING
  "Semicolon-separated list of tests to exclude from the build (${all_tests})")
set(DISABLED_VARIANTS "" CACHE STRING
  "Semicolon-separated list of variants to exclude from the build (${all_variants})")
set(EXTRA_TARGET_COMMITS "" CACHE STRING
  "Semicolon-separated list of test.variant.commit to build")

list_intersect(disabled_tests all_tests DISABLED_TESTS)
list_intersect(disabled_variants all_variants DISABLED_VARIANTS)

set(DISABLED_TESTS ${disabled_tests} CACHE STRING
  "Semicolon-separated list of tests to exclude from the build (${all_tests})")
set(DISABLED_VARIANTS ${disabled_variants} CACHE STRING
  "Semicolon-separated list of variants to exclude from the build (${all_variants})")

list_difference(tests all_tests disabled_tests)
list_difference(variants all_variants disabled_variants)

# Define interfaces for each type of test/variant.
# This makes it easier to make multiple targets that link different tool
# versions, or skip making targets for an interface altogether.

foreach(test IN LISTS all_tests)
  foreach(variant IN LISTS all_variants)
    add_library(${test}.${variant}.if INTERFACE)
  endforeach()
endforeach()

add_library(getoptions OBJECT getoptions.c)
foreach(test IN LISTS all_needs_getoptions)
  foreach(variant IN LISTS all_variants)
    target_link_libraries(${test}.${variant}.if INTERFACE $<TARGET_OBJECTS:getoptions>)
  endforeach()
endforeach()

foreach(test IN LISTS all_needs_lm)
  foreach(variant IN LISTS all_variants)
    target_link_libraries(${test}.${variant}.if INTERFACE m)
  endforeach()
endforeach()

foreach(test IN LISTS all_tests_c)
  foreach(variant IN LISTS all_variants)
    target_sources(${test}.${variant}.if INTERFACE ${test}.c)
  endforeach()
endforeach()

foreach(test IN LISTS all_tests_cpp)
  foreach(variant IN LISTS all_variants)
    target_sources(${test}.${variant}.if INTERFACE ${test}.cpp)
  endforeach()
endforeach()

foreach(test IN LISTS all_tests)
  target_compile_options(${test}.nocilk.if INTERFACE -ftapir=serial -includecilk/cilk_stub.h)
  target_link_options(${test}.nocilk.if INTERFACE -ftapir=serial)
endforeach()

foreach(test IN LISTS all_tests)
  foreach(variant IN LISTS all_has_cilk)
    target_compile_options(${test}.${variant}.if INTERFACE -fopencilk)
    target_link_options(${test}.${variant}.if INTERFACE -fopencilk)
  endforeach()
endforeach()

foreach(test IN LISTS all_tests)
  foreach(variant IN LISTS all_race_detectors)
    target_compile_options(${test}.${variant}.if INTERFACE "SHELL:-mllvm -enable-static-race-detection=false")
  endforeach()
endforeach()

foreach(test IN LISTS all_tests)
  target_compile_options(${test}.cilksan.if INTERFACE -fsanitize=cilk)
endforeach()

foreach(test IN LISTS all_tests)
  target_compile_options(${test}.cilkprace.if INTERFACE -fsanitize=cilkprace)
  target_link_options(${test}.cilkprace.if INTERFACE -fopencilk-enable-pedigrees)
endforeach()

foreach(test IN LISTS all_tests)
  target_compile_options(${test}.cilkpiston.if INTERFACE -fsanitize=cilkpiston)
endforeach()

foreach(variant IN LISTS all_has_cilk)
  target_compile_options(label_test.${variant}.if INTERFACE -fopencilk-enable-pedigrees)
  target_link_options(label_test.${variant}.if INTERFACE -fopencilk-enable-pedigrees)
endforeach()

# Define meta-targets for timing everything, or a particular test or variant.

add_custom_target(time)

foreach(test IN LISTS tests)
  add_custom_target(${test})
  add_custom_target(${test}.time)
endforeach()

foreach(variant IN LISTS variants)
  add_custom_target(${variant})
  add_custom_target(${variant}.time)
endforeach()

function(add_to_meta_targets target test variant)
  # Incredible jank to create a target for a custom command that has the same
  # name as the output file.
  #
  # If you want target a to build file b, the following works:
  # add_custom_command(OUTPUT b COMMAND ...)
  # add_custom_target(a DEPENDS b)
  #
  # But add_custom_target(a DEPENDS a) makes a circular dependency,
  # instead of specifying that target a depends on output file a.
  #
  # To avoid this shadowing, we rename the output file before running the
  # custom command, and rename it back afterwards.
  add_custom_target(${target}.time.rename_to_tmp
    COMMAND [ -e ${CMAKE_CURRENT_BINARY_DIR}/${target}.time ]
      && mv ${CMAKE_CURRENT_BINARY_DIR}/${target}.time ${CMAKE_CURRENT_BINARY_DIR}/${target}.time.tmp
      || true
    VERBATIM)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}.time.tmp
    COMMAND bash -c "(time ${CMAKE_CURRENT_BINARY_DIR}/${target} &> /dev/null) 2> ${CMAKE_CURRENT_BINARY_DIR}/${target}.time.tmp"
    DEPENDS ${target}
    DEPENDS ${target}.time.rename_to_tmp
    COMMENT "Timing ${target}"
    VERBATIM
  )
  add_custom_target(${target}.time
    COMMAND cmake -E rename ${CMAKE_CURRENT_BINARY_DIR}/${target}.time.tmp ${CMAKE_CURRENT_BINARY_DIR}/${target}.time
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${target}.time.tmp
    VERBATIM
  )
  set_target_properties(${target}.time
    PROPERTIES
    ADDITIONAL_CLEAN_FILES ${CMAKE_CURRENT_BINARY_DIR}/${target}.time
  )

  add_dependencies(${test} ${target})
  add_dependencies(${test}.time ${target}.time)
  add_dependencies(${variant} ${target})
  add_dependencies(${variant}.time ${target}.time)
  add_dependencies(time ${target}.time)
endfunction()

# Define normal targets

foreach(test IN LISTS tests)
  foreach(variant IN LISTS variants)
    add_executable(${test}.${variant})
    target_link_libraries(${test}.${variant} ${test}.${variant}.if)

    add_to_meta_targets(${test}.${variant} ${test} ${variant})
  endforeach()
endforeach()

foreach(test IN LISTS tests)
  target_link_options(${test}.cilksan PRIVATE -fsanitize=cilk)
  target_link_options(${test}.cilkprace PRIVATE -fsanitize=cilkprace)
  target_link_options(${test}.cilkpiston PRIVATE -fsanitize=cilkpiston)
endforeach()

# Define extra targets

set_property(DIRECTORY PROPERTY ADDITIONAL_CLEAN_FILES
  ${CMAKE_CURRENT_BINARY_DIR}/runtimes
)
set(added_commits)
function(add_extra_target_commit extra_test extra_variant extra_commit)
  set(extra_target ${extra_test}.${extra_variant})
  set(extra_target_commit ${extra_target}.${extra_commit})
  set(extra_commit_name cilktools.${extra_commit})

  if(NOT extra_commit IN_LIST added_commits)
    set(added_commits ${added_commits} ${extra_commit} PARENT_SCOPE)
    ExternalProject_Add(
      ${extra_commit_name}
      PREFIX runtimes/${extra_commit_name}
      GIT_REPOSITORY git@github.com:kennyzzhang/productivity-tools.git
      GIT_TAG ${extra_commit}
      CMAKE_ARGS
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DLLVM_CMAKE_DIR=${LLVM_CMAKE_DIR}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON
        #    -DLLVM_COMMON_CMAKE_UTILS=${OPENCILK_LLVM_SOURCE_DIRECTORY}/../cmake
        #    -DLLVM_MAIN_SRC_DIR=${OPENCILK_LLVM_SOURCE_DIRECTORY}
      INSTALL_COMMAND ""
    )
  endif()

  add_executable(${extra_target_commit})
  add_dependencies(${extra_target_commit} ${extra_commit_name})
  target_link_libraries(${extra_target_commit}
    ${extra_target}.if
    ${CMAKE_CURRENT_BINARY_DIR}/runtimes/${extra_commit_name}/src/${extra_commit_name}-build/lib/${ARCH}/libclang_rt.${extra_variant}.a
    stdc++
  )
  add_to_meta_targets(${extra_target_commit} ${extra_test} ${extra_variant})
endfunction()

foreach(extra_target_commits IN LISTS EXTRA_TARGET_COMMITS)
  if(extra_target_commits MATCHES "^([^.]*)\.([^.]*)\.(.*)$")
    set(extra_test ${CMAKE_MATCH_1})
    set(extra_variant ${CMAKE_MATCH_2})
    set(extra_commit ${CMAKE_MATCH_3})
    if(extra_variant IN_LIST variants)
      if(extra_test STREQUAL "*")
        foreach(test IN LISTS tests)
          add_extra_target_commit(${test} ${extra_variant} ${extra_commit})
        endforeach()
      elseif(extra_test IN_LIST tests)
        add_extra_target_commit(${extra_test} ${extra_variant} ${extra_commit})
      endif()
    endif()
  endif()
endforeach()

foreach(test IN LISTS tests)
  foreach(variant IN LISTS variants)
  endforeach()
endforeach()




