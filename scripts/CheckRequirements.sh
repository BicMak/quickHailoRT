#!/bin/bash
#===============================================================================
# Pre-install validator (Driver + HailoRT only, NO TAPPAS)
#
# Assumption: deb/whl files are downloaded but NOT YET installed.
# Place them under <script_dir>/install_file/{arm,x86}/ — the script picks the
# correct subdir automatically based on `uname -m`.
#
# Verifies the system is ready to install:
#   - hailort-pcie-driver_*_all.deb            (DKMS, arch-independent)
#   - hailort_*_<amd64|arm64>.deb              (runtime, host-arch dependent)
#   - hailort-*-linux_<x86_64|aarch64>.whl     (Python binding, py-tag matched)
#
# Scope: HailoRT only. TAPPAS / GStreamer plugins are intentionally NOT covered.
#
# Usage:
#   ./requireValid.sh           # run all checks
#===============================================================================

set -uo pipefail

#-------------------------------------------------------------------------------
# Colors
#-------------------------------------------------------------------------------
if [[ -t 1 ]]; then
  RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
  CYAN=$'\033[0;36m'; BOLD=$'\033[1m'; NC=$'\033[0m'
else
  RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; NC=''
fi

log_info()    { echo -e "${GREEN}ℹ️  $*${NC}"; }
log_success() { echo -e "${GREEN}✅ $*${NC}"; }
log_warning() { echo -e "${YELLOW}⚠️  $*${NC}"; }
log_error()   { echo -e "${RED}❌ $*${NC}" >&2; }
log_step()    { echo ""; echo -e "${CYAN}${BOLD}━━━ $* ━━━${NC}"; echo ""; }

#-------------------------------------------------------------------------------
# Args
#   Hailo packages are expected at: <script_dir>/install_file/{arm,x86}/
#   The architecture subdir is selected automatically from `uname -m`.
#-------------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
       sed -n '/^#===/,/^#===/p' "$0"; exit 0 ;;
    *) log_error "Unknown arg: $1"; exit 1 ;;
  esac
done

ERRORS=0
WARNINGS=0
fail() { log_error "$*"; ((ERRORS++)); }
warn() { log_warning "$*"; ((WARNINGS++)); }

#-------------------------------------------------------------------------------
# Step 1: Hailo hardware (PCIe presence) — fail fast if board not seated
#   Note: sudo readiness is intentionally NOT checked here — validation is a
#   read-only operation. sudo is only needed at install time and will be
#   verified by the install phase.
#-------------------------------------------------------------------------------
log_step "Step 1: Hailo hardware (PCIe presence)"

if ! command -v lspci >/dev/null 2>&1; then
  warn "lspci not found — cannot verify hardware (install with: sudo apt-get install -y pciutils)"
else
  # Match by PCI class (Co-processor) AND vendor name (Hailo) — current Hailo-8
  HAILO_PCI=$(lspci -nn 2>/dev/null | grep -i 'Co-processor' | grep -i 'hailo' || true)
  if [[ -n "$HAILO_PCI" ]]; then
    log_success "Hailo device detected on PCIe bus:"
    echo "    $HAILO_PCI"
    # Kernel module attachment ≠ full HailoRT install.
    if lspci -v 2>/dev/null | grep -A3 -i 'Co-processor.*hailo' | grep -q "Kernel driver in use: hailo"; then
      log_info "Kernel module 'hailo' is loaded and bound (HailoRT packages checked separately below)"
    else
      log_info "Kernel module not yet bound — expected before driver .deb is installed"
    fi
  else
    fail "No Hailo device found on PCIe bus"
    echo "    Check: physical seating, M.2/HAT connection, power, ribbon cable"
    echo "    Expected: 'Co-processor: Hailo Technologies Ltd. Hailo-8 AI Processor'"
    echo "    Run manually: lspci | grep -i hailo"
  fi
fi

#-------------------------------------------------------------------------------
# Step 2: Kernel headers (PCIe driver build via DKMS)
#   Lowest software layer above the kernel — driver compiles against these.
#-------------------------------------------------------------------------------
log_step "Step 2: Kernel headers (for PCIe driver build)"

KERNEL=$(uname -r)
log_info "Running kernel: $KERNEL"

