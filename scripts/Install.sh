#!/bin/bash
#===============================================================================
# HailoRT Installer (Driver + Runtime + Python binding)
#
# Installs from pre-downloaded packages in install_file/{arm,x86}/
# Run CheckRequirements.sh first to verify prerequisites.
#
# Usage:
#   sudo ./install.sh
#===============================================================================

set -euo pipefail

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
# Must run as root
#-------------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
  log_error "Run with sudo: sudo ./install.sh"
  exit 1
fi

REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME=$(eval echo "~$REAL_USER")

#-------------------------------------------------------------------------------
# Resolve install_file dir (same logic as CheckRequirements.sh)
#-------------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

HOST_ARCH=$(uname -m)
case "$HOST_ARCH" in
  x86_64)  DEB_ARCH="amd64";  WHL_ARCH="x86_64";  ARCH_DIR="x86" ;;
  aarch64) DEB_ARCH="arm64";  WHL_ARCH="aarch64"; ARCH_DIR="arm" ;;
  armv7l)  DEB_ARCH="armel";  WHL_ARCH="";        ARCH_DIR="arm" ;;
  *)
    log_error "Unsupported architecture: $HOST_ARCH"
    exit 1
    ;;
esac

INSTALL_DIR="$SCRIPT_DIR/install_file/$ARCH_DIR"

if [[ ! -d "$INSTALL_DIR" ]]; then
  log_error "Install directory not found: $INSTALL_DIR"
  exit 1
fi

#-------------------------------------------------------------------------------
# Find the 3 package files
#-------------------------------------------------------------------------------
DRIVER_DEB=$(find "$INSTALL_DIR" -maxdepth 1 -name "hailort-pcie-driver_*_all.deb" -type f | head -1)
RUNTIME_DEB=$(find "$INSTALL_DIR" -maxdepth 1 -name "hailort_*_${DEB_ARCH}.deb" -type f | head -1)
WHEEL=$(find "$INSTALL_DIR" -maxdepth 1 -name "hailort-*-linux_${WHL_ARCH}.whl" -type f | head -1)

if [[ -z "$DRIVER_DEB" || -z "$RUNTIME_DEB" ]]; then
  log_error "Required .deb files not found in $INSTALL_DIR"
  log_error "Run ./CheckRequirements.sh to diagnose"
  exit 1
fi

echo ""
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  HailoRT Installer${NC}"
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo ""
log_info "Architecture : $HOST_ARCH"
log_info "Driver  .deb : $(basename "$DRIVER_DEB")"
log_info "Runtime .deb : $(basename "$RUNTIME_DEB")"
[[ -n "$WHEEL" ]] && log_info "Python  .whl : $(basename "$WHEEL")" || log_warning "No .whl found — skipping Python binding"
echo ""

#-------------------------------------------------------------------------------
# Step 1: PCIe driver (DKMS)
#-------------------------------------------------------------------------------
log_step "Step 1: PCIe driver"

dpkg -i "$DRIVER_DEB"
log_success "PCIe driver installed"

#-------------------------------------------------------------------------------
# Step 2: HailoRT runtime
#-------------------------------------------------------------------------------
log_step "Step 2: HailoRT runtime"

dpkg -i "$RUNTIME_DEB"
log_success "HailoRT runtime installed"

#-------------------------------------------------------------------------------
# Step 3: Python binding (venv)
#-------------------------------------------------------------------------------
if [[ -n "$WHEEL" ]]; then
  log_step "Step 3: Python binding"

  VENV_PATH="$SCRIPT_DIR/venv_hailoRT"

  if [[ ! -d "$VENV_PATH" ]]; then
    log_info "Creating venv at $VENV_PATH"
    sudo -u "$REAL_USER" python3 -m venv --system-site-packages "$VENV_PATH"
  else
    log_info "Reusing existing venv at $VENV_PATH"
  fi

  sudo -u "$REAL_USER" "$VENV_PATH/bin/pip" install --upgrade pip -q
  sudo -u "$REAL_USER" "$VENV_PATH/bin/pip" install "$WHEEL"
  log_success "PyHailoRT installed in $VENV_PATH"
fi

#-------------------------------------------------------------------------------
# Done
#-------------------------------------------------------------------------------
echo ""
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  Installation Complete${NC}"
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo ""
log_success "HailoRT $( dpkg -l hailort 2>/dev/null | awk '/^ii/{print $3}' ) installed"
echo ""
if [[ -n "$WHEEL" ]]; then
  log_info "To use Python binding:"
  echo "    source $SCRIPT_DIR/venv_hailoRT/bin/activate"
  echo ""
fi
log_warning "Reboot required to load the PCIe driver"
log_info "After reboot, run:  sudo ./Verify.sh"
echo ""
