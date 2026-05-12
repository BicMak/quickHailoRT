#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
ARCH=$(uname -m)
BUILD_DIR="${SCRIPT_DIR}/build/${ARCH}"
BIN="${BUILD_DIR}/zero_shot_classification"

# LOG_LEVEL: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE
# usage: LOG_LEVEL=0 ./build.sh
#        ./build.sh --input local_data/some.jpg --prompt "a cat" --prompt "a dog"
LOG_LEVEL="${LOG_LEVEL:-1}"

echo "-I- Building ${ARCH} (LOG_LEVEL=${LOG_LEVEL})"
cd "${SCRIPT_DIR}"
mkdir -p "${BUILD_DIR}"
cmake -S. -B"${BUILD_DIR}" -DLOG_LEVEL=${LOG_LEVEL}
cmake --build "${BUILD_DIR}" -j$(nproc)

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

# clean stale hailort log from prior runs
[[ -f "${SCRIPT_DIR}/hailort.log" ]] && rm "${SCRIPT_DIR}/hailort.log"

echo ""
echo "-I- Running: ${BIN} $*"
cd "${ROOT_DIR}"
"${BIN}" "$@"