if [[ -d "/lib/modules/$KERNEL/build" ]]; then
  log_success "Kernel headers present at /lib/modules/$KERNEL/build"
else
  fail "Kernel headers missing for $KERNEL"
  echo "  Install with:"
  echo "  sudo apt-get install -y linux-headers-$(uname -r) || sudo apt-get install -y raspberrypi-kernel-headers"
fi

#-------------------------------------------------------------------------------
# Step 3: System packages (build toolchain + DKMS + Python — NO TAPPAS deps)
#   With kernel headers present, the next layer is the userspace toolchain
#   that actually compiles the driver and supports Python.
#-------------------------------------------------------------------------------
log_step "Step 3: System packages (build toolchain + DKMS + Python)"

CORE_PKGS=(
  # Python (pip + venv + virtualenv + dev headers)
  python3 python3-dev python3-pip python3-venv python3-virtualenv python3-setuptools
  # Build toolchain (PCIe driver compile + general builds)
  build-essential cmake pkg-config git rsync
  # DKMS stack (needed by hailort-pcie-driver to compile against running kernel)
  dkms bison flex libelf-dev
)

APT_MISSING=()
for p in "${CORE_PKGS[@]}"; do
  if dpkg -l "$p" 2>/dev/null | grep -q "^ii"; then
    printf "  ${GREEN}[OK]${NC}      %s\n" "$p"
  else
    printf "  ${RED}[MISSING]${NC} %s\n" "$p"
    APT_MISSING+=("$p")
  fi
done

if [[ ${#APT_MISSING[@]} -gt 0 ]]; then
  fail "${#APT_MISSING[@]} apt package(s) missing"
  echo ""
  echo "  Install with:"
  echo "  sudo apt-get install -y ${APT_MISSING[*]}"
fi

#-------------------------------------------------------------------------------
# Step 4: Python interpreter sanity
#   With Python installed (Step 3), check the version is one HailoRT wheels
#   ship for. Wheel-vs-interpreter tag matching happens in Step 5, after we
#   know which wheel file we have.
#-------------------------------------------------------------------------------
log_step "Step 4: Python interpreter"

PY_VER=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>/dev/null || echo "")
if [[ -z "$PY_VER" ]]; then
  fail "python3 not callable"
else
  log_info "python3 version: $PY_VER"
  case "$PY_VER" in
    3.10|3.11|3.12) log_success "Python version is in the supported range (3.10-3.12)" ;;
    *) warn "Python $PY_VER may not match HailoRT wheel — verify whl filename has cp${PY_VER//./}" ;;
  esac
fi

# Version-specific Python dev (e.g. python3.12-dev)
if [[ -n "$PY_VER" ]]; then
  pkg="python${PY_VER}-dev"
  if dpkg -l "$pkg" 2>/dev/null | grep -q "^ii"; then
    log_success "$pkg installed"
  else
    warn "$pkg not installed (generic python3-dev should still cover it)"
  fi
fi

#-------------------------------------------------------------------------------
# Step 5: Hailo deb/whl files exist (3 expected files per Hailo docs)
#   1. hailort-pcie-driver_<ver>_all.deb     — DKMS, arch-independent
#   2. hailort_<ver>_<deb_arch>.deb          — runtime, host arch must match
#   3. hailort-<ver>-cp<py>-cp<py>-linux_<whl_arch>.whl — Python binding
#
#   Done last because it depends on host arch (Steps above) AND Python version
#   (Step 4) being known. If those checks failed, this step is meaningless.
#
# Layout convention: install_file/{arm,x86}/<files>
#   - aarch64 / armv7l hosts read from install_file/arm/
#   - x86_64 hosts read from install_file/x86/
#-------------------------------------------------------------------------------
log_step "Step 5: Hailo install packages (deb/whl)"

# Auto-detect host architecture, derive Hailo file-name tokens AND the
# matching subdirectory under install_file/.
HOST_ARCH=$(uname -m)
case "$HOST_ARCH" in
  x86_64)   DEB_ARCH="amd64";  WHL_ARCH="x86_64";  ARCH_DIR="x86" ;;
  aarch64)  DEB_ARCH="arm64";  WHL_ARCH="aarch64"; ARCH_DIR="arm" ;;
  armv7l)   DEB_ARCH="armel";  WHL_ARCH="";        ARCH_DIR="arm" ;;  # no whl for armv7
  *)        DEB_ARCH="";       WHL_ARCH="";        ARCH_DIR=""
            fail "Unsupported host architecture: $HOST_ARCH (Hailo supports x86_64, aarch64, armv7l)"
            ;;
