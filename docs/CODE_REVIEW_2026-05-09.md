# 코드 리뷰 — 2026-05-09

로깅 시스템 도입 + 온도 측정 추가 작업에 대한 리뷰.

## 리뷰 대상

- `src/common/logging.hpp` / `logging.cpp`
- `src/common/hailo_infer.hpp` / `hailo_infer.cpp`
- `src/common/toolbox.cpp`
- `src/classification/classifier.cpp`
- `src/classification/CMakeLists.txt`
- `src/classification/build.sh`

---

## Critical — 실제 버그 가능성

### C1. `logging.cpp:41` — `localtime()` 스레드 안전 X

```cpp
struct tm *tm_info = localtime(&ts.tv_sec);
```

`localtime()`은 정적 버퍼 포인터를 반환해서 멀티스레드에 안전하지 않다. `infer()`의 read 스레드 여러 개가 동시에 LOG를 찍으면 타임스탬프가 깨질 수 있다.

**수정**

```cpp
struct tm tm_info;
localtime_r(&ts.tv_sec, &tm_info);
strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_info);
```

`set_log_file()` 안의 `localtime()`도 같이 교체.

### C2. `logging.cpp` — 멀티스레드 동시 쓰기 시 로그 줄 섞임

`fprintf` 호출이 여러 번 나뉘어 있어서 다른 스레드 출력이 한 줄 중간에 끼어들 수 있다.

**수정**: `_log()` 시작에 `std::lock_guard<std::mutex>` 추가.

```cpp
static std::mutex g_log_mutex;
std::lock_guard<std::mutex> lock(g_log_mutex);
```

### C3. `logging.cpp:60-76` — CSV 메시지에 `\n`, `\r` 처리 안 됨

지금은 `"`만 이스케이프. 메시지에 줄바꿈이 들어가면 CSV가 깨진다. 콤마는 `"`로 감싸서 OK지만 개행은 별도 처리 필요.

**수정**

```cpp
for (char *p = msgbuf; *p; ++p) {
    if (*p == '"') fputc('"', g_csv_file);
    if (*p == '\n' || *p == '\r') { fputc(' ', g_csv_file); continue; }
    fputc(*p, g_csv_file);
}
```

### C4. `hailo_infer.cpp:160` — 매 추론마다 firmware control 호출

```cpp
auto temp = this->device->get().get_chip_temperature();
if (temp) LOG_INFO("temperature: ts0=...", ...);
```

`get_chip_temperature()`는 PCIe ioctl + firmware 응답이라 ms 단위 비용. 매 프레임마다 호출하면 FPS 측정 자체가 왜곡된다. 현재 `inference_time` 측정 구간 밖에 있어서 다행이지만 여전히 무거움.

**수정안**

- `processed % 10 == 0` 형태로 가끔만, 또는
- `LOG_TRACE`로 강등, 또는
- `print_inference_statistics()` 시점에 한 번만

---

## Important — 구조 개선

### I1. `hailo_infer.hpp:12` — 헤더에 `using namespace hailort;`

이 헤더를 include한 모든 파일이 글로벌 스코프에 `hailort::` 끌어옴. 헤더에서 `using namespace`는 안 좋은 패턴.

**수정**: 헤더에서 제거하고 `cpp`에서만 `using` 사용.

### I2. `hailo_infer.hpp:17` — `std::optional<std::reference_wrapper<Device>>`

생성자에서 항상 채워지고 `nullopt` 상태가 의미 없다. 비-소유 포인터를 표현하기에는 raw pointer가 더 직관적.

```cpp
Device *device = nullptr;  // 비-소유 (VDevice가 소유)
```

### I3. `logging.cpp` — CSV 파일 fclose 안 함

`g_csv_file`이 프로그램 종료 시 닫히지 않음. `fflush`가 매 줄마다 들어가서 데이터 손실은 없지만 깔끔하지 않음.

**수정**: `atexit()`에 `fclose` 등록, 또는 RAII로 감싸기.

### I4. `LOG_ERROR` + `std::cerr` 중복 출력

```cpp
LOG_ERROR("failed to write input vstream: status=%d", static_cast<int>(status));
std::cerr << "Failed to write to input vstream, status = " << status << std::endl;
```

같은 내용이 두 번 나옴. `std::cerr` 줄 제거.

같은 패턴이 `infer()`의 write/read 실패 처리, `classifier.cpp`의 exception 캐치에도 있음.

### I5. `logging.cpp` — `path[256]` 고정 크기 버퍼

log_dir 경로가 길면 잘림. `snprintf` 잘림 감지도 안 함.

**수정**: `std::string` 사용.

