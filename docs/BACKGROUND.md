# 배경지식 — Hailo 드라이버 설치를 이해하기 위한 기초

위 설치 과정에서 등장한 개념들을 이해하기 위한 배경지식 정리.

---

## 1. `.deb` 패키지와 dpkg/apt

### `.deb` 파일이란
Debian 계열(Ubuntu 포함) 리눅스의 **패키지 포맷**. 압축된 아카이브 안에 다음이 들어있음:
- 실제 설치될 파일들 (바이너리, 라이브러리, 설정 등)
- 메타데이터 (이름, 버전, 의존성 목록)
- 설치/제거 시 실행될 스크립트 (preinst, postinst, prerm, postrm)

파일명 규칙: `<패키지명>_<버전>_<아키텍처>.deb`
- `hailort-pcie-driver_4.23.0_all.deb` → 패키지 `hailort-pcie-driver`, 버전 `4.23.0`, 아키텍처 `all`(모든 아키 공용 — 보통 소스 포함)

### dpkg vs apt
- **`dpkg`**: 저수준 도구. `.deb` 파일 하나를 설치/제거. **의존성을 자동으로 해결하지 않음** — 의존성이 없으면 그냥 에러를 뱉음.
- **`apt` / `apt-get`**: 고수준 도구. 저장소(repository)에서 패키지를 받아오고 의존성을 자동 해결. 내부적으로 dpkg를 호출.

→ 이번 사례에서 `dpkg -i`로 설치했더니 `build-essential` 의존성이 없어서 에러. `apt-get install -f`(fix-broken)로 해결 시도했지만, apt가 `build-essential`을 찾을 수 없는 상황에서는 충돌을 푸는 가장 빠른 길로 **패키지를 제거**해버림.

### dpkg 상태 코드
`dpkg -l <pkg>` 출력의 첫 두 글자:
- `ii`: 정상 설치됨 (Install + installed)
- `iU`: 설치 의도 / Unpacked (압축만 풀고 설정 안 됨)
- `iF`: half-conFigured (postinst 스크립트 실패)
- `un`: Unknown / not installed (한 번도 설치된 적 없음)

### dpkg lock
한 번에 한 프로세스만 패키지 DB를 수정할 수 있게 하는 잠금:
- `/var/lib/dpkg/lock-frontend`
- `/var/lib/dpkg/lock`

다른 프로세스(예: `unattended-upgrade`, GUI 패키지 매니저, `apt update`)가 잡고 있으면 새 dpkg 호출은 실패. **잠금 파일을 강제로 삭제하면 패키지 DB가 깨질 수 있음** — 절대 금지.

---

## 2. APT 저장소 구조

Ubuntu의 패키지는 여러 "suite"로 나뉨:

| Suite | 용도 |
|---|---|
| `noble` | 24.04 출시 시점의 패키지 (변하지 않음) |
| `noble-updates` | 출시 후 일반 버그 픽스/업데이트 |
| `noble-security` | 보안 패치 (CVE 대응) |
| `noble-backports` | 더 최신 버전 백포트 (선택) |

각 suite는 다시 component로 나뉨:
- `main`: 캐노니컬이 공식 지원
- `restricted`: 자유 라이선스 아니지만 필요한 것 (NVIDIA 드라이버 등)
- `universe`: 커뮤니티 유지
- `multiverse`: 자유롭지 않은 것 (코덱 등)

### sources 파일 형식
- 구식: `/etc/apt/sources.list` + `/etc/apt/sources.list.d/*.list` (한 줄 형식)
- 신식 (24.04 기본): `/etc/apt/sources.list.d/*.sources` (deb822 다중 줄 형식)

**이번 사례의 핵심**: `noble-updates`가 활성화되지 않으면 보안 업데이트로 올라간 라이브러리(`libbz2`)와 본체 패키지(`bzip2`)의 버전이 어긋남. Debian/Ubuntu에서 같이 빌드된 패키지들은 보통 정확한 버전 매칭(`= 1.0.8-5.1build0.1`)을 요구하기 때문에, 한쪽만 업데이트되면 의존성 지옥이 됨.

---

## 3. `unattended-upgrade`

자동 보안 업데이트를 백그라운드에서 적용해주는 데몬. 라즈베리파이/Ubuntu 서버 기본 활성화.

- 설정: `/etc/apt/apt.conf.d/50unattended-upgrades`, `/etc/apt/apt.conf.d/20auto-upgrades`
- 로그: `/var/log/unattended-upgrades/`
- 보통 새벽이나 부팅 직후에 실행

→ 사용자가 직접 dpkg/apt를 돌리려 할 때 충돌하는 흔한 원인.

---

## 4. 리눅스 커널 모듈 & DKMS

### 커널 모듈
리눅스 커널 자체는 모놀리식이지만, **모듈**로 기능을 동적 로드/언로드 가능:
- 모듈 파일: `.ko` (kernel object)
- 위치: `/lib/modules/<커널버전>/kernel/`
- 로드: `modprobe <모듈명>` 또는 `insmod`
- 조회: `lsmod`

부팅 시 자동 로드되는 모듈 목록은 `/etc/modules-load.d/` 또는 udev/PCI 매칭으로 결정. PCIe 디바이스 드라이버는 보통 PCI ID 매칭으로 자동 로드.