esac
log_info "Host architecture: $HOST_ARCH (deb token: ${DEB_ARCH:-?}, whl token: ${WHL_ARCH:-N/A})"

# Resolve install dir relative to the script, so the script is portable.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_BASE="$SCRIPT_DIR/install_file"
INSTALL_DIR=""
if [[ -n "$ARCH_DIR" ]]; then
  INSTALL_DIR="$INSTALL_BASE/$ARCH_DIR"
  if [[ -d "$INSTALL_DIR" ]]; then
    log_info "Looking for Hailo packages in: $INSTALL_DIR"
  else
    fail "Expected install dir not found: $INSTALL_DIR"
    echo "    Create it and place the Hailo files for $HOST_ARCH there:"
    echo "      mkdir -p $INSTALL_DIR"
    INSTALL_DIR=""
  fi
fi

# Logical keys (driver/runtime/wheel) so later steps don't depend on literal patterns.
declare -A EXPECTED_PATTERN
declare -A EXPECTED_DESC
EXPECTED_PATTERN[driver]="hailort-pcie-driver_*_all.deb"
EXPECTED_DESC[driver]="HailoRT PCIe driver (.deb, arch-independent / DKMS)"
if [[ -n "$DEB_ARCH" ]]; then
  EXPECTED_PATTERN[runtime]="hailort_*_${DEB_ARCH}.deb"
  EXPECTED_DESC[runtime]="HailoRT runtime (.deb, $DEB_ARCH)"
fi
if [[ -n "$WHL_ARCH" ]]; then
  EXPECTED_PATTERN[wheel]="hailort-*-linux_${WHL_ARCH}.whl"
  EXPECTED_DESC[wheel]="PyHailoRT (.whl, $WHL_ARCH)"
fi

declare -A FOUND_PATHS
if [[ -n "$INSTALL_DIR" ]]; then
  echo ""
  for key in "${!EXPECTED_PATTERN[@]}"; do
    pattern="${EXPECTED_PATTERN[$key]}"
    desc="${EXPECTED_DESC[$key]}"
    # Look only inside the arch-specific directory (no recursion needed).
    hit=$(find "$INSTALL_DIR" -maxdepth 1 -name "$pattern" -type f 2>/dev/null | head -1)
    if [[ -n "$hit" ]]; then
      log_success "${desc}"
      echo "    → $hit"
      FOUND_PATHS[$key]="$hit"
    else
      fail "${desc} NOT FOUND (looked for: $INSTALL_DIR/$pattern)"
    fi
  done

  # Sanity: warn if files for the OTHER architecture are mistakenly mixed in here.
  other_arch=""
  case "$ARCH_DIR" in
    arm) other_arch="x86" ;;
    x86) other_arch="arm" ;;
  esac
  if [[ -n "$other_arch" ]]; then
    case "$ARCH_DIR" in
      arm) wrong=$(find "$INSTALL_DIR" -maxdepth 1 \( -name '*_amd64.deb' -o -name '*linux_x86_64.whl' \) 2>/dev/null) ;;
      x86) wrong=$(find "$INSTALL_DIR" -maxdepth 1 \( -name '*_arm64.deb' -o -name '*_armel.deb' -o -name '*linux_aarch64.whl' \) 2>/dev/null) ;;
    esac
    if [[ -n "$wrong" ]]; then
      warn "Files for the wrong architecture found in $INSTALL_DIR (move them to install_file/$other_arch/):"
      echo "$wrong" | sed 's/^/    /'
    fi
  fi
fi

