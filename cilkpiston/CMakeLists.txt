# Build for the CilkPiston runtime support library.

set(CILKPISTON_SOURCES
  cilkpiston.cpp
  csanrt.cpp
  debug_util.cpp
  driver.cpp
  libhooks.cpp
  locking.cpp
  print_addr.cpp
  reducers.cpp)

set(CILKPISTON_BITCODE_SOURCE
  driver.cpp
  libhooks.cpp
  reducers.cpp)

set(CILKPISTON_RR_FILES
  rr_commands.py)

include_directories(${CILKTOOLS_SOURCE_DIR}/include)

set(CILKPISTON_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_list_if(CILKTOOLS_HAS_CILK -fopencilk CILKPISTON_CFLAGS)
append_list_if(CILKTOOLS_HAS_FNO_EXCEPTIONS_FLAG -fno-exceptions CILKPISTON_CFLAGS)
append_list_if(CILKTOOLS_HAS_FDEBUG_DEFAULT_VERSION_EQ_4_FLAG
  -fdebug-default-version=4 CILKPISTON_CFLAGS)
append_rtti_flag(OFF CILKPISTON_CFLAGS)

set(CILKPISTON_OBJ_DEPS)

set(CILKPISTON_DYNAMIC_DEFINITIONS ${CILKPISTON_COMMON_DEFINITIONS})

set(CILKPISTON_DYNAMIC_CFLAGS ${CILKPISTON_CFLAGS})

set(CILKPISTON_COMMON_LIBS ${SANITIZER_CXX_ABI_LIBRARY} ${SANITIZER_COMMON_LINK_LIBS})

set(CILKPISTON_DYNAMIC_LIBS ${CILKPISTON_COMMON_LIBS})
append_list_if(CILKTOOLS_HAS_LIBDL dl CILKPISTON_DYNAMIC_LIBS)
append_list_if(CILKTOOLS_HAS_LIBPTHREAD pthread CILKPISTON_DYNAMIC_LIBS)

set(CILKPISTON_COMMON_LINK_FLAGS)
append_list_if(CILKTOOLS_HAS_CILK -fopencilk CILKPISTON_COMMON_LINK_FLAGS)

set(CILKPISTON_STATIC_LINK_FLAGS ${CILKPISTON_COMMON_LINK_FLAGS})
append_list_if(CILKTOOLS_HAS_CILK
  -Wl,--wrap,__cilkrts_internal_merge_two_rmaps CILKPISTON_STATIC_LINK_FLAGS)

set(CILKPISTON_DYNAMIC_LINK_FLAGS ${CILKPISTON_COMMON_LINK_FLAGS})

# Setup flags and defs for cilktool bitcode build
set(CILKPISTON_BITCODE_CFLAGS ${CILKPISTON_CFLAGS} -emit-llvm)
set(CILKPISTON_BITCODE_DEFINITIONS ${CILKPISTON_COMMON_DEFINITIONS}
    "CILKSAN_VIS=__attribute__((noinline))")

# Build CilkPiston runtimes shipped with Clang.
add_cilktools_component(cilkpiston)

if (APPLE)
  add_cilktools_bitcode(cilkpiston
    OS ${CILKTOOL_SUPPORTED_OS}
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    SOURCES ${CILKPISTON_BITCODE_SOURCE}
    CFLAGS ${CILKPISTON_BITCODE_CFLAGS}
    DEFS ${CILKPISTON_BITCODE_DEFINITIONS}
    PARENT_TARGET cilkpiston)

  # Compile CilkPiston sources into an object library
  add_cilktools_object_libraries(RTCilkPiston_dynamic
    OS ${CILKTOOL_SUPPORTED_OS}
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    SOURCES ${CILKPISTON_SOURCES}
    CFLAGS ${CILKPISTON_DYNAMIC_CFLAGS}
    DEFS ${CILKPISTON_DYNAMIC_DEFINITIONS}
    DEPS ${CILKPISTON_OBJ_DEPS})

  add_cilktools_object_libraries(RTCilkPiston
    OS ${CILKTOOL_SUPPORTED_OS}
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    SOURCES ${CILKPISTON_SOURCES}
    CFLAGS ${CILKPISTON_CFLAGS}
    DEFS ${CILKPISTON_COMMON_DEFINITIONS}
    DEPS ${CILKPISTON_OBJ_DEPS})

  add_cilktools_runtime(clang_rt.cilkpiston
    STATIC
    OS ${CILKTOOL_SUPPORTED_OS}
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    OBJECT_LIBS RTCilkPiston
    CFLAGS ${CILKPISTON_CFLAGS}
    LINK_FLAGS ${CILKPISTON_STATIC_LINK_FLAGS}
    DEFS ${CILKPISTON_COMMON_DEFINITIONS}
    PARENT_TARGET cilkpiston)

  add_cilktools_runtime(clang_rt.cilkpiston
    SHARED
    OS ${CILKTOOL_SUPPORTED_OS}
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    OBJECT_LIBS RTCilkPiston_dynamic
    CFLAGS ${CILKPISTON_DYNAMIC_CFLAGS}
    LINK_FLAGS ${CILKPISTON_DYNAMIC_LINK_FLAGS}
    LINK_LIBS ${CILKPISTON_DYNAMIC_LIBS}
    DEFS ${CILKPISTON_DYNAMIC_DEFINITIONS}
    PARENT_TARGET cilkpiston)

else() # Not APPLE
  # Compile CilkPiston sources into an object library
  add_cilktools_object_libraries(RTCilkPiston_dynamic
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    SOURCES ${CILKPISTON_SOURCES}
    CFLAGS ${CILKPISTON_DYNAMIC_CFLAGS}
    DEFS ${CILKPISTON_DYNAMIC_DEFINITIONS}
    DEPS ${CILKPISTON_OBJ_DEPS})

  add_cilktools_object_libraries(RTCilkPiston
    ARCHS ${CILKPISTON_SUPPORTED_ARCH}
    SOURCES ${CILKPISTON_SOURCES}
    CFLAGS ${CILKPISTON_CFLAGS}
    DEFS ${CILKPISTON_COMMON_DEFINITIONS}
    DEPS ${CILKPISTON_OBJ_DEPS})

  foreach (arch ${CILKPISTON_SUPPORTED_ARCH})
    add_cilktools_bitcode(cilkpiston
      ARCHS ${arch}
      SOURCES ${CILKPISTON_BITCODE_SOURCE}
      CFLAGS ${CILKPISTON_BITCODE_CFLAGS}
      DEFS ${CILKPISTON_BITCODE_DEFINITIONS}
      PARENT_TARGET cilkpiston)

    add_cilktools_runtime(clang_rt.cilkpiston
      STATIC
      ARCHS ${arch}
      OBJECT_LIBS RTCilkPiston
      CFLAGS ${CILKPISTON_CFLAGS}
      LINK_FLAGS ${CILKPISTON_STATIC_LINK_FLAGS}
      DEFS ${CILKPISTON_COMMON_DEFINITIONS}
      PARENT_TARGET cilkpiston)

    add_cilktools_runtime(clang_rt.cilkpiston
      SHARED
      ARCHS ${arch}
      OBJECT_LIBS RTCilkPiston_dynamic
      CFLAGS ${CILKPISTON_DYNAMIC_CFLAGS}
      LINK_FLAGS ${CILKPISTON_DYNAMIC_LINK_FLAGS}
      LINK_LIBS ${CILKPISTON_DYNAMIC_LIBS}
      DEFS ${CILKPISTON_DYNAMIC_DEFINITIONS}
      PARENT_TARGET cilkpiston)
  endforeach()
endif()

if (CILKTOOLS_INCLUDE_TESTS)
  # TODO: add tests
endif()

foreach (file ${CILKPISTON_RR_FILES})
  install(PROGRAMS ${file}
    DESTINATION share/cilkpiston
    COMPONENT cilkpiston-rr)
endforeach (file)

add_custom_target(cilkpiston-rr DEPENDS ${CILKPISTON_RR_FILES})
add_cilktools_install_targets(cilkpiston-rr PARENT_TARGET cilkpiston)
