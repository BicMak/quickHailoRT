# HailoRT 설치 기록

라즈베리파이 5 (Ubuntu 24.04 noble, ARM64) 환경에 HailoRT 풀스택을 깐 기록.

> 참고: PCIe 커널 드라이버 설치 과정은 [INSTALL_LOG.md](INSTALL_LOG.md), 배경 개념은 [BACKGROUND.md](BACKGROUND.md) 참고.

---

## HailoRT 풀스택 = 3개 패키지

공식 문서가 안내하는 설치 단위:

| # | 파일 | 역할 | 포맷 |
|---|---|---|---|
| 1 | `hailort-pcie-driver_4.23.0_all.deb` | 커널 드라이버 + 펌웨어 | `.deb` (dpkg) |
| 2 | `hailort_4.23.0_arm64.deb` | 유저스페이스 라이브러리 + CLI + systemd 서비스 | `.deb` (dpkg) |
| 3 | `hailort-4.23.0-cp312-cp312-linux_aarch64.whl` | Python API (PyHailoRT) | `.whl` (pip) |

세 가지가 서로 다른 층을 담당하고 합쳐졌을 때 NPU를 제대로 쓸 수 있음.

```
사용자 코드 (Python / C++ / GStreamer)
    │
    ├─── PyHailoRT (#3) ──────┐
    │                         ↓
    │               libhailort.so (#2)  ← C 라이브러리 본체
    │                         │
    │                  /dev/hailo0 (ioctl)
    │                         │
    └────────────► hailo_pci.ko (#1)
                              │
                          Hailo-8 NPU
```

---

## 1. PCIe 드라이버 (`hailort-pcie-driver_4.23.0_all.deb`)

### 무엇을 설치하나
- **커널 모듈** `hailo_pci.ko` → `/lib/modules/$(uname -r)/kernel/drivers/misc/`
- **펌웨어 바이너리** `hailo8_fw.bin`, `hailo8_board_cfg.bin`, `hailo8_fw_cfg.bin` → `/lib/firmware/hailo/`
- 부팅 시 PCIe 디바이스 매칭으로 자동 로드되어 `/dev/hailo0` 디바이스 노드 생성

### 설치 명령
```bash
sudo apt-get install -y build-essential linux-headers-$(uname -r) dkms
sudo dpkg -i hailort-pcie-driver_4.23.0_all.deb
```

(설치 중 "Do you wish to use DKMS? [Y/n]"에 `Y` — 다음 커널 업데이트 때 자동 재빌드되도록.)

### 검증
```bash
lsmod | grep hailo
# hailo_pci             122880  0

lspci | grep -i hailo
# 0000:01:00.0 Co-processor: Hailo Technologies Ltd. Hailo-8 AI Processor (rev 01)

dpkg -l hailort-pcie-driver
# ii  hailort-pcie-driver 4.23.0       all

sudo dmesg | grep -i hailo | tail
# hailo: Init module. driver version 4.23.0
# hailo 0000:01:00.0: NNC Firmware loaded successfully
# hailo 0000:01:00.0: Probing: Added board 1e60-2864, /dev/hailo0
```

`/dev/hailo0`이 생성되고 `dmesg`에 `Added board ...` 줄이 나오면 정상.

---

## 2. HailoRT 유저스페이스 (`hailort_4.23.0_arm64.deb`)

### 무엇을 설치하나
- **C 공유 라이브러리** `/usr/lib/libhailort.so.4.23.0` (+ `libhailort.so` 심링크) — NPU 제어/추론 API의 본체. 모든 유저스페이스 길은 결국 여기로 모임.
- **C++ 헤더** `/usr/include/hailo/*.hpp` — `hailort.hpp`, `vdevice.hpp`, `hef.hpp`, `infer_model.hpp` 등 C++로 직접 짤 때 쓰는 헤더.
- **C 헤더** `/usr/include/hailo/hailort.h`, `platform.h`
- **CLI** `/usr/bin/hailortcli` — 디바이스 정보, 모델 실행, 벤치마크 등을 명령줄로.
- **백그라운드 서비스** `/usr/local/bin/hailort_service` + `/lib/systemd/system/hailort.service` — Python API(PyHailoRT)가 디바이스에 접근할 때 쓰는 데몬.
- **GStreamer 메타데이터 헤더** `/usr/include/gstreamer-1.0/gst/hailo/`

### 설치 명령
```bash
sudo dpkg -i hailort_4.23.0_arm64.deb
# Do you wish to activate hailort service? (required for most pyHailoRT use cases) [y/N]: y
```

서비스 활성화에 `y`를 답하면 다음 일이 일어남:
- `systemctl enable --now hailort.service` 효과
- `/etc/systemd/system/multi-user.target.wants/hailort.service` 심링크 생성 (부팅 자동 시작)
- 서비스 즉시 시작 → `hailort_service` 데몬 프로세스 가동

