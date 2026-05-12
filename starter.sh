#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${ROOT_DIR}/config.yaml"
ARCH=$(uname -m)

# LOG_LEVEL: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE
LOG_LEVEL="${LOG_LEVEL:-1}"

if [[ ! -f "${CONFIG}" ]]; then
    echo "-E- config.yaml not found: ${CONFIG}"
    exit 1
fi

TASK=$(grep '^task:' "${CONFIG}" | awk '{print $2}' | tr -d '[:space:]')

if [[ -z "${TASK}" ]]; then
    echo "-E- 'task' field not found in config.yaml"
    exit 1
fi

echo "-I- task=${TASK}  arch=${ARCH}  LOG_LEVEL=${LOG_LEVEL}"

case "${TASK}" in
    classification)
        TASK_DIR="${ROOT_DIR}/src/classification"
        BIN="${TASK_DIR}/build/${ARCH}/classifier"
        ;;
    zero_shot_classification)
        TASK_DIR="${ROOT_DIR}/src/zero_shot_classification"
        BIN="${TASK_DIR}/build/${ARCH}/zero_shot_classification"
        ;;
    *)
        echo "-E- Unknown task: '${TASK}'"
        echo "    Available: classification, zero_shot_classification"
        exit 1
        ;;
esac

echo "-I- Building ${TASK_DIR}"
cd "${TASK_DIR}"
mkdir -p "build/${ARCH}"
cmake -S. -B"build/${ARCH}" -DLOG_LEVEL=${LOG_LEVEL}
cmake --build "build/${ARCH}" -j$(nproc)

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

[[ -f "${TASK_DIR}/hailort.log" ]] && rm "${TASK_DIR}/hailort.log"

echo ""
echo "-I- Running: ${BIN} $*"
cd "${ROOT_DIR}"
"${BIN}" "$@"
