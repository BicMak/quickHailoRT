이 스크립트는 **Hailo NPU 드라이버/런타임 패키지를 자동으로 찾고 검증**하는 거야. 초보자 관점에서 단계별로 설명할게.

---

## 1. 아키텍처 감지 및 매핑

```bash
HOST_ARCH=$(uname -m)
case "$HOST_ARCH" in
  x86_64)   DEB_ARCH="amd64";  WHL_ARCH="x86_64";  ARCH_DIR="x86" ;;
  aarch64)  DEB_ARCH="arm64";  WHL_ARCH="aarch64"; ARCH_DIR="arm" ;;
  ...
esac
```

- `uname -m`: 현재 CPU 아키텍처 반환 (x86_64, aarch64 등)
- `case` 문으로 아키텍처별로 **파일명 토큰** 매핑:
  - `DEB_ARCH`: .deb 패키지 파일명에 쓰이는 아키텍처 표기 (amd64, arm64)
  - `WHL_ARCH`: Python wheel 파일명에 쓰이는 표기 (x86_64, aarch64)
  - `ARCH_DIR`: 파일이 저장된 디렉토리명 (x86, arm)

**예:** Raspberry Pi 5는 `aarch64` → `.deb` 파일은 `*_arm64.deb`, wheel은 `*_aarch64.whl` 형식

---

## 2. 설치 파일 디렉토리 찾기

```bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_BASE="$SCRIPT_DIR/install_file"
INSTALL_DIR="$INSTALL_BASE/$ARCH_DIR"
```

- `${BASH_SOURCE[0]}`: 현재 실행 중인 스크립트 파일 경로
- `dirname`: 파일이 있는 디렉토리 추출
- `cd ... && pwd`: 절대 경로로 변환

**결과:** 스크립트 위치 기준으로 `install_file/x86` 또는 `install_file/arm` 디렉토리 설정

---

## 3. 연관 배열로 찾을 파일 정의

```bash
declare -A EXPECTED_PATTERN
declare -A EXPECTED_DESC
EXPECTED_PATTERN[driver]="hailort-pcie-driver_*_all.deb"
EXPECTED_DESC[driver]="HailoRT PCIe driver (.deb, arch-independent / DKMS)"
```

- `declare -A`: **연관 배열** 선언 (key-value 쌍)
- `EXPECTED_PATTERN[driver]`: "driver"라는 키에 파일명 패턴 저장
- `EXPECTED_DESC[driver]`: 설명 저장

**3종류 파일 정의:**
- `driver`: PCIe 드라이버 (모든 아키텍처 공통)
- `runtime`: 런타임 라이브러리 (아키텍처별)
- `wheel`: Python 바인딩 (아키텍처별)

---

## 4. 파일 검색 루프

```bash
for key in "${!EXPECTED_PATTERN[@]}"; do
  pattern="${EXPECTED_PATTERN[$key]}"
  hit=$(find "$INSTALL_DIR" -maxdepth 1 -name "$pattern" -type f 2>/dev/null | head -1)
  if [[ -n "$hit" ]]; then
    FOUND_PATHS[$key]="$hit"
  fi
done
```

- `"${!EXPECTED_PATTERN[@]}"`: 배열의 **모든 키** 순회 (driver, runtime, wheel)
- `find -maxdepth 1`: 현재 디렉토리만 검색 (하위 폴더 안 봄)
- `-name "$pattern"`: 패턴 매칭
- `head -1`: 여러 개 있어도 첫 번째만
- `FOUND_PATHS[$key]`: 찾은 파일 경로 저장

---

## 5. 잘못된 아키텍처 파일 경고

```bash
case "$ARCH_DIR" in
  arm) wrong=$(find "$INSTALL_DIR" -maxdepth 1 \( -name '*_amd64.deb' -o -name '*linux_x86_64.whl' \) 2>/dev/null) ;;
  x86) wrong=$(find "$INSTALL_DIR" -maxdepth 1 \( -name '*_arm64.deb' -o -name '*_armel.deb' -o -name '*linux_aarch64.whl' \) 2>/dev/null) ;;
esac
```

- `\( ... -o ... \)`: 여러 패턴 OR 조건 (`-o`는 or)
- ARM 디렉토리에 x86 파일 있거나, 반대 경우 경고

---

## 6. 버전 일치 검증

```bash
deb_ver=$(basename "${FOUND_PATHS[runtime]}" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
whl_ver=$(basename "${FOUND_PATHS[wheel]}"   | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
```

- `basename`: 경로에서 파일명만 추출
- `grep -oE '[0-9]+\.[0-9]+\.[0-9]+'`: 정규표현식으로 버전 번호 추출 (예: 4.19.0)
  - `-o`: 매칭된 부분만 출력
  - `-E`: 확장 정규표현식
- `.deb`와 `.whl` 버전이 다르면 에러 (`4.19.0` vs `4.18.0` 같은 경우)

---

## 7. Python 버전 호환성 체크

```bash
py_tag="cp${PY_VER//./}"  # PY_VER=311 → cp311
if [[ "$whl_name" == *"$py_tag"* ]]; then
```

- `${PY_VER//./}`: 문자열 치환 - 모든 `.` 제거 (3.11 → 311)
- `cp311`: CPython 3.11 태그
- wheel 파일명에 `cp311` 포함되어야 해당 Python 버전 호환

---

**전체 흐름 요약:**

1. 현재 CPU 아키텍처 감지
2. 아키텍처에 맞는 디렉토리에서 3가지 파일(driver, runtime, wheel) 검색
3. 파일 못 찾으면 에러
4. 잘못된 아키텍처 파일 있으면 경고
5. `.deb`와 `.whl` 버전 일치 확인
6. Python 버전과 wheel 호환성 확인

방어적 프로그래밍으로 사용자가 파일을 잘못 놓거나 버전 불일치 상황을 미리 잡아내는 구조야.