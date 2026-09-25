# Shadow label implementation for cilkprace
set(CILKPRACE_LABEL_IMPL "leb8-single" CACHE STRING "Shadow label implementation for cilkprace (leb8-single, leb8-range, string, leb8-ptr, or leb8-short-ptr)")
set_property(CACHE CILKPRACE_LABEL_IMPL PROPERTY STRINGS "leb8-single" "leb8-range" "string" "leb8-ptr" "leb8-short-ptr")

if (CILKPRACE_LABEL_IMPL STREQUAL "leb8-range")
  set(CILKPRACE_LABEL_SRC leb8-range.cpp)
  set(CILKPRACE_LABEL_DEF "USE_OS_LABEL_LEB8_RANGE=1")
elseif (CILKPRACE_LABEL_IMPL STREQUAL "string")
  set(CILKPRACE_LABEL_SRC string-label.cpp)
  set(CILKPRACE_LABEL_DEF "USE_OS_LABEL_STRING=1")
elseif (CILKPRACE_LABEL_IMPL STREQUAL "leb8-ptr")
  # leb8-anchor labels in a table, with one-word shadow entries (leb8-ptr.h).
  # cheetah/runtime/CMakeLists.txt builds the runtime's labels to match.
  set(CILKPRACE_LABEL_SRC leb8-ptr.cpp)
  set(CILKPRACE_LABEL_DEF "USE_OS_LABEL_LEB8_ANCHOR=1" "CILKPRACE_USE_LEB8_PTR=1")
elseif (CILKPRACE_LABEL_IMPL STREQUAL "leb8-short-ptr")
  # leb8-single labels behind packed pointers in one-word shadow entries
  # (leb8-short-ptr.h).
  set(CILKPRACE_LABEL_SRC leb8-short-ptr.cpp)
  set(CILKPRACE_LABEL_DEF "USE_OS_LABEL_LEB8_SINGLE=1" "CILKPRACE_USE_LEB8_SHORT_PTR=1")
else()
  set(CILKPRACE_LABEL_SRC leb8-single.cpp)
  set(CILKPRACE_LABEL_DEF "USE_OS_LABEL_LEB8_SINGLE=1")
endif()

set(CILKPRACE_SOURCES
  cilkprace.cpp
  ${CILKPRACE_LABEL_SRC}
  cilkprace_csi.cpp
  csirt.cpp
  cilkprace_csan.cpp
  csanrt.cpp
  libhooks.cpp
  debug_util.cpp
  ${CILKTOOLS_SOURCE_DIR}/common-internal/outs_red.cpp
  )

set(CILKPRACE_BITCODE_SOURCE
  cilkprace.cpp
  ${CILKPRACE_LABEL_SRC}
  cilkprace_csi.cpp
  cilkprace_csan.cpp
  libhooks.cpp
  debug_util.cpp
  ${CILKTOOLS_SOURCE_DIR}/common-internal/outs_red.cpp
  )

include_directories(${CILKTOOLS_SOURCE_DIR}/common-internal/include)
include_directories(${CILKTOOLS_SOURCE_DIR}/include)

set(CILKPRACE_CFLAGS ${SANITIZER_COMMON_CFLAGS})
#set(CILKPRACE_CFLAGS ${SANITIZER_COMMON_CFLAGS} -mbmi2)
append_list_if(CILKTOOLS_HAS_CILK -fopencilk CILKPRACE_CFLAGS)
append_list_if(CILKTOOLS_HAS_FDEBUG_DEFAULT_VERSION_EQ_4_FLAG
  -fdebug-default-version=4 CILKPRACE_CFLAGS)
append_rtti_flag(OFF CILKPRACE_CFLAGS)

set(CILKPRACE_GRANULARITY "8" CACHE STRING "Shadow memory granularity in bytes (4 or 8)")
set_property(CACHE CILKPRACE_GRANULARITY PROPERTY STRINGS "4" "8")

set(CILKPRACE_COMMON_DEFINITIONS)
# Label width, defined by cheetah/runtime/CMakeLists.txt. Defaulted here too so
# cilkprace can be configured standalone; the two must match.
set(CILKPRACE_LABEL_WORDS "12" CACHE STRING "Label width in 64-bit words for leb8-single (cheetah + cilkprace)")
list(APPEND CILKPRACE_COMMON_DEFINITIONS ${CILKPRACE_LABEL_DEF} "CILKPRACE_GRANULARITY=${CILKPRACE_GRANULARITY}"
     "CILKPRACE_LABEL_WORDS=${CILKPRACE_LABEL_WORDS}")
append_list_if(CILKTOOLS_HAS_CILK SERIAL_TOOL=0 CILKPRACE_COMMON_DEFINITIONS)

set(CILKPRACE_DYNAMIC_LINK_FLAGS)
append_list_if(CILKTOOLS_HAS_CILK -fopencilk CILKPRACE_DYNAMIC_LINK_FLAGS)
append_list_if(CILKTOOLS_HAS_CILK -fopencilk-enable-pedigrees CILKPRACE_DYNAMIC_LINK_FLAGS)

set(CILKPRACE_STATIC_LINK_FLAGS ${CILKPRACE_DYNAMIC_LINK_FLAGS})

set(CILKPRACE_DYNAMIC_CFLAGS ${CILKPRACE_CFLAGS})
set(CILKPRACE_DYNAMIC_DEFINITIONS ${CILKPRACE_COMMON_DEFINITIONS})

set(CILKPRACE_COMMON_LIBS ${SANITIZER_CXX_ABI_LIBRARY} ${SANITIZER_COMMON_LINK_LIBS})

