# 세션 2 — HailoRT 유저스페이스 + Tappas + hailo-apps 설치 시도

PCIe 드라이버 설치 이후, NPU를 실제로 사용 가능한 상태로 만들기 위한 작업 기록.

> 이전: [INSTALL_LOG.md](INSTALL_LOG.md) — PCIe 드라이버 설치
> 배경: [BACKGROUND.md](BACKGROUND.md), [HAILORT_INSTALL.md](HAILORT_INSTALL.md)

---

## 시작 상태

- ✅ `hailort-pcie-driver_4.23.0` 설치 완료, `/dev/hailo0` 동작
- ✅ `hailort_4.23.0_arm64.deb` 설치, `hailortcli`, systemd 서비스 정상
- ✅ PyHailoRT (`hailort-4.23.0-cp312-cp312-linux_aarch64.whl`)를 `~/hailo/venv`에 설치
- 🎯 목표: GStreamer 파이프라인 데모(`detection_simple.py` 등)까지 돌릴 수 있는 환경 구축

---

## 1. Tappas 풀버전 소스 빌드

### 무엇을 했나
GitHub에서 `~/hailo/tappas/` 클론 후 `./install.sh` 실행.

### 만난 문제 1 — 시스템 의존성 누락

```
SYSTEM REQUIREMENTS REPORT
ffmpeg X | x11-utils X | python-gi-dev X | pkg-config X | libcairo2-dev X |
libgirepository1.0-dev X | libgstreamer1.0-dev X | cmake X | ... | opencv4 X
```

**원인**: Tappas 빌드에 필요한 시스템 패키지들이 없음.

**해결**:
```bash
sudo apt-get install -y \
  ffmpeg x11-utils python-gi-dev pkg-config \
  libcairo2-dev libgirepository1.0-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x \
  cmake libzmq3-dev libopencv-dev
```

**주의**: Ubuntu 24.04에서도 패키지 이름이 `python-gi-dev` (앞에 `python3-` 안 붙음). 처음에 `python3-gi-dev`로 시도해서 "Unable to locate package" 에러.

### 만난 문제 2 — `tappas/install.sh`가 amd64용 hailort .deb 찾음

```
dpkg: error: cannot access archive '/home/jiwon/hailo/tappas/hailort/hailort_*_amd64.deb'
```

**원인**: `tappas/install.sh` 143번째 줄이 `tappas/hailort/` 디렉토리에서 hailort .deb를 dpkg로 다시 깔려고 함. 우린 이미 hailort 깔아놔서 그 디렉토리가 비어있음.

**해결**: `--skip-hailort` 플래그 발견 (스크립트 옵션).
```bash
cd ~/hailo/tappas
./install.sh --skip-hailort
```

(중간에 install.sh 직접 편집해서 syntax error 냈었음 → `git checkout install.sh`로 복원 후 위 옵션 사용)

### 빌드 결과 — 무엇이 만들어졌나

```bash
ls /usr/lib/gstreamer-1.0/ | grep hailo
# libgsthailo.so
# libgsthailopython.so
# libgsthailotools.so (+ .so.5, .so.5.3.0)
# libgsthailotracers.so (+ .so.5, .so.5.3.0)

find ~/hailo/tappas -name "hailo*.so" -path "*python*"
# /home/jiwon/hailo/tappas/core/hailo/build.release/plugins/hailo.cpython-312-aarch64-linux-gnu.so
```

✅ GStreamer 플러그인은 시스템에 정상 설치
⚠️ Python 바인딩(`hailo.cpython-312-...so`)은 **빌드는 됐으나 site-packages로 복사 안 됨** → `import hailo` 실패

### 빌드 중 경고들 (전부 비크리티컬)
- `xtensor`의 `'shape' may be used uninitialized` — GCC 13 false positive
- `meson [options]` deprecated — 새 meson은 `meson setup` 권장
- ZMQ `setsockopt` deprecated — 4.7.0+ 이슈, 동작 OK
- Python `PySys_SetArgv` deprecated — 3.11+ 이슈, 3.12에서 동작 OK
- 빌드 자체는 끝까지 성공 (135/135)

---

## 2. hailo-rpi5-examples / hailo-apps 시도

### 시도 1: `hailo-rpi5-examples` 레포

**문제**: Pi OS(Bookworm, Python 3.11) 가정으로 만들어져서 Ubuntu 24.04(Python 3.12)와 일부 안 맞음. 카메라 스택(`picamera2`), 패키지 이름 차이 등.

→ 깊이 파지 않고 더 OS-중립적인 `hailo-apps` 인프라로 전환.

