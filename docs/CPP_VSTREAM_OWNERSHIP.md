# C++ 메모리 모델과 InputVStream 설계 정리

> 2026-05-12 대화 기반 정리

---

## 1. 참조 vs Shallow Copy vs Deep Copy

### 참조 (Reference)

객체를 새로 만들지 않고 기존 객체에 이름만 추가.

```
A ──┐
    ├──► [객체: x=10, ptr ──► DMA 버퍼]
B ──┘
```

- 객체는 1개
- `A.x = 99` 하면 `B.x` 도 99 (같은 객체)
- C++ 에서 `InputVStream &ref = vs;` — 전혀 문제없음

### Shallow Copy

객체는 새로 만들지만, 내부 포인터가 가리키는 자원은 공유.

```
A ──► [객체1: x=10, ptr ──┐
                           ├──► DMA 버퍼
B ──► [객체2: x=10, ptr ──┘
```

- 객체는 2개 (각자 독립적인 x)
- `A.x = 99` 해도 `B.x` 는 그대로 10
- 근데 `*A.ptr = 777` 하면 B 도 영향받음 (같은 메모리)

### Deep Copy

객체도 새로 만들고, 내부 자원도 전부 복제.

```
A ──► [객체1: x=10, ptr ──► DMA 버퍼 A]
B ──► [객체2: x=10, ptr ──► DMA 버퍼 B]
```

- 완전히 독립

---

## 2. Shallow Copy 에서 "내부 자원 공유" 의 의미

포인터 변수 자체(stack) 는 각자 독립적이지만, 포인터가 가리키는 대상(heap) 을 공유한다는 뜻.

```c
Foo a;
a.x   = 10;
a.ptr = new int(99);   // heap 에 99

Foo b = a;   // shallow copy
// b.x   = 10         ← 독립적인 복사본
// b.ptr = a.ptr 와 같은 주소  ← 같은 heap 메모리
```

- `b.x = 55` → a.x 영향 없음
- `*b.ptr = 777` → a 도 영향받음

---

## 3. 파이썬 vs C++ 기본 동작 차이

| | 파이썬 | C++ |
|---|---|---|
| `b = a` | 같은 객체를 가리키는 참조 | 새 객체를 복사 생성 (deep/shallow) |
| 참조처럼 쓰려면 | 기본 동작이 그것 | `&b = a` 명시해야 함 |
| 메모리 관리 | GC 가 자동 | 직접 관리 |

파이썬은 기본이 참조, C++ 은 기본이 값 복사.

---

## 4. C++ 포인터 기본

```c
int num = 10;
int *ptr = &num;   // ptr 은 num 의 주소를 담는 변수
```

- `int *ptr = 10;` 은 잘못된 코드 — "주소 10번지를 가리켜라" 는 뜻, 거의 segfault
- 포인터 변수 자체는 선언된 위치(stack/heap) 에 존재
- `heap` 에 값을 올리려면 반드시 `new` 또는 `malloc` 사용

```c
int *ptr = new int(10);   // heap 에 10 저장, ptr 은 그 주소
```

포인터 `+1` 은 바이트 1이 아니라 **타입 크기만큼 증가** (int 면 4바이트).

---

## 5. InputVStream 이 Move-Only 인 이유

`InputVStream` 은 내부에 `shared_ptr<InputVStreamInternal>` 로 **HW DMA 버퍼** 를 소유.

DMA 버퍼는:
- HW 채널과 1:1 대응하는 유일한 자원
- 두 객체가 같은 버퍼를 소유하면 동시 write → race / corruption 위험

그래서 SDK 가 복사 생성자/대입을 아예 금지:

```cpp
InputVStream(InputVStream &&other) noexcept = default;        // move O
InputVStream &operator=(InputVStream &&other) noexcept = default;  // move O
// 복사 생성자/대입 → 선언 없음 → 자동으로 deleted
```

| 종류 | 가능? |
|---|---|
| 참조 (`&`) | ✅ 객체 1개, 이름만 추가 |
| Shallow copy | ❌ 복사 자체가 deleted |
| Deep copy | ❌ 복사 자체가 deleted |
| Move (`std::move`) | ✅ 소유권 이전, 원본은 무효화 |
| 새 vstream 신규 생성 | ✅ HW 자원을 추가 할당 (복제 아님) |

`std::unique_ptr`, `std::thread`, `std::fstream`, `std::mutex` 도 같은 이유로 move-only.

---

## 6. vector\<InputVStream\> 복사 시도 시 에러

`std::vector` 는 복사 시 원소를 deep copy 시도:

```cpp
std::vector<InputVStream> a = ...;
std::vector<InputVStream> b = a;   // ❌ 컴파일 에러
// vector 가 InputVStream 복사 생성자 호출 시도
// → InputVStream 복사 = deleted → 에러
```

에러의 원인은 vector 가 아니라 **원소인 `InputVStream`**.

---

## 7. 코드 구조 분석 — HailoInfer

### 소유권 사슬

```
HailoInfer
 └─ models : vector<HailoModelState>
       ├─ config         : HailoModel
       ├─ input_vstreams : vector<InputVStream>   ← 객체 자체 소유
       ├─ output_vstreams: vector<OutputVStream>  ← 객체 자체 소유
       ├─ network_group  : shared_ptr<...>
       └─ output_vstream_info_by_name : map<...>
```

각 `InputVStream` 은 `vector` 의 한 칸에 정확히 1개만 존재. Move-only 가 이걸 보장.

### 왜 push_back 에 std::move 가 필요했냐

```cpp
// hailo_infer.cpp:32
this->models.push_back(std::move(state));
```

```
push_back(state)        ← state 전체 복사 시도
  → input_vstreams 복사
  → InputVStream 복사 생성자 호출
  → deleted → ❌ 컴파일 에러

push_back(std::move(state))  ← state 전체 이동
  → InputVStream move 생성자 호출
  → ✅
```

### for 문에서 참조는 문제없는 이유

```cpp
for (auto &vs : state.input_vstreams) {
    // auto & → 참조, 복사 없음 → 완전히 안전
}
```

### HailoVStreamPool 제거

원래 `HailoVStreamPool` 이라는 중간 구조체가 있었지만:
- `HailoModelState` 안에서만 사용, 재사용 없음
- `state.pool.input_vstreams` 대신 `state.input_vstreams` 가 더 단순
- Move-only 제약 우회와는 무관, 순수하게 그룹핑 목적이었음

→ 제거하고 `HailoModelState` 에 직접 멤버로 넣음.

### Delegating Constructor

```cpp
HailoInfer::HailoInfer(const std::string &hef_path,
                       hailo_format_type_t input_type,
                       hailo_format_type_t output_type)
    : HailoInfer(std::vector<HailoModel>{{hef_path, input_type, output_type}}) {}
```

단일 모델 생성자가 멀티 모델 생성자를 호출하는 C++11 위임 생성자.  
기존 호출자를 깨지 않으면서 로직 중복을 피하는 backward-compatible wrapper.
