# Hailo PCIe 드라이버 설치 기록

라즈베리파이 5 (Ubuntu 24.04 noble, ARM64) 환경에서 `hailort-pcie-driver_4.23.0_all.deb` 설치한 기록.

---

## 환경

- 머신: Raspberry Pi 5 (`SexyPi`)
- OS: Ubuntu 24.04 (noble), ARM64
- 커널: `6.8.0-1052-raspi`
- 패키지: `hailort-pcie-driver_4.23.0_all.deb`
- 디바이스: Hailo-8 AI Processor (PCIe)

---

## 단계별 진행

### 1. 첫 시도 — dpkg lock 충돌

```bash
sudo dpkg -i hailort-pcie-driver_4.23.0_all.deb
```

**에러**:
```
dpkg: error: dpkg frontend lock was locked by another process with pid 1057
```

**원인**: 백그라운드에서 `unattended-upgrade`(자동 보안 업데이트)가 실행 중이라 dpkg 잠금을 잡고 있었음.

```bash
ps -p 1057 -o pid,user,cmd
# 1057 root /usr/bin/python3 /usr/bin/unattended-upgrade
```

**해결**: 잠금 파일은 절대 강제로 지우면 안 됨. unattended-upgrade가 끝날 때까지 대기.

```bash
sudo tail -f /var/log/unattended-upgrades/unattended-upgrades-dpkg.log
```

커널/systemd/openssh 등 핵심 패키지가 업데이트되어 있어서, 끝난 후 **재부팅**.

---

### 2. 두 번째 시도 — `build-essential` 의존성 누락

```bash
sudo dpkg -i hailort-pcie-driver_4.23.0_all.deb
```

**에러**:
```
hailort-pcie-driver depends on build-essential; however:
  Package build-essential is not installed.
```

**원인**: 드라이버는 커널 모듈을 빌드해야 해서 컴파일러(`build-essential`)가 필요한데 시스템에 없음.

**잘못된 시도**:
```bash
sudo apt-get install -f -y
```
이게 의존성을 깔아주는 게 아니라 hailort-pcie-driver를 **삭제**해버림. apt가 build-essential을 후보로 잡지 못하고 충돌을 푸는 가장 빠른 방법으로 패키지 제거를 선택한 것.

**올바른 해결**: build-essential을 직접 설치.
```bash
sudo apt-get install -y build-essential
```

---

### 3. 세 번째 문제 — 깨진 의존성 (`bzip2` ↔ `libbz2`)

```bash
sudo apt-get install -y build-essential
```

**에러**:
```
dpkg-dev : Depends: bzip2 but it is not installable
```

더 파보니:
```
bzip2 : Depends: libbz2-1.0 (= 1.0.8-5.1) but 1.0.8-5.1build0.1 is to be installed
```

**원인**: `libbz2-1.0`은 보안 업데이트 버전(`1.0.8-5.1build0.1`)이 깔려있는데, `bzip2` 패키지 후보는 base `noble` 저장소의 구 버전(`1.0.8-5.1`)밖에 없음. 두 버전이 정확히 일치해야 하는데 안 맞아서 충돌.

진단:
```bash
apt-cache madison bzip2
#   bzip2 |  1.0.8-5.1 | http://ports.ubuntu.com/ubuntu-ports noble/main arm64 Packages
```

`noble`만 보이고 `noble-updates`가 없음.

```bash
cat /etc/apt/sources.list.d/ubuntu.sources
# Suites: noble
# Suites: noble-security
```

→ **`noble-updates` 저장소가 비활성화** 되어있어서 보안 업데이트된 라이브러리에 맞는 패키지 본체를 받을 길이 없었음.

**해결**: `noble-updates` 추가.
```bash
sudo sed -i 's/^Suites: noble$/Suites: noble noble-updates/' /etc/apt/sources.list.d/ubuntu.sources
sudo apt-get update
sudo apt-get install -y build-essential
```

---

### 4. 네 번째 문제 — DKMS 빌드 실패 (커널 헤더 없음)

