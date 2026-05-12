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

The following files are required before building:

| File | Path | Source |
|---|---|---|
| `clip_vit_b_16_text_encoder.hef` | `hef/` | Hailo Model Zoo |
| `clip_vit_b_16_image_encoder.hef` | `hef/` | Hailo Model Zoo |
| `clip_vit_b_16_token_embedding.npy` | `npy/` | Extract from OpenAI CLIP |
| `clip_vit_b_16_text_projection.npy` | `npy/` | Extract from OpenAI CLIP |
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
  encoder_path: src/zero_shot_classification/hef/
  text_encoder:  clip_vit_b_16_text_encoder.hef
  image_encoder: clip_vit_b_16_image_encoder.hef
  npy_path:      src/zero_shot_classification/npy/
  text_projection: clip_vit_b_16_text_projection.npy
  embedding_npy:   clip_vit_b_16_token_embedding.npy
  bpe_vocab: src/zero_shot_classification/tokenizer/bpe_simple_vocab_16e6.txt
  prompts_file: src/zero_shot_classification/text_label.yaml
  threshold: 0.30
```

Prompts are managed via `text_label.yaml` — add or remove labels without recompiling.

## Logs

Saved as `logs/YYYY-MM-DD_HHMMSS.csv` on every run.