set(CILKPRACE_DYNAMIC_LIBS ${CILKPRACE_COMMON_LIBS})


# Setup flags and defs for cilktool bitcode build
set(CILKPRACE_BITCODE_CFLAGS ${CILKPRACE_CFLAGS} -emit-llvm -g -O3)
set(CILKPRACE_BITCODE_DEFINITIONS ${CILKPRACE_COMMON_DEFINITIONS}
    "CILKPRACE_VIS=")

# Build Cilkprace runtimes shipped with Clang.
add_cilktools_component(cilkprace)


# I don't fully understand what I did here. TODO FIXME.
if (APPLE)
  add_cilktools_bitcode(cilkprace
      OS ${CILKTOOL_SUPPORTED_OS}
      ARCHS ${CILKPRACE_SUPPORTED_ARCH}
      SOURCES ${CILKPRACE_BITCODE_SOURCE}
      CFLAGS ${CILKPRACE_BITCODE_CFLAGS}
      DEFS ${CILKPRACE_BITCODE_DEFINITIONS}
      PARENT_TARGET cilkprace)

    add_cilktools_object_libraries(RTCilkprace_dynamic
      OS ${CILKTOOL_SUPPORTED_OS} 
      ARCHS ${CILKPRACE_SUPPORTED_ARCH}
      SOURCES ${CILKPRACE_SOURCES}
      CFLAGS ${CILKPRACE_DYNAMIC_CFLAGS}
      DEFS ${CILKPRACE_DYNAMIC_DEFINITIONS}
      DEPS ${CILKPRACE_OBJ_DEPS})

    add_cilktools_object_libraries(RTCilkprace
      OS ${CILKTOOL_SUPPORTED_OS}
      ARCHS ${CILKPRACE_SUPPORTED_ARCH}
      SOURCES ${CILKPRACE_SOURCES}
      CFLAGS ${CILKPRACE_CFLAGS}
      DEFS ${CILKPRACE_COMMON_DEFINITIONS}
      DEPS ${CILKPRACE_OBJ_DEPS})

    add_cilktools_runtime(clang_rt.cilkprace
      STATIC
      OS ${CILKTOOL_SUPPORTED_OS}
      ARCHS ${CILKPRACE_SUPPORTED_ARCH}
      OBJECT_LIBS RTCilkprace
      CFLAGS ${CILKPRACE_CFLAGS}
      DEFS ${CILKPRACE_COMMON_DEFINITIONS}
      PARENT_TARGET cilkprace)

    add_cilktools_runtime(clang_rt.cilkprace
      SHARED
      OS ${CILKTOOL_SUPPORTED_OS}
      ARCHS ${CILKPRACE_SUPPORTED_ARCH}
      OBJECT_LIBS RTCilkprace_dynamic
      CFLAGS ${CILKPRACE_DYNAMIC_CFLAGS}
      LINK_FLAGS ${CILKPRACE_DYNAMIC_LINK_FLAGS}
      LINK_LIBS ${CILKPRACE_DYNAMIC_LIBS}
      DEFS ${CILKPRACE_DYNAMIC_DEFINITIONS}
      PARENT_TARGET cilkprace)
    
else()
  # Compile Cilkprace sources into an object library
  add_cilktools_object_libraries(RTCilkprace_dynamic
    ARCHS ${CILKPRACE_SUPPORTED_ARCH}
    SOURCES ${CILKPRACE_SOURCES}
    CFLAGS ${CILKPRACE_DYNAMIC_CFLAGS}
    DEFS ${CILKPRACE_DYNAMIC_DEFINITIONS}
    DEPS ${CILKPRACE_OBJ_DEPS})

  add_cilktools_object_libraries(RTCilkprace
    ARCHS ${CILKPRACE_SUPPORTED_ARCH}
    SOURCES ${CILKPRACE_SOURCES}
    CFLAGS ${CILKPRACE_CFLAGS}
    DEFS ${CILKPRACE_COMMON_DEFINITIONS}
    DEPS ${CILKPRACE_OBJ_DEPS})

  foreach (arch ${CILKPRACE_SUPPORTED_ARCH})
    add_cilktools_bitcode(cilkprace
      ARCHS ${arch}
      SOURCES ${CILKPRACE_BITCODE_SOURCE}
      CFLAGS ${CILKPRACE_BITCODE_CFLAGS}
      DEFS ${CILKPRACE_BITCODE_DEFINITIONS}
      PARENT_TARGET cilkprace)

    add_cilktools_runtime(clang_rt.cilkprace
      STATIC
      ARCHS ${arch}
      OBJECT_LIBS RTCilkprace
      CFLAGS ${CILKPRACE_CFLAGS}
      LINK_FLAGS ${CILKPRACE_STATIC_LINK_FLAGS}
      DEFS ${CILKPRACE_COMMON_DEFINITIONS}
      PARENT_TARGET cilkprace)

    add_cilktools_runtime(clang_rt.cilkprace
      SHARED
      ARCHS ${arch}
      OBJECT_LIBS RTCilkprace_dynamic
      CFLAGS ${CILKPRACE_DYNAMIC_CFLAGS}
      LINK_FLAGS ${CILKPRACE_DYNAMIC_LINK_FLAGS}
      LINK_LIBS ${CILKPRACE_DYNAMIC_LIBS}
      DEFS ${CILKPRACE_DYNAMIC_DEFINITIONS}
      PARENT_TARGET cilkprace)
  endforeach()
endif()

if (CILKTOOLS_INCLUDE_TESTS)
  # TODO: add tests
endif()
