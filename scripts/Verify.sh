#!/bin/bash
#===============================================================================
# HailoRT Verification
#
# Run after reboot to confirm driver, device, and Python binding are working.
#
# Usage:
#   sudo ./Verify.sh
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
  log_error "Run with sudo: sudo ./Verify.sh"
  exit 1
fi

REAL_USER="${SUDO_USER:-$USER}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PASS=0
FAIL=0

_pass() { PASS=$(( PASS + 1 )); }
_fail() { FAIL=$(( FAIL + 1 )); }

echo ""
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  HailoRT Verification${NC}"
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"

#-------------------------------------------------------------------------------
# Check 1: dmesg firmware load
#-------------------------------------------------------------------------------
log_step "Check 1: Firmware load (dmesg)"

DMESG_LINE=$(dmesg | grep -i hailo | grep "Probing: Added board" || true)
if [[ -n "$DMESG_LINE" ]]; then
  log_success "Firmware loaded — 'Probing: Added board' found in dmesg"
  echo "    $DMESG_LINE"
  _pass
else
  log_error "'Probing: Added board' not found in dmesg"
  log_info  "Hint: check 'dmesg | grep -i hailo' for details"
  _fail
fi

#-------------------------------------------------------------------------------
# Check 2: hailortcli scan
#-------------------------------------------------------------------------------
log_step "Check 2: hailortcli scan"

if hailortcli scan 2>/dev/null | grep -q "Device:"; then
  log_success "Device found:"
  hailortcli scan 2>/dev/null | grep "Device:" | sed 's/^/    /'
  _pass
else
  log_error "hailortcli scan: no device found"
  _fail
fi

#-------------------------------------------------------------------------------
# Check 3: Python binding
#-------------------------------------------------------------------------------
log_step "Check 3: Python binding"

VENV_PATH="$SCRIPT_DIR/venv_hailoRT"

if [[ ! -d "$VENV_PATH" ]]; then
  log_warning "venv not found at $VENV_PATH — skipping (was .whl installed?)"
else
  HAILO_PKG=$(sudo -u "$REAL_USER" bash -c "source '$VENV_PATH/bin/activate' && pip list 2>/dev/null" | grep -i hailort || true)
  if [[ -n "$HAILO_PKG" ]]; then
    log_success "hailort installed in venv: $HAILO_PKG"
    _pass
  else
    log_error "hailort package not found in venv (pip list)"
    log_info  "Hint: try reinstalling the .whl via Install.sh"
    _fail
  fi
fi

#-------------------------------------------------------------------------------
# Summary
#-------------------------------------------------------------------------------
echo ""
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
if [[ $FAIL -eq 0 ]]; then
  echo -e "${GREEN}${BOLD}  All checks passed ($PASS/$PASS)${NC}"
else
  echo -e "${RED}${BOLD}  $FAIL check(s) failed — $PASS passed${NC}"
fi
echo -e "${BOLD}════════════════════════════════════════════════════════════${NC}"
echo ""

[[ $FAIL -eq 0 ]]
