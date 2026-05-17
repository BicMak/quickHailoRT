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

TASK=""
PASSTHRU=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --task)
            TASK="$2"
            shift 2
            ;;
        --task=*)
            TASK="${1#--task=}"
            shift
            ;;
        *)
            PASSTHRU+=("$1")
            shift
            ;;
    esac
done

if [[ -z "${TASK}" ]]; then
    TASK=$(grep '^task:' "${CONFIG}" | awk '{print $2}' | tr -d '[:space:]')
fi

if [[ -z "${TASK}" ]]; then
    echo "-E- task not specified (use --task <name> or set 'task:' in config.yaml)"
    exit 1
fi

echo "-I- task=${TASK}  arch=${ARCH}  LOG_LEVEL=${LOG_LEVEL}"

# Extract a scalar value from a top-level YAML section in config.yaml.
# usage: yaml_get <section> <key>
yaml_get() {
    local section="$1" key="$2"
    awk -v sec="${section}" -v k="${key}" '
        $0 ~ "^"sec":"           { in_sec=1; next }
        in_sec && /^[^[:space:]]/ { in_sec=0 }
        in_sec && $1 == k":"      { sub("^[[:space:]]*"k":[[:space:]]*","",$0); sub("[[:space:]]*#.*","",$0); print; exit }
    ' "${CONFIG}"
}

TARGET=""
case "${TASK}" in
    classification)
        TASK_DIR="${ROOT_DIR}/src/classification"
        TARGET="classifier"
        ;;
    zero_shot_classification)
        TASK_DIR="${ROOT_DIR}/src/zero_shot_classification"
        TARGET="zero_shot_classification"
        ;;
    sahi_object_detection)
        TASK_DIR="${ROOT_DIR}/src/SAHI_object_detection"
        MODE=$(yaml_get "sahi_object_detection" "mode")
        case "${MODE}" in
            image) TARGET="SAHI_object_detection" ;;
            video) TARGET="SAHI_object_detection_video" ;;
            "")
                echo "-E- sahi_object_detection.mode not set in config.yaml (expected: image|video)"
                exit 1 ;;
            *)
                echo "-E- Invalid sahi_object_detection.mode '${MODE}' (expected: image|video)"
                exit 1 ;;
        esac
        echo "-I- sahi mode=${MODE}"
        ;;
    *)
        echo "-E- Unknown task: '${TASK}'"
        echo "    Available: classification, zero_shot_classification, sahi_object_detection"
        exit 1
        ;;
esac

BIN="${TASK_DIR}/build/${ARCH}/${TARGET}"

echo "-I- Building ${TASK_DIR} (target=${TARGET})"
cd "${TASK_DIR}"
mkdir -p "build/${ARCH}"
cmake -S. -B"build/${ARCH}" -DLOG_LEVEL=${LOG_LEVEL}
cmake --build "build/${ARCH}" -j$(nproc) --target "${TARGET}"

if [[ ! -x "${BIN}" ]]; then
    echo "-E- Build artifact not found: ${BIN}"
    exit 1
fi

[[ -f "${TASK_DIR}/hailort.log" ]] && rm "${TASK_DIR}/hailort.log"

echo ""
echo "-I- Running: ${BIN} ${PASSTHRU[*]}"
cd "${ROOT_DIR}"
"${BIN}" "${PASSTHRU[@]}"