### 시도 2: `hailo-apps` 인프라

```bash
cd ~/hailo/hailo-apps
sudo ./install.sh
```

**Step 2 (Prerequisites Check)에서 차단**:
```
❌   • TAPPAS Core (.deb)
❌   • HailoRT Python binding (.whl)
❌   • TAPPAS Core Python binding (.whl)
```

**원인 분석**: install.sh가 `scripts/check_installed_packages.sh`를 통해 검사하는데:
- `dpkg -l` 로 deb 패키지 확인
- 시스템 `/usr/bin/python3`에서 `import` 시도
- 우리가 깐 PyHailoRT는 **`~/hailo/venv`에만** 있어서 시스템 Python에서 못 봄
- Tappas 풀버전은 dpkg DB에 등록 안 됨 (소스 빌드라서)

---

## 3. 핵심 깨달음 — git 빌드 vs `.deb` 설치

| 항목 | git clone + 빌드 | `.deb` 설치 |
|---|---|---|
| 받는 것 | 소스 코드 | 이미 빌드된 결과물 |
| 빌드 필요 | ✅ 컴파일러로 직접 | ❌ 미리 빌드돼있음 |
| dpkg DB 등록 | ❌ | ✅ |
| `dpkg -l`로 보임 | ❌ | ✅ |
| 자동 의존성 해결 | ❌ | ✅ |
| 실제 동작 | 같음 | 같음 |

→ **결과 파일은 같지만, dpkg에 등록되느냐가 다름**. 우리 검사 스크립트는 dpkg를 보기 때문에 소스 빌드는 못 인정받음.

GStreamer 자체는 dpkg 안 보고 `/usr/lib/gstreamer-1.0/`만 스캔하므로 **`gst-launch-1.0`로 파이프라인 직접 돌리는 건 동작**.

---

## 4. PyHailoRT 시스템 Python에 설치

### 진단
```bash
/usr/bin/python3 -c "import hailo_platform"
# ModuleNotFoundError: No module named 'hailo_platform'
```

→ 시스템 Python에 PyHailoRT 미설치. hailo-apps 검사 실패의 원인.

### 해결 — Ubuntu 24.04 PEP 668 우회
```bash
sudo pip3 install --break-system-packages --force-reinstall \
    ~/hailo/hailort-4.23.0-cp312-cp312-linux_aarch64.whl
```

`--break-system-packages` 플래그로 PEP 668(externally-managed-environment) 보호 무시. 시스템 dist-packages에 직접 설치.

### 검증
```bash
/usr/bin/python3 -c "import hailo_platform; print(hailo_platform.__version__)"
# 4.23.0  ← 통과
```

→ hailo-apps의 PyHailoRT 검사 항목 통과 가능 상태가 됨.

---

## 5. 모듈 이름 정리 — 헷갈리는 3종

| import 이름 | 패키지 출처 | 역할 | 현재 상태 |
|---|---|---|---|
| `hailort` | (실제 이 이름 없음) | — | — |
| `hailo_platform` | PyHailoRT (`hailort-*.whl`) | NPU 직접 제어 / 추론 | ✅ 시스템 Python에 설치됨 |
| `hailo` | Tappas Core Python 바인딩 | GStreamer 추론 메타데이터 다루기 | ❌ `import` 실패 |

`detection_simple.py` 같은 파이프라인 데모는 `import hailo`를 씀 → Tappas Python 바인딩 필요.

---

## 6. Tappas Python 바인딩 위치 발견

빌드 산출물 탐색:
```bash
find ~/hailo/tappas -name "hailo*.so" 2>/dev/null
# /home/jiwon/hailo/tappas/core/hailo/build.release/plugins/hailo.cpython-312-aarch64-linux-gnu.so
```

→ **이미 빌드되어 있음!** 단지 `site-packages`로 복사가 안 됐을 뿐.

### 다음에 할 것 (옵션 A — 가장 빠른 길)
```bash
sudo cp /home/jiwon/hailo/tappas/core/hailo/build.release/plugins/hailo.cpython-312-aarch64-linux-gnu.so \
        /usr/local/lib/python3.12/dist-packages/

/usr/bin/python3 -c "import hailo; print(hailo.__file__)"
```

성공하면 hailo-apps의 Tappas Python 검사 통과 가능.
단, `tappas-core` **deb 패키지** 검사는 여전히 안 통할 수 있음 (dpkg DB에 없으므로) → 그 경우 `--no-tappas-required` 우회.

---

## 현재 상태 정리

