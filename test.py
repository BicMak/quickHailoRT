"""
Minimal Hailo standalone inference — single image, raw output.
Goal: verify the chip works and shows what a YOLO model outputs.
"""

import cv2
import numpy as np
from hailo_platform import (
    HEF, VDevice, ConfigureParams, HailoStreamInterface,
    InferVStreams, InputVStreamParams, OutputVStreamParams, FormatType,
)

HEF_PATH = "/usr/local/hailo/resources/models/hailo8/yolov8m.hef"
IMAGE_PATH = "/usr/local/hailo/resources/images/bus.jpg"

# ── 1. Load the compiled model ────────────────────────────────────────────────
hef = HEF(HEF_PATH)
input_info = hef.get_input_vstream_infos()[0]
input_shape = input_info.shape  # (H, W, C)
print(f"Model expects input: name={input_info.name}, shape={input_shape}")

# ── 2. Preprocess image ───────────────────────────────────────────────────────
img = cv2.imread(IMAGE_PATH)
if img is None:
    raise FileNotFoundError(f"Image not found: {IMAGE_PATH}")

h, w = input_shape[0], input_shape[1]
resized = cv2.resize(img, (w, h))                    # OpenCV is BGR
rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)        # YOLO usually wants RGB
inp = np.expand_dims(rgb, axis=0).astype(np.uint8)    # (1, H, W, C)

# ── 3. Run inference on Hailo chip ────────────────────────────────────────────
with VDevice() as device:
    # Configure the network group on the device
    configure_params = ConfigureParams.create_from_hef(
        hef, interface=HailoStreamInterface.PCIe
    )
    network_group = device.configure(hef, configure_params)[0]
    network_group_params = network_group.create_params()

    # Define vstream params (UINT8 is the native format for quantized models)
    input_vstreams_params = InputVStreamParams.make_from_network_group(
        network_group, format_type=FormatType.UINT8
    )
    output_vstreams_params = OutputVStreamParams.make_from_network_group(
        network_group, format_type=FormatType.FLOAT32
    )

    # Run synchronous inference
    with InferVStreams(network_group, input_vstreams_params, output_vstreams_params) as pipeline:
        with network_group.activate(network_group_params):
            outputs = pipeline.infer({input_info.name: inp})

# ── 4. Parse YOLO + NMS output ────────────────────────────────────────────────
# Output layout from `yolov8_nms_postprocess`:
#   outputs[name] is a list (or batched list) where each entry is:
#     [class_0_dets, class_1_dets, ..., class_79_dets]
#   Each class_i_dets is an ndarray of shape (N_i, 5):
#     [ymin, xmin, ymax, xmax, score]   — coords normalized to [0, 1]
COCO_LABELS = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush",
]

SCORE_THRESHOLD = 0.5

print(f"\nReceived {len(outputs)} output(s):")
output_name = next(iter(outputs))
raw = outputs[output_name]
print(f"  output name: {output_name}")
print(f"  output type: {type(raw).__name__}, length: {len(raw)}")

# Some HEFs return [class_lists] directly; others wrap one extra batch dim.
# Unwrap if the first element is itself a list of length 80 (num classes).
if isinstance(raw[0], list) and len(raw[0]) == len(COCO_LABELS):
    class_dets = raw[0]
else:
    class_dets = raw

img_h, img_w = img.shape[:2]
detections = []
for class_id, dets in enumerate(class_dets):
    arr = np.asarray(dets)
    if arr.size == 0:
        continue
    for det in arr:
        ymin, xmin, ymax, xmax, score = det
        if score < SCORE_THRESHOLD:
            continue
        detections.append({
            "label": COCO_LABELS[class_id],
            "score": float(score),
            "box": (
                int(xmin * img_w), int(ymin * img_h),
                int(xmax * img_w), int(ymax * img_h),
            ),
        })

print(f"\nFound {len(detections)} detection(s) with score >= {SCORE_THRESHOLD}:")
for d in detections:
    x1, y1, x2, y2 = d["box"]
    print(f"  {d['label']:15s}  score={d['score']:.2f}  box=({x1},{y1})-({x2},{y2})")

# ── 5. Draw boxes and save ────────────────────────────────────────────────────
out = img.copy()
for d in detections:
    x1, y1, x2, y2 = d["box"]
    cv2.rectangle(out, (x1, y1), (x2, y2), (0, 255, 0), 2)
    label = f"{d['label']} {d['score']:.2f}"
    cv2.putText(out, label, (x1, max(20, y1 - 5)),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

out_path = "/home/jiwon/hailo/test_output.jpg"
cv2.imwrite(out_path, out)
print(f"\nSaved annotated image to: {out_path}")