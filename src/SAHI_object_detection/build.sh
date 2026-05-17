#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ARCH=$(uname -m)

# LOG_LEVEL: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE
# usage: LOG_LEVEL=0 ./build.sh --mode image
LOG_LEVEL="${LOG_LEVEL:-2}"

MODE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode)
            MODE="$2"; shift 2 ;;
        --mode=*)
            MODE="${1#--mode=}"; shift ;;
        -h|--help)
            echo "Usage: $0 --mode {image|video}"
            exit 0 ;;
        *)
            echo "-E- Unknown argument: $1"
            echo "Usage: $0 --mode {image|video}"
            exit 1 ;;
    esac
done

case "${MODE}" in
    image) TARGET="SAHI_object_detection" ;;
    video) TARGET="SAHI_object_detection_video" ;;
    "")
        echo "-E- --mode is required (image|video)"
        echo "Usage: $0 --mode {image|video}"
        exit 1 ;;
    *)
        echo "-E- Invalid --mode '${MODE}' (expected: image|video)"
        exit 1 ;;
esac

BIN="${SCRIPT_DIR}/build/${ARCH}/${TARGET}"

echo "-I- Building ${TARGET} for ${ARCH} (LOG_LEVEL=${LOG_LEVEL})"
cd "${SCRIPT_DIR}"
mkdir -p build/${ARCH}
cmake -S. -Bbuild/${ARCH} -DLOG_LEVEL=${LOG_LEVEL}
cmake --build build/${ARCH} -j$(nproc) --target ${TARGET}

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

echo "-I- Build success: ${BIN}"
echo ""
echo "-I- Running: ${BIN}  (cwd=${REPO_ROOT}, using config.yaml)"
echo ""

cd "${REPO_ROOT}"
"${BIN}"
