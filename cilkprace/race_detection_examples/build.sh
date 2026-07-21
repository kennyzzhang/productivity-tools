#!/usr/bin/env bash

usage() {
  echo "Usage: $0 [-j core count] /path/to/opencilk [build dir]"
  exit 1
}

getopt -T
if (( "$?" == 4 )); then
  params="$(getopt -o h?j: -n "$0" -- "$@")"
  if (( "$?" != 0 )); then
    usage
  fi
  eval set -- "$params"
else
  echo "getopt not found, defaulting to getopts"
fi

CORE_COUNT=1

while getopts ":h?j:" o; do
  case "$o" in
    "h" | "?")
      usage
      ;;
    j)
      CORE_COUNT="$OPTARG"
      ;;
    *)
      usage
      ;;
  esac
done
shift $((OPTIND-1))

if (( "$#" < 1 )); then
  usage
fi

OPENCILK="$(realpath -- "$1")"
SOURCE_DIR="$(realpath -- "$(dirname -- "${BASH_SOURCE[0]}")")"
BUILD_DIR="$(realpath -- "${2:-build}")"

if [[ "$SOURCE_DIR" == "$BUILD_DIR" ]]; then
  echo "Refusing to build in same directory as source"
  usage
fi

echo $OPENCILK $SOURCE_DIR $BUILD_DIR

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
pwd
cmake                                                \
  -DCMAKE_C_COMPILER="$OPENCILK/bin/clang"           \
  -DCMAKE_CXX_COMPILER="$OPENCILK/bin/clang++"       \
  -DCMAKE_BUILD_TYPE="RelWithDebInfo"                \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG"   \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS="ON"               \
  -DDISABLED_TESTS="fibred;heat;fft"                 \
  "$SOURCE_DIR"

cmake --build . -j "$CORE_COUNT"