### 검증
```bash
# 1. 서비스 상태
systemctl status hailort.service
# Loaded: loaded (... ; enabled)
# Active: active (running)
# Main PID: 3732 (hailort_service)

systemctl is-enabled hailort.service
# enabled

# 2. CLI로 펌웨어와 통신
hailortcli fw-control identify
# Board Name: Hailo-8
# Device Architecture: HAILO8
# Firmware Version: 4.23.0 ...
# Serial Number: ...

# 3. 라이브러리 위치 확인
ls /usr/lib/libhailort*
# /usr/lib/libhailort.so -> libhailort.so.4.23.0
# /usr/lib/libhailort.so.4.23.0
```

`hailortcli fw-control identify`가 보드 정보를 출력하면 **드라이버 ↔ 펌웨어 ↔ 유저스페이스 라이브러리 통신이 전부 정상**.

---

## 3. PyHailoRT (`hailort-4.23.0-cp312-cp312-linux_aarch64.whl`)

### 무엇을 설치하나
- Python에서 NPU를 다루기 위한 C 확장 모듈 (`hailo_platform` 패키지)
- `libhailort.so`를 Python으로 감싼 얇은 바인딩
- **선택사항** — Python API 안 쓸 거면 깔 필요 없음

### 파일명 해석
- `cp312` → CPython 3.12 전용
- `linux_aarch64` → ARM64 리눅스 전용
- → 시스템 Python이 정확히 3.12여야 함 (`python3 --version`)

### 설치 명령
Ubuntu 24.04는 PEP 668로 시스템 Python에 직접 pip 설치를 막아서 **venv 사용**.

```bash
sudo apt-get install -y python3-venv
python3 -m venv ~/hailo/venv
source ~/hailo/venv/bin/activate
pip install "numpy<2"   # PyHailoRT는 numpy 1.x만 지원
pip install ~/hailo/hailort-4.23.0-cp312-cp312-linux_aarch64.whl
```

### 검증
```bash
# venv 활성화 상태에서
python -c "import hailo_platform; print(hailo_platform.__version__)"
# 4.23.0
```

버전 문자열이 출력되면 OK. 실제 NPU와 통신은 위 #2의 `hailort.service`가 떠있어야 동작.

이후 NPU 사용 시마다:
```bash
source ~/hailo/venv/bin/activate
python <스크립트>
```

---

## 단계별 설치 중 만난 문제 (요약)

자세한 trace는 [INSTALL_LOG.md](INSTALL_LOG.md). 핵심만:

| 단계 | 문제 | 원인 | 해결 |
|---|---|---|---|
| #1 | dpkg lock | unattended-upgrade 실행 중 | 끝날 때까지 대기 |
| #1 | `build-essential` 미설치 | 컴파일러 없음 | `apt install build-essential` |
| #1 | `bzip2`/`libbz2` 버전 충돌 | `noble-updates` 비활성화 | `ubuntu.sources`에 `noble-updates` 추가 |
| #1 | DKMS 빌드 실패 | 커널 헤더 없음 | `linux-headers-$(uname -r)` 설치 |
| #3 | `pip install` 거부 | Ubuntu 24.04 PEP 668 | venv 사용 |

---

## 최종 풀스택 검증 체크리스트

위 3개 패키지가 모두 정상 설치되었는지 한 번에 확인:

```bash
# 1. 드라이버 레벨
lsmod | grep hailo                    # → hailo_pci 로드됨
lspci | grep -i hailo                 # → Hailo-8 인식
ls /dev/hailo0                        # → 디바이스 노드 존재

# 2. 유저스페이스 레벨
dpkg -l hailort hailort-pcie-driver | grep ^ii
systemctl is-active hailort.service   # → active
hailortcli fw-control identify        # → 보드 정보 출력

# 3. Python 레벨
source ~/hailo/venv/bin/activate
python -c "import hailo_platform; print(hailo_platform.__version__)"
```

세 단계 다 통과하면 NPU에서 추론 돌릴 준비 끝.

---

## 다음 단계 옵션

용도별로 갈리는 길:

- **빠른 동작 확인 / 벤치마크**: `hailortcli run <모델.hef>` — 코드 0줄
- **Python으로 추론 코드 작성**: PyHailoRT (#3) 사용
- **C++로 임베디드/저레이턴시**: `/usr/include/hailo/`의 헤더와 `libhailort.so`로 직접 빌드 (`g++ ... -lhailort`)
- **카메라/비디오 파이프라인**: GStreamer + Hailo Tappas 별도 설치 (`libgsthailo`, `libgsthailotools` 플러그인)
- **Pi 5 데모**: `hailo-rpi5-examples` 레포 (Pi OS 가정이라 Ubuntu에선 일부 손봐야 함)