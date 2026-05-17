# Classification (EfficientNet-M)

ImageNet-based image classification using EfficientNet-M on Hailo8 NPU.

```
input image → preprocess → EfficientNet-M HEF → top-1 class + confidence
```

## Requirements

- HailoRT >= 4.19.0
- OpenCV >= 4.2
- CMake >= 3.20

## Resources

| File | Path | Source |
|---|---|---|
| `efficientnet_m.hef` | `hef/` | Hailo Model Zoo |

Download:
```bash
wget -P hef https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.18.0/hailo8/efficientnet_m.hef
```

## Quick Start

```bash
# 1. set task in root config.yaml
task: classification

# 2. build + run from repo root
./starter.sh
```

## config.yaml

```yaml
classification:
  input: images/                 # relative to paths.input_root
  net_path: src/classification/hef/
  net: efficientnet_m.hef
  threshold: 0.30
```

Results below `threshold` confidence are discarded.

## Logs

Saved as `local_data/output/classification/logs/YYYY-MM-DD_HHMMSS.csv` on every run.