### 왜 모듈을 컴파일해야 하나?
커널 모듈은 **현재 실행 중인 커널의 정확한 버전과 빌드 환경에 맞춰야** 함. 소스 코드만 있는 드라이버(out-of-tree)는 사용자 머신에서 빌드해야 함.

빌드에 필요한 것:
- 컴파일러 (`gcc`, `make`) → `build-essential`
- 커널 헤더 → `linux-headers-$(uname -r)` → `/lib/modules/$(uname -r)/build` 심볼릭 링크
- (선택) `dkms`

### DKMS (Dynamic Kernel Module Support)
**문제**: 커널이 업데이트되면 out-of-tree 모듈은 새 커널에 다시 빌드해야 함. 손으로 매번 하는 건 귀찮음.

**해결**: DKMS는 모듈 소스를 `/usr/src/<모듈>-<버전>/`에 등록해두고, 커널 업데이트 훅에서 자동으로 재빌드 + 설치.

- 등록: `dkms add`, `dkms build`, `dkms install`
- 상태 확인: `dkms status`

이번 hailort-pcie-driver는 DKMS 사용을 권장하지만, `dkms` 패키지가 없으면 폴백으로 한 번만 빌드해서 설치(이 경우 다음 커널 업데이트 때 수동 재빌드 필요).

### 모듈 서명 / "tainted kernel"
보안을 위해 커널은 서명된 모듈만 신뢰. 우리가 빌드한 out-of-tree 모듈은 서명 키가 없어서:
```
hailo_pci: module verification failed: signature and/or required key missing - tainting kernel
```
"tainted"는 단순히 "비공식 모듈이 로드됐다"는 표시일 뿐, 동작에는 영향 없음. SecureBoot가 켜진 시스템에서는 차단될 수 있지만, 라즈베리파이는 일반적으로 SecureBoot 미사용.

---

## 5. PCIe와 Hailo-8

### PCIe 디바이스 인식 흐름
1. 부팅 시 BIOS/UEFI/펌웨어가 PCIe 버스 스캔 → 디바이스 enumeration
2. 커널이 vendor:device ID로 매칭되는 드라이버 찾기 (`/lib/modules/<ver>/modules.alias`)
3. 매칭되는 드라이버가 모듈이면 자동 `modprobe`
4. 드라이버의 `probe()` 함수 호출 → 디바이스 초기화, 펌웨어 업로드, 디바이스 노드 생성

`lspci`로 인식 여부 확인. `dmesg`에서 probe 로그 확인.

### Hailo-8 probe 시퀀스 (이번 dmesg에서 본 것)
```
Probing on: 1e60:2864          ← Vendor 1e60 (Hailo), Device 2864 (Hailo-8)
Allocate memory for device extension
enabling device              ← PCIe BAR enable
mapped bar 0/2/4              ← MMIO 영역 매핑
Enabled 64 bit dma            ← DMA 설정
Disabling ASPM L0s            ← 전력 절전 모드 끄기 (안정성)
Writing file hailo/hailo8_fw.bin   ← 펌웨어를 디바이스에 업로드
NNC Firmware loaded successfully
FW loaded, took 78 ms
Added board 1e60-2864, /dev/hailo0   ← 유저스페이스 노드 생성
```

`/dev/hailo0`이 생기면 유저스페이스 라이브러리(HailoRT)가 이 디바이스 파일을 통해 추론 작업을 보낼 수 있음.

### 라즈베리파이 5의 PCIe
Pi 5는 외부 PCIe x1 슬롯 1개 제공. 기본은 Gen2지만 `/boot/firmware/config.txt`에서 Gen3 활성화 가능:
```
dtparam=pciex1
dtparam=pciex1_gen=3
```
Hailo AI HAT/AI Kit 등을 이 슬롯에 연결.

---

## 6. Hailo 스택 구조

```
[ 사용자 앱 (Python/C++) ]
         ↓
[ HailoRT 라이브러리 (libhailort.so, hailortcli) ]   ← 유저스페이스
         ↓ ioctl
[ /dev/hailo0 (디바이스 노드) ]
         ↓
[ hailo_pci.ko 커널 모듈 ]                            ← 이번에 설치한 것
         ↓ PCIe
[ Hailo-8 NPU 하드웨어 + 펌웨어 ]
```

이번에 설치한 `hailort-pcie-driver`는 가장 아래의 **커널 모듈 + 펌웨어 바이너리**만. 실제로 NPU를 사용하려면 위 단계의 **HailoRT 유저스페이스 라이브러리**도 별도로 설치해야 함 (`hailort_<version>_arm64.deb`).

---

## 7. 자주 만나는 명령 요약

| 명령 | 용도 |
|---|---|
| `dpkg -i <file.deb>` | .deb 직접 설치 |
| `dpkg -l <pkg>` | 패키지 상태 확인 |
| `dpkg --configure -a` | 미완성 설정 마무리 |
| `apt-get install -f` | 깨진 의존성 복구 시도 (주의) |
| `apt-cache madison <pkg>` | 사용 가능한 모든 버전과 출처 |
| `apt-cache policy <pkg>` | 우선순위/설치 후보 확인 |
| `lsmod \| grep <name>` | 로드된 모듈 확인 |
| `modprobe <name>` / `rmmod <name>` | 모듈 로드/언로드 |
| `lspci -nn` | PCIe 디바이스 + ID 표시 |
| `dmesg` / `journalctl -k` | 커널 로그 |
| `uname -r` | 현재 커널 버전 |
| `dkms status` | DKMS 등록 모듈 상태 |