# Zero-Shot Classification (CLIP ViT-B/16)

Zero-shot image classification using CLIP ViT-B/16 on Hailo8 NPU.

```
prompts → BPE tokenization → token embedding → text encoder HEF → text embeddings
input image → image encoder HEF → image embedding
→ cosine similarity → softmax → argmax → label
```


## Requirements

- HailoRT >= 4.19.0
- OpenCV >= 4.2
- CMake >= 3.20

## Resources

The following files are required before building. The `.npy` files can be downloaded directly from the links in the **Source** column below — extracting them yourself from OpenAI CLIP is optional (see the Python snippet further down).

| File | Path | Source |
|---|---|---|
| `clip_vit_b_16_text_encoder.hef` | `hef/` | Hailo Model Zoo |
| `clip_vit_b_16_image_encoder.hef` | `hef/` | Hailo Model Zoo |
| `clip_vit_b_16_token_embedding.npy` | `npy/` | [Extract from OpenAI CLIP](https://drive.google.com/file/d/1BJaqM-yq9rIFtvJxthqgxvIfOcF7W8Xy/view?usp=drive_link) |
| `clip_vit_b_16_text_projection.npy` | `npy/` | [Extract from OpenAI CLIP](https://drive.google.com/file/d/1e9HiXeFzhPpVa45GX5S-4FyMZ4pfF8sx/view?usp=drive_link) |
| `bpe_simple_vocab_16e6.txt` | `tokenizer/` | Hailo CS Data |

**HEF + BPE vocab** — run from this directory:
```bash
./download_resources.sh
```

**NPY files** — extract on a machine with PyTorch + CLIP installed:
```python
import clip, torch, numpy as np

model, _ = clip.load("ViT-B/16", device="cpu", jit=False)
model.eval()

np.save("npy/clip_vit_b_16_token_embedding.npy",
        model.token_embedding.weight.detach().cpu().numpy().astype(np.float32))

tp = model.text_projection
proj = (tp.weight if hasattr(tp, "weight") else tp).detach().cpu().numpy().astype(np.float32)
np.save("npy/clip_vit_b_16_text_projection.npy", proj)
```

## Quick Start

```bash
# 1. set task in root config.yaml
task: zero_shot_classification

# 2. build + run from repo root
./starter.sh
```

## config.yaml

```yaml
zero_shot_classification:
  input: images/                 # relative to paths.input_root
  threshold: 0.30
  encoder_path: src/zero_shot_classification/hef/
  text_encoder:  clip_vit_b_16_text_encoder.hef
  image_encoder: clip_vit_b_16_image_encoder.hef
  npy_path:      src/zero_shot_classification/npy/
  text_projection: clip_vit_b_16_text_projection.npy
  embedding_npy:   clip_vit_b_16_token_embedding.npy
  bpe_vocab: src/zero_shot_classification/tokenizer/bpe_simple_vocab_16e6.txt
  prompts_file: src/zero_shot_classification/text_label.yaml
```

## Prompts (`text_label.yaml`)

Labels are managed in a separate YAML so you can add or remove classes without recompiling:

```yaml
prompts:
  - a photo of a dog
  - a photo of a cat
  - a photo of a car
  - a photo of an airplane
  # ...
```

Default file ships with 100+ labels across animals, vehicles, electronics, etc.

## Logs

Saved as `local_data/output/zero_shot_classification/logs/YYYY-MM-DD_HHMMSS.csv` on every run.

