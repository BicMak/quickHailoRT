# quickHailoRT

Inference framework for Hailo8 NPU. Switch tasks via a single `config.yaml` and build + run with one `starter.sh` call.

## Requirements

- HailoRT >= 4.19.0
- OpenCV >= 4.2
- CMake >= 3.20

---

## Installation

> Place `.deb` / `.whl` files under `install_file/{arm,x86}/` before running.

### Step 1 — Pre-install check

```bash
./scripts/CheckRequirements.sh
```

Validates the system before installation:
- Hailo8 PCIe hardware detection
- Kernel headers present (for DKMS driver build)
- Required apt packages (`build-essential`, `cmake`, `dkms`, etc.)
- Python version (3.10–3.12)
- `.deb` / `.whl` files present under `install_file/` with matching versions
- Disk space, network connectivity, and conflicts with existing Hailo installs

Prints the installation commands when all checks pass.

### Step 2 — Install

```bash
sudo ./scripts/Install.sh
```

Runs in order:
1. PCIe driver `.deb` (DKMS)
2. HailoRT runtime `.deb`
3. Creates `venv_hailoRT/` and installs PyHailoRT `.whl`

**Reboot required** after completion.

### Step 3 — Verify (after reboot)

```bash
sudo ./scripts/Verify.sh
```

- Firmware load confirmed in `dmesg`
- Device recognized via `hailortcli scan`
- `hailort` package found in venv

---

## Quick Start

```bash
# 1. Set task in config.yaml
vi config.yaml

# 2. Build + run
./starter.sh
```

Change log level:
```bash
LOG_LEVEL=0 ./starter.sh   # 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR
```

## config.yaml

```yaml
task: zero_shot_classification   # task to run
input: local_data/input_img      # input image or directory
output: local_data/output_img    # output path

classification:
  net: src/classification/hef/efficientnet_m.hef
  threshold: 0.30

zero_shot_classification:
  encoder_path: src/zero_shot_classification/hef/
  text_encoder: clip_vit_b_16_text_encoder.hef
  image_encoder: clip_vit_b_16_image_encoder.hef
  npy_path: src/zero_shot_classification/npy/
  text_projection: clip_vit_b_16_text_projection.npy
  embedding_npy: clip_vit_b_16_token_embedding.npy
  bpe_vocab: src/zero_shot_classification/tokenizer/bpe_simple_vocab_16e6.txt
  prompts_file: src/zero_shot_classification/text_label.yaml
```

Changing the `task` field is all it takes to switch tasks.

## Supported Tasks

### classification

EfficientNet-based image classification.

```
input image → EfficientNet HEF → top-1 class + confidence
```

### zero_shot_classification

Zero-shot classification using CLIP ViT-B/16.

```
prompts → BPE tokenization → token embedding → text encoder HEF → text embeddings
input image → image encoder HEF → image embedding
→ cosine similarity → softmax → argmax
```

Prompts are managed via `prompts_file` (yaml). Default: `text_label.yaml` (100+ labels across animals, vehicles, electronics, etc.).


## Logs

Saved as `logs/YYYY-MM-DD_HHMMSS.csv` on every run.

```bash
python3 analyze_logs.py logs/2026-05-12_152609.csv
```

## HailoInfer API

Manage multiple models in a single object:

```cpp
// single model
HailoInfer infer("model.hef");

// multi-model (CLIP)
HailoInfer infer({
    { "text_encoder.hef",  HAILO_FORMAT_TYPE_FLOAT32, HAILO_FORMAT_TYPE_FLOAT32 },
    { "image_encoder.hef", HAILO_FORMAT_TYPE_UINT8,   HAILO_FORMAT_TYPE_FLOAT32 },
});

// inference
std::vector<MemoryView> outputs{ MemoryView(buf.data(), buf.size()) };
hailo_status s = infer.infer(MemoryView(input.data(), input.size()), outputs, model_index);
```
