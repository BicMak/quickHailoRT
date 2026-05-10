# Inference Architecture

The class for using HailoRT is encapsulated in `src/common/hailo_infer.*`,
and uses HailoRT's **VStream API** (synchronous, multi-threaded).

> For a comparison of the 4 API layers provided by HailoRT, see [HAILORT_INFER_API.md](HAILORT_INFER_API.md).

---

## HailoInfer Class

### Constructor — Model Loading and VStream Initialization

```
HEF file
   ↓ Hef::create()
Hef
   ↓ vdevice.configure()
ConfiguredNetworkGroup    ← model loaded onto HW
   ↓ make_input/output_vstream_params()
   ↓ VStreamsBuilder::create_*_vstreams()
InputVStream / OutputVStream   ← includes format conversion + internal queues
```

```cpp
HailoInfer model(hef_path,
                 HAILO_FORMAT_TYPE_UINT8,    // input: uint8 BGR
                 HAILO_FORMAT_TYPE_FLOAT32); // output: float32 logits
```

Specifying input/output formats via constructor parameters lets VStream handle conversions internally.
Setting `HAILO_FORMAT_TYPE_AUTO` uses the default format recorded in the HEF.

### Public Interface

| Method | Return | Description |
|---|---|---|
| `get_input_info()` | `hailo_vstream_info_t` | Query input shape (H/W/C) |
| `get_output_infos()` | `vector<hailo_vstream_info_t>` | Query shape per output stream |
| `infer(input, output_buffers)` | `vector<pair<uint8_t*, info>>` | Run inference on 1 frame |

The `output_buffers` of `infer()` are **owned by the caller**. The returned `uint8_t*` pointers are tied to the lifetime of this vector, so keep the vector alive until post-processing is complete.

---

## infer() Internal Behavior — Why Two Threads

VStream has separate internal input and output queues.
If `write()` and `read()` are called sequentially in the same thread:

```
Process data received via write()
    → register result in output queue
    → nobody calls read()
    → output queue fills up
    → HW stalls unable to push result
    → stops pulling from input queue
    → input queue fills up
    → write() blocks
    → read() is never reached (single thread)
    → deadlock
```

To avoid this, write and read are separated into distinct threads. The I/O buffer handoff to/from the NPU must be offloaded to threads to prevent timeout from queue overflow.

```
[ main thread ]    [ write_thread ]  [ Hailo HW NPU ]   [ read_thread ]
       |                   |                 |                  |
       |---spawn---------->| ---spawn------- | ----spawn----->  |
       |                 write()             |                read()
       |              (copy input) --------> | --------> (copy output)
       |                   |           (Processing)             |
       |                   V                 |                  V
[ write.join ] <--- (All inputs sent)        |                  |
       |                                                        |
[ read.join ] <--------------------------------------  (All outputs received)
     |
     V
[ infer() proceeds ]
```

- write complete = the point at which HW has taken the input data
- read complete = the point at which HW output has been copied into `output_buffers[i]`
- `infer()` proceeds only after both `write_thread.join()` and `read_thread[i].join()` complete
- For models with multiple output streams (multi-head), a read thread is created for each

---

## Data Flow Summary

```
cv::Mat (BGR uint8)
   │
   │  preprocess_image()        toolbox.cpp
   │  - image resizing
   │  - image normalization (optional)
   │  - BGR → RGB
   ▼
cv::Mat (RGB uint8, target HxW)
   │
   │  write_thread: vstream.write(MemoryView)
   │  → VStream internally converts format (uint8 → HW quantized)
   ▼
[Hailo-8 NPU] : inference the input data in NPU
   │
   │  read_thread: vstream.read(MemoryView)
   │  → VStream internally inverse-converts (HW → float32)
   ▼
output_buffers[i]  (float32 raw bytes)
   │
   │  caller casts: reinterpret_cast<float*>(data_ptr)
   ▼
post-processing (argmax / softmax / bbox decode, etc.)
```

---

## Usage Example — classifier.cpp

```cpp
// 1. Load model
HailoInfer model("src/classification/hef/efficientnet_m.hef",
                 HAILO_FORMAT_TYPE_UINT8,
                 HAILO_FORMAT_TYPE_FLOAT32);

// 2. Query input size → preprocess
auto info = model.get_input_info();
cv::Mat preprocessed;
preprocess_image(org, preprocessed, info.shape.width, info.shape.height, false);

// 3. Inference
std::vector<std::vector<uint8_t>> output_buffers;
auto results = model.infer(preprocessed, output_buffers);
// output_buffers must not be freed while results pointers are alive

// 4. Post-processing
const auto &[data_ptr, vstream_info] = results[0];
size_t num_classes = vstream_info.shape.features;
const float *logits = reinterpret_cast<const float*>(data_ptr);
// apply argmax, softmax, etc.
```

---

## VStream Implementation Characteristics

VStream is a **synchronous API** — `write()` and `read()` are each blocking calls.
However, by running write_thread and read_thread concurrently, input and output I/O are parallelized:

- While write_thread is blocking on data transfer to the NPU, read_thread simultaneously pulls results out
- This overlaps I/O wait time with NPU processing time, improving overall throughput
- Buffer lifetime is simple: once `write()` returns, the HW has already taken the data — no need to extend buffer lifetime beyond that point

read_thread is not polling for results — it is **standing by from the start** so that output is captured as soon as the NPU finishes processing. Once input data is fed in, the NPU automatically processes it and places the result in the output queue; read_thread exists to catch that output the moment it appears.

In short, VStream achieves parallel I/O on top of a synchronous API by separating write and read into distinct threads.
