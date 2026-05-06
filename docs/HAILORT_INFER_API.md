# HailoRT 추론 API 아키텍처

HailoRT C++ SDK는 같은 NPU에 대해 **4가지 추론 API 계층**을 제공한다.
모두 "버퍼에 데이터 → HW → 결과 버퍼"라는 동일한 일을 하지만,
**포맷 변환·큐잉·수명관리를 누가 책임지는가**가 다르다.

> 본 문서는 `sources/hailort/libhailort/examples/cpp/`의 예시 4종을 기준으로 정리.
> 프로젝트 코드 [`src/common/hailo_infer.cpp`](../src/common/hailo_infer.cpp)는
> 이 중 **InferModel + Bindings (Async)** 방식을 사용한다.

---

## 1. 공통 토대 — 모델 로딩까지



어느 API를 쓰든 여기까지는 동일하다:

```
HEF 파일
   ↓ Hef::create()
Hef                       ← 모델 정의 (그래프, 가중치, 메타데이터)
   ↓ vdevice.configure()  또는  vdevice.create_infer_model()
[NetworkGroup | InferModel]   ← HW에 로드된 "모델 인스턴스"
```

분기점은 **여기서부터** — "이미지 데이터를 HW까지 어떻게 흘려보낼 것인가".

---

## 2. 4가지 데이터 경로

### 2.1 Raw Stream (가장 저수준)

예시: `raw_streams_example`

```
[user buffer (host format)]
        ↓ InputTransformContext::transform()    ← 사용자가 직접 변환
[hw buffer (HW format, quantized)]
        ↓ input.write()                          ← 블로킹
[HW]
        ↓ output.read()                          ← 블로킹
[hw buffer]
        ↓ OutputTransformContext::transform()    ← 사용자가 직접 역변환
[user buffer]
```

| 항목 | 내용 |
|---|---|
| 동기/비동기 | 동기 (블로킹) |
| 포맷 변환 | **사용자 코드** (`TransformContext` 명시 호출) |
| 버퍼 | host용 / hw용 두 벌 |
| 장점 | 변환 과정 완전 통제 |
| 단점 | 보일러플레이트 많음 |

### 2.2 VStream (Virtual Stream)

예시: `vstreams_example`

```
[user buffer (사용자 지정 포맷)]
        ↓ input.write()    ← 내부에서 자동 변환 + 큐잉
[HW]
        ↓ output.read()    ← 내부에서 자동 역변환
[user buffer]
```

VStream = **Stream + TransformContext + 내부 큐**가 합쳐진 단위.

| 항목 | 내용 |
|---|---|
| 동기/비동기 | 동기 (블로킹) — 내부 큐로 파이프라이닝 효과 일부 |
| 포맷 변환 | 라이브러리 |
| 버퍼 | VStream마다 user 포맷 1개 |
| 장점 | 멀티스레드 스트리밍에 적합 (입출력별 별도 스레드) |
| 단점 | write 호출 단위로 블로킹 — 진정한 async는 아님 |

### 2.3 InferVStreams (Pipeline)

예시: `infer_pipeline_example`

```
{ "input1": [N프레임 buffer], "input2": [...] }
        ↓ pipeline.infer(inputs, outputs, N)   ← 한 번에 N프레임 처리
{ "output1": [N프레임 buffer], ... }
```

VStream들을 **하나의 파이프라인**으로 묶어 N프레임을 한 번에 동기 처리.

| 항목 | 내용 |
|---|---|
| 동기/비동기 | 동기, 일괄 |
| 포맷 변환 | 라이브러리 |
| 버퍼 | name별 N프레임짜리 통짜 버퍼 |
| 장점 | 가장 단순 — `infer()` 한 번 호출로 끝 |
| 단점 | 굵은 단위, 스트리밍에 부적합 |

### 2.4 InferModel + Bindings (Async)

예시: `async_infer_basic_example`, `async_infer_advanced_example`
**프로젝트 사용 방식**: [`src/common/hailo_infer.cpp`](../src/common/hailo_infer.cpp)

```
InferModel
   ↓ configure()
ConfiguredInferModel       ← HW에 활성화된 인스턴스
   ↓ create_bindings()     ← "이번 추론에 쓸 버퍼 묶음"
Bindings
   ├─ input("name")  → set_buffer(MemoryView)
   └─ output("name") → set_buffer(MemoryView)
   ↓ run_async(bindings, callback)
[비동기 — 콜백으로 결과 알림]
```