# Version consistency: runtime .deb and wheel must share the same version string
if [[ -n "${FOUND_PATHS[runtime]:-}" && -n "${FOUND_PATHS[wheel]:-}" ]]; then
  deb_ver=$(basename "${FOUND_PATHS[runtime]}" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
  whl_ver=$(basename "${FOUND_PATHS[wheel]}"   | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
  if [[ "$deb_ver" != "$whl_ver" ]]; then
    fail "HailoRT version mismatch: runtime=$deb_ver vs wheel=$whl_ver (must be identical)"
  else
    log_success "HailoRT versions match: $deb_ver"
  fi
fi

# Wheel ↔ Python interpreter tag match (needs both Step 4 PY_VER and the wheel from above)
if [[ -n "${FOUND_PATHS[wheel]:-}" && -n "$PY_VER" ]]; then
  whl_name=$(basename "${FOUND_PATHS[wheel]}")
  py_tag="cp${PY_VER//./}"
  if [[ "$whl_name" == *"$py_tag"* ]]; then
    log_success "PyHailoRT wheel matches Python $PY_VER ($py_tag)"
  else
    fail "PyHailoRT wheel ($whl_name) doesn't match Python $PY_VER (expected tag: $py_tag)"
  fi
fi

#-------------------------------------------------------------------------------
# Step 6: Disk space
#-------------------------------------------------------------------------------
log_step "Step 6: Disk space"

HOME_FREE_MB=$(df -m "$HOME" | awk 'NR==2 {print $4}')
ROOT_FREE_MB=$(df -m / | awk 'NR==2 {print $4}')

log_info "Free in \$HOME ($HOME): ${HOME_FREE_MB} MB"
log_info "Free in /:             ${ROOT_FREE_MB} MB"

if [[ "$HOME_FREE_MB" -lt 1024 ]]; then
  fail "Less than 1GB free in \$HOME"
elif [[ "$HOME_FREE_MB" -lt 3072 ]]; then
  warn "Less than 3GB free in \$HOME — venv + models may run tight"
else
  log_success "Sufficient disk space in \$HOME"
fi

if [[ "$ROOT_FREE_MB" -lt 512 ]]; then
  fail "Less than 512MB free in / — system .deb installs may fail"
fi

#-------------------------------------------------------------------------------
# Step 7: Network (apt + pip)
#-------------------------------------------------------------------------------
log_step "Step 7: Network connectivity"

if curl -sSf --max-time 5 https://pypi.org/ -o /dev/null 2>&1; then
  log_success "pypi.org reachable"
else
  warn "pypi.org unreachable — pip installs will fail"
fi

if curl -sSf --max-time 5 http://archive.ubuntu.com/ -o /dev/null 2>&1 \
   || curl -sSf --max-time 5 http://deb.debian.org/ -o /dev/null 2>&1; then
  log_success "Distro mirror reachable"
else
  warn "Distro mirror unreachable — apt-get may fail"
fi

#-------------------------------------------------------------------------------
# Step 8: Conflict check (prior Hailo installs)
#-------------------------------------------------------------------------------
log_step "Step 8: Conflict check"

PRIOR=$(dpkg -l 2>/dev/null | awk '/^ii/ && /hailo/ {print "  " $2 " " $3}')
if [[ -n "$PRIOR" ]]; then
  warn "Existing Hailo packages detected (consider purging for a clean install):"
  echo "$PRIOR"
else
  log_success "No prior Hailo packages found — clean slate"
fi

#-------------------------------------------------------------------------------
# Final verdict
#-------------------------------------------------------------------------------
echo ""
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  Pre-Install Validation Summary (Driver + HailoRT)${NC}"
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "  Errors:   ${RED}${ERRORS}${NC}"
echo -e "  Warnings: ${YELLOW}${WARNINGS}${NC}"
echo ""

if [[ $ERRORS -eq 0 ]]; then
  log_success "Ready to install. Suggested order:"
  echo ""
  echo "  1. sudo dpkg -i ${FOUND_PATHS[driver]:-<pcie-driver_all.deb>}"
  echo "  2. sudo dpkg -i ${FOUND_PATHS[runtime]:-<hailort_${DEB_ARCH:-<arch>}.deb>}"
  echo "  3. python3 -m venv --system-site-packages ~/myproject/venv"
  echo "  4. source ~/myproject/venv/bin/activate"
  echo "  5. pip install ${FOUND_PATHS[wheel]:-<hailort-*.whl>}"
  exit 0
else
  log_error "Fix the $ERRORS error(s) above before installing."
  exit 1
fi
