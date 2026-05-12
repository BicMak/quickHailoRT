# TODO

## 진행 중

- [ ] zero_shot_classification synset 카테고리 매핑 정확도 개선
  - 탈것 범위(`n027xxxxx`) 재정의 필요 — 잡화 클래스 혼입
- [ ] `analyze_logs.py` 에 중분류 기준 정확도 평가 추가
- [ ] .hef 및 .npy gitignore 반영 후 download_resource.sh 에서 링크 새로 맞춰주기
- [ ] classification readme 작성해서 기록 해두기
- [ ] zeroshot classification readme 작성하기

## 완료

### 2026-05-12

- [x] **HailoInfer 멀티모델 리팩토링**
  - `HailoVStreamPool` 중간 구조체 제거 → `HailoModelState` 에 `input_vstreams` / `output_vstreams` 직접 flatten
  - `push_back(std::move(state))` — InputVStream move-only 제약 대응
  - single-model delegating constructor 유지 (backward-compatible)

- [x] **루트 `config.yaml` 통합**
  - 기존 task별 분산 yaml → 루트 하나로 통합
  - `task` / `input` / `output` 공통 필드, task별 블록(`classification`, `zero_shot_classification`) 분리
  - `encoder_path` + `npy_path` prefix 방식으로 경로 관리

- [x] **`cli.hpp` BaseConfig 리팩토링**
  - `BaseConfig` 도입 — `task`, `input`, `output` 공통 필드 상속
  - `load_base_config()` / `add_base_options()` 공통 함수 분리
  - `load_config` / `load_zeroshot_config` 가 루트 yaml 의 task 블록 읽도록 수정
  - `join_path()` 헬퍼 — `encoder_path` + 파일명 합치기
  - `prompts_file` 지원 — 별도 yaml 에서 prompts 로드

- [x] **`starter.sh` 작성**
  - `config.yaml` 의 `task` 읽어서 해당 task 빌드 + 실행
  - `LOG_LEVEL` 환경변수 지원
  - 추가 인자(`$@`) 바이너리로 전달

- [x] **`text_label.yaml` prompts_file 참조 방식 전환**
  - config.yaml 인라인 prompts 제거 → `prompts_file` 키로 외부 yaml 참조
  - `text_label.yaml` — 동물/탈것/전자기기/건축 등 100개 이상 레이블

- [x] **config.yaml 경로 통일**
  - `clip_example.cpp`, `classifier.cpp` — task별 yaml 경로 → 루트 `config.yaml` 로 통일

- [x] **`scripts/` 정리** — `CheckRequirements.sh` / `Install.sh` / `Verify.sh` 루트에서 scripts/ 로 이동

- [x] **`README.md` 작성** (루트)
  - 설치 3단계 (CheckRequirements → Install → Verify)
  - Quick Start, config.yaml 설명, 지원 Task 플로우, 프로젝트 구조, HailoInfer API 예시

- [x] **`docs/CPP_VSTREAM_OWNERSHIP.md` 작성**
  - 참조 / Shallow Copy / Deep Copy 개념
  - InputVStream move-only 설계 이유
  - `vector<InputVStream>` 복사 에러 원인
  - HailoInfer 소유권 사슬 분석

### 이전

- [x] HailoInfer 멀티모델 리팩토링 (multi-model constructor 도입)
- [x] Main 경로 스크립트 정리 → `scripts/` 폴더로 이동

## 백로그

- [ ] `tokenizer.hpp` 중복 제거 — `BYTES_ENCODER` / `BYTES_DECODER` 미사용, `BYTES_ENCODER_MAP` 만 남기기
- [ ] `NUM_TOKENS = 77` 하드코딩 중복 제거 (`clip_example.cpp` vs `tokenizer.cpp`)
- [ ] `common.h` 미사용 `TSQueue` 정리
- [ ] `encode_texts` 함수 분리 — 토크나이징 / 추론 / 프로젝션 단계 분리