| 항목 | 내용 |
|---|---|
| 동기/비동기 | **진짜 비동기** (콜백 기반) |
| 포맷 변환 | 라이브러리 (InferModel 단계에서 set_format_type) |
| 버퍼 | MemoryView로 **포인터만 등록** — 수명은 사용자가 보장 |
| 장점 | 최대 throughput, batching, pipelining |
| 단점 | 수명 관리 부담 (가드 필요) |

---

## 3. "버퍼 유닛"의 정체

각 API가 부르는 "버퍼"는 물리적으로는 다 그냥 메모리 청크지만, **논리적 역할이 다르다**:

| API | 버퍼의 정체 | 소유권 | 변환 위치 |
|---|---|---|---|
| Raw Stream | host용/hw용 raw bytes 2개 | 사용자 | 사용자 코드 |
| VStream | user 포맷 bytes 1개 | 사용자 (수명만) | VStream 내부 |
| InferVStreams | name별 N프레임 통짜 bytes | 사용자 | Pipeline 내부 |
| **Bindings** | **MemoryView로 등록한 포인터** | **사용자 (수명 보장 필수)** | **InferModel 내부** |

---

## 4. 프로젝트 코드 매핑

[`src/common/hailo_infer.cpp`](../src/common/hailo_infer.cpp)의 `set_input_buffers`
([L71-92](../src/common/hailo_infer.cpp#L71-L92)):

```cpp
auto bindings = configured_infer_model.create_bindings()...
bindings.input(input_name)->set_buffer(MemoryView(input.data, frame_size));
image_guards.push_back(std::make_shared<cv::Mat>(input));
```

세 줄에 비동기 API의 본질이 다 담김:

1. `set_buffer`는 **데이터를 복사하지 않고 포인터만 등록**한다.
2. `run_async`는 **즉시 리턴**하고 HW가 백그라운드에서 DMA를 일으킨다.
3. 이때 원본 `cv::Mat`이 소멸되면 **HW가 dangling memory를 읽게 된다**.
4. → `image_guards`에 `shared_ptr<cv::Mat>`을 박아 콜백 종료까지 **수명 연장**.

VStream API라면 `write()`가 블로킹이라 함수 리턴 시점에 이미 데이터가 HW로 넘어간 뒤이므로
이런 가드가 필요 없다. **이 차이가 두 아키텍처의 결정적 분기점**이다.

### `multiple_bindings`가 vector인 이유

[L78](../src/common/hailo_infer.cpp#L78)에서 `batch_size`만큼 Bindings를 만들어 vector에 쌓는다.

- Bindings 1개 = "프레임 1장의 입출력 디스크립터"
- Hailo는 `run_async(vector<Bindings>)` 오버로드를 지원
- → 여러 프레임을 **한 번의 async submit**으로 큐잉 (batching 최적화)

VStream 방식에서 N번 `write()` 호출할 일을 한 방에 끝내는 셈.

---

## 5. 한 장 요약

```
                       추상화    동기/비동기   버퍼 단위
Raw Stream             낮음      동기          host+hw 두 개
VStream                중        동기          user 포맷 1개
InferVStreams          높음      동기 일괄     name별 N프레임 묶음
InferModel + Bindings  높음      비동기        포인터 등록 (수명 가드 필요)
```

**왜 4개나 있나** — 사용자 트레이드오프가 다르기 때문:

| 원하는 것 | 적합한 API |
|---|---|
| 최대 제어권 / 커스텀 변환 | Raw Stream |
| 가장 간편 / 한 방 호출 | InferVStreams |
| 스트리밍 (카메라 프레임) / 멀티스레드 | VStream |
| **최대 throughput / batching / pipelining** | **InferModel + Bindings** |

프로젝트의 `image_guards`, `output_guards`, `multiple_bindings`는
**"비동기 추론에서 HW가 메모리를 다 쓸 때까지 수명을 늘리기 위한 장치"**라는 한 문장으로 요약된다.
이게 비동기 API에만 있는 추가 부담이고, 그 대가로 throughput을 얻는다.