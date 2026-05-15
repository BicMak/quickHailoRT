#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ARCH=$(uname -m)
BIN="${SCRIPT_DIR}/build/${ARCH}/SAHI_object_detection"

# LOG_LEVEL: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE
# usage: LOG_LEVEL=0 ./build.sh
LOG_LEVEL="${LOG_LEVEL:-2}"

echo "-I- Building ${ARCH} (LOG_LEVEL=${LOG_LEVEL})"
cd "${SCRIPT_DIR}"
mkdir -p build/${ARCH}
cmake -S. -Bbuild/${ARCH} -DLOG_LEVEL=${LOG_LEVEL}
cmake --build build/${ARCH} -j$(nproc)

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

echo "-I- Build success: ${BIN}"
echo ""

NET="${SCRIPT_DIR}/hef/yolov8n.hef"
INPUT="${SCRIPT_DIR}/image"
OUTPUT="${SCRIPT_DIR}/output"
mkdir -p "${OUTPUT}"

echo "-I- Running: ${BIN}"
echo "-I-   --net    ${NET}"
echo "-I-   --input  ${INPUT}"
echo "-I-   --output ${OUTPUT}"
echo ""

cd "${SCRIPT_DIR}"
"${BIN}" --net "${NET}" --input "${INPUT}" --output "${OUTPUT}"
