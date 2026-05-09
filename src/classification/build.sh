#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
ARCH=$(uname -m)
BIN="${SCRIPT_DIR}/build/${ARCH}/classifier"

# LOG_LEVEL: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE
# usage: LOG_LEVEL=0 ./build.sh
LOG_LEVEL="${LOG_LEVEL:-0}"

echo "-I- Building ${ARCH} (LOG_LEVEL=${LOG_LEVEL})"
cd "${SCRIPT_DIR}"
mkdir -p build/${ARCH}
cmake -S. -Bbuild/${ARCH} -DLOG_LEVEL=${LOG_LEVEL}
cmake --build build/${ARCH} -j$(nproc)

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

echo ""
echo "-I- Running: ${BIN}"
cd "${ROOT_DIR}"
"${BIN}"
