SAHI Object Detection
=====================

An object detection example that runs on Hailo-8 / Hailo-8L / Hailo-10H devices using the **SAHI (Sliced Aided Hyper Inference)** technique.
The input image/video is split into small slices that are inferred independently; the detections are then remapped back to the original coordinate space and merged with NMM (Non-Maximum Merging) to remove duplicates. This outperforms plain single-shot inference on high-resolution footage that contains many small objects.

Two binaries are produced:

- `object_detection`        — processes a single image or a folder of images
- `object_detection_video`  — processes a video file (with per-frame EMA latency / FPS overlay)


Implementation Highlights
-------------------------

### Slicing and merging
- The input frame is cut into overlapping slices sized to the model's input resolution. `overlap_height` / `overlap_width` (0.0–1.0) control how much adjacent slices overlap so that objects sitting on a slice boundary are still captured.
- Each slice is inferred separately. Detections are remapped to the original image coordinates and then merged with **NMM** (Non-Maximum Merging) using `nmm_iou_threshold` as the IoU cutoff for treating two boxes as the same object.
- Class scores below `threshold` are discarded before merging.

### Supported models
The HEF must include the HailoRT Post-process (NMS) op, so only detection models compatible with HailoRT-Postprocess are supported:

- YOLOv5, YOLOv6, YOLOv7, YOLOv8, YOLOv10, YOLOv11
- YOLOX
- SSD
- CenterNet

Default model: `yolov8m.hef`.

### Video pipeline
`object_detection_video` runs the following stages per frame:

```
VideoCapture.read → slice → preprocess → infer_batch → postprocess(remap + NMM + draw) → VideoWriter.write
```

- **Target FPS**: hard-coded as `TARGET_FPS = 3.0`. If the source video has a higher fps, frames are skipped to match the target, and the output video is also written at 3 fps. Adjust at [object_detection_video.cpp:73](src/SAHI_object_detection/object_detection_video.cpp#L73) if needed.
- **Slice cap**: `MAX_SLICES = 32`. If a frame would produce more slices than this, the frame is skipped.
- **EMA overlay**: per-frame total latency and FPS are smoothed with an exponential moving average (`EMA_ALPHA = 0.1`) and drawn in the top-left corner.

### Configuration-driven (no CLI args)
The program reads only the repository-root [`config.yaml`](../../config.yaml). All inputs are resolved under `paths.input_root` and outputs are written under `paths.output_root/<task>/{results,logs}/`.

```yaml
sahi_object_detection:
  mode: video                     # image | video
  input: videos/testvideo.mp4     # → local_data/input/videos/testvideo.mp4
  net: src/SAHI_object_detection/hef/yolov8n.hef
  threshold: 0.80                 # class score threshold (drop below)
  nmm_iou_threshold: 0.10         # IoU threshold used to merge cross-slice duplicates
  overlap_height: 0.30            # vertical slice overlap ratio (0.0 ~ 1.0)
  overlap_width: 0.30             # horizontal slice overlap ratio (0.0 ~ 1.0)
```

| Key | Description |
|---|---|
| `mode` | `image` or `video` (must match `--mode` passed to `build.sh`) |
| `input` | Path relative to `input_root`. Single image, image folder, or video file. |
| `net` | HEF path. The model's input resolution determines the size of one slice. |
| `threshold` | Detection score threshold. Lowering it yields more detections but more false positives. |
| `nmm_iou_threshold` | IoU cutoff used by NMM to remove duplicates across slice boundaries. |
| `overlap_height`, `overlap_width` | Slice overlap ratios. Use enough overlap to cover boundary objects. |

### Logging
At runtime a CSV log is created under [`local_data/output/sahi_object_detection/logs/`](../../local_data/output/sahi_object_detection/logs/) as `YYYY-MM-DD_HHMMSS.csv`. Columns are shared across all tasks:

```
timestamp,level,file,line,func,message
```

Video inference writes one row per frame:

```
[sahi-vid] frame=12/450 slices=4 slice=2.1 prep=3.4 infer=18.7 post=1.9 total=26.1 ms  ema=27.0 ms (37.04 FPS)  det=42->18
```

`HailoInfer` periodically appends chip temperature to the same CSV:

```
temperature: ts0=58.6 C ts1=58.9 C
```


How to Run
----------

Build and run in one step — `build.sh` builds only the binary for the selected mode and then launches it:

```shell
cd src/SAHI_object_detection
./build.sh --mode image     # single image / image folder
./build.sh --mode video     # video file
```

Output locations (created automatically by the program):

| Type | Path |
|---|---|
| Results (image/video) | `local_data/output/sahi_object_detection/results/` |
| Logs (CSV) | `local_data/output/sahi_object_detection/logs/YYYY-MM-DD_HHMMSS.csv` |

- Image input → `results/<original_filename>`
- Video input → `results/<original_filename>_annotated.mp4`