### 동작하는 것
- ✅ `/dev/hailo0` 디바이스 노드, 펌웨어 로드 정상
- ✅ `hailortcli fw-control identify` (NPU와 통신)
- ✅ `hailort.service` (백그라운드 서비스)
- ✅ `~/hailo/venv`에서 `import hailo_platform`
- ✅ `/usr/bin/python3`에서 `import hailo_platform` (방금 시스템에 설치)
- ✅ `/usr/lib/gstreamer-1.0/libgsthailo*.so` (GStreamer 플러그인)
- ✅ 가능: `gst-launch-1.0 ... ! hailonet ... ` 직접 파이프라인 실행

### 아직 안 되는 것
- ❌ `import hailo` (Tappas Python 바인딩 — .so 위치만 옮기면 가능)
- ❌ `dpkg -l hailort-tappas-core` (소스 빌드라 DB 등록 안 됨)
- ❌ `hailo-apps`의 `./install.sh` 끝까지 통과 (위 두 개 때문)
- ❌ `detection_simple.py` 같은 hailo-apps 파이프라인 데모

### 세션 후반 추가 진행

#### Tappas Python 바인딩 wheel 설치
파일: `~/hailo/hailo-apps/hailo_tappas_core_python_binding-5.3.0-py3-none-any.whl`

```bash
sudo pip3 install --break-system-packages \
    ~/hailo/hailo-apps/hailo_tappas_core_python_binding-5.3.0-py3-none-any.whl
```

검증:
```bash
/usr/bin/python3 -c "import hailo; print(hailo.__file__)"
# /usr/local/lib/python3.12/dist-packages/hailo.cpython-312-aarch64-linux-gnu.so
```

→ `import hailo` 통과. hailo-apps의 두 번째 Python 검사는 통과 가능 상태.

#### 시스템 의존성 점검 스크립트 작성
`~/hailo/requireValid.sh` — 한 번에 모든 의존성 점검.

검증 결과 (전부 통과 또는 비크리티컬):
- ✅ Ubuntu 24.04, Python 3.12, 7.8GB RAM
- ✅ build-essential, dkms, libelf-dev, cmake 3.28
- ✅ 핵심 GStreamer/Tappas apt 의존성 (ffmpeg, x11-utils, libgstreamer*-dev, gstreamer1.0-plugins-{base,good,bad,libav,tools,x,gl,ugly,alsa,gtk3,qt5,pulseaudio} 등)
- ✅ libcairo2-dev, libgirepository1.0-dev, python-gi-dev
- ✅ libopencv-dev (pkg-config opencv4 → 4.6.0)
- ✅ `hailo_platform` 4.23.0 (시스템 Python)
- ✅ `hailo` (Tappas Python — 시스템 Python)
- ⚠️ 비크리티컬 누락: `gstreamer1.0-doc` (Ubuntu 24.04에서 패키지 자체 없음), `gstreamer1.0-vaapi` (Pi에선 무관)

### 헤드리스 환경 주의

현 환경은 **모니터 미연결 SSH 접속**. GTK 윈도우를 띄우는 데모는 그대로 못 돌림.

| 옵션 | 비고 |
|---|---|
| SSH X11 forwarding (`ssh -X`) | 가장 간단, 비디오는 좀 느림 |
| VNC | 가상 데스크톱, Pi에 VNC 서버 설치 필요 |
| 파일/스트림 sink로 변경 | GUI 없이 결과를 파일/RTSP로 저장 |
| 모니터 직결 | 가장 안정적 |

`hailortcli benchmark`, standalone 추론 등 GUI 없는 데모는 헤드리스에서 그대로 동작.

---

## 7. 발견 — Hailo AI SW Suite 2025-10

`~/hailo/hailo8_ai_sw_suite_2025-10.run` (Makeself 자기 압축 해제 형식) 발견.

### 릴리즈 노트 (우리 환경과 매칭)

| 항목 | SW Suite 2025-10 | 우리 환경 | 매칭 |
|---|---|---|---|
| 대상 칩 | Hailo-8 only | Hailo-8 | ✅ |
| HailoRT | 4.23.0 | 4.23.0 | ✅ |
| Tappas | HailoRT 4.23.0 호환 | (필요) | ✅ |
| Python | 3.12 / 3.13 | 3.12 | ✅ |
| OS | Ubuntu 24.04 | Ubuntu 24.04 | ✅ |

→ 정공법 진행 가능. 버전 충돌 위험 없음 (같은 4.23.0).

### Makeself 옵션 (확인 완료)

