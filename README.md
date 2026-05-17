# quickHailoRT

Inference framework for the Hailo8 NPU. Switch tasks via a single `config.yaml` and build + run with one `starter.sh` call.

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

Override the task on the command line (skips the `task:` line in `config.yaml`):
```bash
./starter.sh --task sahi_object_detection
```

Change log level:
```bash
LOG_LEVEL=0 ./starter.sh   # 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE
```

## config.yaml

A single root config drives every task. Inputs are resolved under `paths.input_root`; outputs are written under `paths.output_root/<task>/{results,logs}/` (the binaries enforce this layout — no per-task `output:` key).

```yaml
task: sahi_object_detection      # task to run

paths:
  input_root:  local_data/input
  output_root: local_data/output

classification:
  input: images/                 # relative to input_root
  net_path: src/classification/hef/
  net: efficientnet_m.hef
  threshold: 0.30

zero_shot_classification:
  input: images/
  threshold: 0.30
  encoder_path: src/zero_shot_classification/hef/
  text_encoder: clip_vit_b_16_text_encoder.hef
  image_encoder: clip_vit_b_16_image_encoder.hef
  npy_path: src/zero_shot_classification/npy/
  text_projection: clip_vit_b_16_text_projection.npy
  embedding_npy: clip_vit_b_16_token_embedding.npy
  bpe_vocab: src/zero_shot_classification/tokenizer/bpe_simple_vocab_16e6.txt
  prompts_file: src/zero_shot_classification/text_label.yaml

sahi_object_detection:
  mode: video                    # image | video
  input: videos/testvideo.mp4
  net: src/SAHI_object_detection/hef/yolov8n.hef
  threshold: 0.80
  nmm_iou_threshold: 0.10
  overlap_height: 0.30
  overlap_width: 0.30
```

Changing the top-level `task:` field is all it takes to switch tasks.

## Supported Tasks

### classification

EfficientNet-based image classification.

```
input image → EfficientNet HEF → top-1 class + confidence
```

### zero_shot_classification

Zero-shot classification with CLIP ViT-B/16.

```
prompts → BPE tokenization → token embedding → text encoder HEF → text embeddings
input image → image encoder HEF → image embedding
→ cosine similarity → softmax → argmax
```

Prompts are managed via `prompts_file` (yaml). Default: `text_label.yaml` (100+ labels across animals, vehicles, electronics, etc.).

### sahi_object_detection

SAHI (Sliced Aided Hyper Inference) detection on Hailo. The frame is split into overlapping slices sized to the model input, each slice is inferred, and detections are merged back to the original image with NMM. See [src/SAHI_object_detection/README.md](src/SAHI_object_detection/README.md) for details.

Two binaries are built from this task; `starter.sh` selects between them based on `sahi_object_detection.mode` in `config.yaml`:

| `mode` | binary | input |
|---|---|---|
| `image` | `SAHI_object_detection` | single image or image folder |
| `video` | `SAHI_object_detection_video` | video file (3 FPS target, EMA latency/FPS overlay) |

Supports detection models whose HEF includes HailoRT-Postprocess (YOLOv5/6/7/8/10/11, YOLOX, SSD, CenterNet).


## Logs

Each run writes a CSV log to `local_data/output/<task>/logs/YYYY-MM-DD_HHMMSS.csv` with columns:

```
timestamp,level,file,line,func,message
```

Analysis helpers under [scripts/](scripts/):

```bash
python3 scripts/analyze_log.py        local_data/output/classification/logs/2026-05-12_152609.csv
python3 scripts/analyze_video_log.py  local_data/output/sahi_object_detection/logs/2026-05-12_152609.csv
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