```bash
sudo dpkg -i hailort-pcie-driver_4.23.0_all.deb
# Do you wish to use DKMS? [Y/n]: Y
# Failed. Exited with status 2.
```

로그 확인:
```bash
sudo tail -80 /var/log/hailort-pcie-driver.deb.log
# make[1]: *** /lib/modules/6.8.0-1052-raspi//build: No such file or directory.  Stop.
```

**원인**: 현재 실행 중인 커널의 헤더(`linux-headers-6.8.0-1052-raspi`)가 없어서 모듈 컴파일 불가.

추가로 `dkms` 패키지 자체도 미설치라 폴백(DKMS 없이 빌드) 경로가 작동.

**해결 (실제로 일어난 일)**: 커널 헤더 설치 후 폴백 빌드 경로가 성공.
```bash
sudo apt-get install -y linux-headers-$(uname -r)
sudo dpkg -i hailort-pcie-driver_4.23.0_all.deb
```

빌드 로그:
```
CC [M]  .../pcie.o
CC [M]  .../fops.o
...
LD [M]  .../hailo_pci.ko
INSTALL /lib/modules/6.8.0-1052-raspi/kernel/drivers/misc/hailo_pci.ko
```

→ `hailo_pci.ko` 모듈이 성공적으로 빌드/설치됨.

---

### 5. 최종 검증 — 재부팅 후 확인

```bash
sudo reboot
```

재부팅 후:

```bash
lsmod | grep hailo
# hailo_pci             122880  0

lspci | grep -i hailo
# 0000:01:00.0 Co-processor: Hailo Technologies Ltd. Hailo-8 AI Processor (rev 01)

dpkg -l hailort-pcie-driver
# ii  hailort-pcie-driver 4.23.0       all

sudo dmesg | grep -i hailo
# [    7.125843] hailo: Init module. driver version 4.23.0
# [    7.127104] hailo 0000:01:00.0: Probing on: 1e60:2864...
# [    7.188533] hailo 0000:01:00.0: File hailo/hailo8_fw.bin written successfully
# [    7.206102] hailo 0000:01:00.0: NNC Firmware loaded successfully
# [    7.221050] hailo 0000:01:00.0: Probing: Added board 1e60-2864, /dev/hailo0
```

**결과**: 모듈 자동 로드 ✅ / PCIe 디바이스 인식 ✅ / dpkg 상태 정상(`ii`) ✅ / 펌웨어 로드 성공 → `/dev/hailo0` 생성 ✅.

`module verification failed` 경고는 서드파티 모듈에서 정상 (커널 서명 키 없음 → "tainted" 표시만 될 뿐 동작에는 영향 없음).

---

## 요약 — 시간순 문제/원인/해결

| # | 문제 | 원인 | 해결 |
|---|---|---|---|
| 1 | dpkg lock | unattended-upgrade 실행 중 | 끝날 때까지 대기 후 재부팅 |
| 2 | `build-essential` 의존성 누락 | 컴파일러 미설치 | `apt install build-essential` |
| 3 | `bzip2`/`libbz2` 버전 충돌 | `noble-updates` 저장소 비활성화 | `ubuntu.sources`에 `noble-updates` 추가 |
| 4 | DKMS 빌드 실패 | 커널 헤더 없음 | `linux-headers-$(uname -r)` 설치 |
| 5 | 검증 | — | `lsmod`/`lspci`/`dmesg` 통과 |

---

## 남은 권장 작업

- **DKMS 등록**: 다음 커널 업데이트 때 자동 재빌드되도록.
  ```bash
  sudo apt-get install -y dkms
  sudo dpkg -i ~/hailo/hailort-pcie-driver_4.23.0_all.deb
  ```
- **HailoRT 유저스페이스 설치**: `hailortcli`, Python 바인딩 등 (`hailort_*.deb`, `hailort-*.whl`).
- **검증 명령**: `hailortcli fw-control identify`로 NPU와 통신 확인.