### I6. `logging.cpp:64` — `va_list` 두 번 시작

```cpp
va_start(args, fmt); vfprintf(stderr, fmt, args); va_end(args);
...
va_start(args2, fmt); vsnprintf(msgbuf, ..., args2); va_end(args2);
```

stderr에 한 번, CSV 위해 다시 vsnprintf. fmt를 두 번 파싱하는 비효율.

**수정**: 한 번 vsnprintf로 buffer 만들고 stderr/CSV 둘 다 그 buffer를 사용.

```cpp
char msgbuf[1024];
va_list args;
va_start(args, fmt);
vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
va_end(args);

fprintf(stderr, "...%s%s\n", msgbuf, _LOG_COL_RESET);
if (g_csv_file) { ... msgbuf 이스케이프해서 쓰기 ... }
```

---

## Minor — 스타일/정리

### M1. `hailo_infer.cpp:3` — `<cstring>` 미사용

include 정리.

### M2. `classifier.cpp:9` — 상대경로 include

```cpp
#include "../common/logging.hpp"
```

다른 파일들은 `"logging.hpp"`로 쓰는데 이것만 상대경로. CMakeLists에 이미 `../common`이 include path에 들어있어서 `"logging.hpp"`로 통일 가능.

### M3. `logging.hpp` — `_LOG_COL_*` 매크로 prefix `_`

표준상 `_`로 시작하는 글로벌 식별자는 구현체 예약.

**수정**: `LOG_COL_*` 또는 `LOGGER_COL_*`.

### M4. `toolbox.cpp:41` — `is_image_file` TRACE가 너무 verbose

디렉토리 스캔할 때마다 모든 파일에 TRACE 한 줄씩. 비-이미지 파일도 포함. 노이즈가 너무 많으면 제거 고려.

### M5. `toolbox.cpp:62` — `is_directory_of_images` TRACE 위치

모든 항목 검사 후에야 찍힘. 도중에 false 반환하면 종료 TRACE가 안 찍혀서 일관성이 떨어짐.

### M6. `classifier.cpp:138` — `std::cerr << "ERROR: ..."` 잔존

`LOG_ERROR` 추가했는데 `std::cerr`도 남겨둠. 둘 중 하나만.

### M7. `build.sh:11` — 기본 `LOG_LEVEL=0`

TRACE는 너무 시끄러워서 일상 빌드 기본값으로 부적절. INFO(2) 또는 DEBUG(1) 권장. 디버깅할 때만 `LOG_LEVEL=0`.

### M8. `CMakeLists.txt:53-55` — `LOG_LEVEL` 기본값 처리 없음

`-DLOG_LEVEL` 안 주면 `logging.hpp` 내부 기본값(NDEBUG에 따라 INFO/DEBUG)으로 감. 의도된 동작이긴 한데, 명시적으로 cmake에서도 기본값 두는 것도 방법.

### M9. `print_inference_statistics()` — 0으로 나누기 가능성

```cpp
double fps = frame_count / inference_time.count();
double latency = 1000.0 / fps;
```

`processed > 0` 조건으로 호출되긴 하나 `inference_time == 0`은 이론적으로 가능. 가드 없음.

---

## 잘 된 부분

- 로깅 시스템 전체 디자인이 깔끔함 (컴파일/런타임 두 단계, 매크로로 호출위치 캡처, stderr+CSV 분리)
- `__attribute__((format(printf, 7, 8)))` — 컴파일 타임 포맷 검증
- `LOG_TRACE`가 `do {} while(0)`로 컴파일 타임 제거 — 릴리즈에서 코드 사라짐
- vstream shape, frame_size, status 등 디버깅에 유용한 값들 잘 캡처
- 에러 경로마다 `LOG_ERROR` 깔려있음
- `set_log_file()`이 디렉토리 자동 생성 (`mkdir`) — 사용성 좋음
- CSV 헤더 자동 작성
- 빌드 시 `LOG_LEVEL` 환경변수로 제어 — 워크플로우 좋음

---

## 우선순위 권장

1. **C1** `localtime_r` — 멀티스레드 환경에서 실제 깨짐 (infer() read 스레드 여러개일 때)
2. **C2** mutex — 같은 이유로 로그 줄 섞임
3. **C4** 매 추론 온도 측정 — 통계 시점에 한 번만 또는 N프레임마다
4. **I1** `using namespace` in header — 정리 필요
5. **I4** `std::cerr` 중복 — 단순 cleanup
6. **M7** `build.sh` 기본 `LOG_LEVEL` — DEBUG(1)로 올리는 거 추천

C1, C2는 실제 데이터 깨짐 가능성이 있어서 우선순위 높음.
