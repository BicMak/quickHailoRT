#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
ARCH=$(uname -m)
BIN="${SCRIPT_DIR}/build/${ARCH}/classifier"

echo "-I- Building ${ARCH}"
cd "${SCRIPT_DIR}"
mkdir -p build/${ARCH}
cmake -S. -Bbuild/${ARCH}
cmake --build build/${ARCH} -j$(nproc)

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

echo ""
echo "-I- Running: ${BIN}"
cd "${ROOT_DIR}"
"${BIN}"