```
--info       임베디드 정보 출력
--list       내부 파일 목록
--check      무결성 검증
--noexec     실행 없이 압축만 해제
--target dir 추출 디렉토리 지정
```

### 우리한테 필요한 건 사실 한두 개

이미 깔린 것: HailoRT driver + runtime + PyHailoRT, Tappas Python wheel
**여전히 못 깐 것**: `hailort_tappas_core_*_arm64.deb` (dpkg DB 등록용)
→ SW Suite에서 이 deb 하나만 추출해서 깔면 hailo-apps 정공법 돌파 가능.

---

## 다음 세션 시작 지점

### 1. SW Suite 안 들여다보기 (실행 안 함, 안전)

```bash
~/hailo/hailo8_ai_sw_suite_2025-10.run --info
~/hailo/hailo8_ai_sw_suite_2025-10.run --list | head -50

mkdir -p ~/hailo/sw_suite_extract
~/hailo/hailo8_ai_sw_suite_2025-10.run --noexec --target ~/hailo/sw_suite_extract
find ~/hailo/sw_suite_extract -name "*.deb" -o -name "*.whl"
```

### 2. `tappas-core` deb만 핀셋 설치

```bash
sudo dpkg -i ~/hailo/sw_suite_extract/.../hailort_tappas_core_*_arm64.deb
sudo apt-get install -f -y    # 의존성 보강 필요 시

# 검증
dpkg -l hailort-tappas-core | grep ^ii
```

### 3. hailo-apps install.sh 정공법

```bash
cd ~/hailo/hailo-apps
sudo ./install.sh \
  --pyhailort ~/hailo/hailort-4.23.0-cp312-cp312-linux_aarch64.whl \
  --pytappas ~/hailo/hailo-apps/hailo_tappas_core_python_binding-5.3.0-py3-none-any.whl
```

prerequisites 5개 모두 통과 → Step 3~7 진행.

### 4. 데모 실행 (헤드리스 고려)

```bash
cd ~/hailo/hailo-apps
source setup_env.sh

# GUI 없는 데모부터 (헤드리스 OK)
ls hailo_apps/python/standalone_apps/ 2>/dev/null
hailortcli benchmark <model.hef>

# GUI 데모는 SSH X11 forwarding 또는 모니터 직결 후
# python hailo_apps/python/pipeline_apps/detection_simple/detection_simple.py
```

---

## 환경 구조 요약 (현재)

```
시스템 레벨
├── /dev/hailo0                                       ← 디바이스 노드
├── /lib/modules/.../hailo_pci.ko                     ← 커널 드라이버
├── /usr/lib/libhailort.so.4.23.0                     ← C 라이브러리
├── /usr/bin/hailortcli                               ← CLI
├── /usr/local/bin/hailort_service                    ← systemd 서비스
└── /usr/lib/gstreamer-1.0/libgsthailo*.so            ← GStreamer 플러그인 (Tappas 빌드 산출물)

시스템 Python (/usr/bin/python3)
└── /usr/local/lib/python3.12/dist-packages/
    └── hailo_platform/                               ← PyHailoRT (방금 설치)
    └── (hailo.so는 아직 없음 — 다음 세션에서 복사 예정)

사용자 venv (~/hailo/venv)
└── lib/python3.12/site-packages/
    └── hailo_platform/                               ← PyHailoRT

hailo-apps venv (예정 — 아직 미생성)
└── ~/hailo/hailo-apps/venv_hailo_apps/
    └── (install.sh 통과 시 생성됨)

Tappas 소스 트리
└── ~/hailo/tappas/
    ├── core/hailo/build.release/plugins/
    │   └── hailo.cpython-312-aarch64-linux-gnu.so    ← 빌드됨, 미배포
    └── (각종 소스, GStreamer 플러그인 빌드 후 시스템에 복사됨)
```

---

## 기록할 만한 헷갈렸던 포인트

1. **`import hailort` ≠ `import hailo_platform` ≠ `import hailo`** — 세 개 다 다른 모듈
2. **소스 빌드된 .so 파일이 있어도 site-packages 안에 있어야 import 가능** — 빌드와 배포는 별개
3. **dpkg DB에 등록되지 않은 설치는 검사 도구에 안 잡힘** — 파일은 있어도 "없는 것" 처리됨
4. **PyHailoRT는 venv에 깔아도 시스템 검사엔 안 잡힘** — 그래서 시스템 Python에 별도 설치 필요했음
5. **`--break-system-packages`** — Ubuntu 24.04 PEP 668 우회, 시스템 Python에 직접 pip 설치 가능