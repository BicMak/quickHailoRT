# Hailo-8 / Hailo-8L Release Versions Compatibility

> Source: Hailo Developer Zone — Release Versions Compatibility chart for Accelerators (Hailo-8 / Hailo-8L).

Hailo SW products are compatible with each other on specific versions. When upgrading a product, the others should be updated accordingly. The preferred option is to use **Hailo SW Suites** — both the **AI SW Suite** and the **Vision Processor Software Package (VPSP)**, which align compatible versions.

> **Note:** The HailoRT column relates both to the HailoRT library and the driver. The Integration Tool column relates to Hailo Accelerator Integration Tool.

## Compatibility Matrix

| AI SW Suite | Dataflow Compiler | HailoRT | Integration Tool | Model Zoo | TAPPAS |
|---|---|---|---|---|---|
| 2025-10 | v3.33.0 | v4.23.0 | — | v2.17.0 | v5.1.0 |
| 2025-07 | v3.32.0 | v4.22.0 | — | v2.16.0 | v5.0.0 |
| 2025-04 | v3.31.0 | v4.21.0 | — | v2.15.0 | — |
| 2025-01 | v3.30.0 | v4.20.0 | v1.20.0 | v2.14.0 | v3.31.0 |
| 2024-10 | v3.29.0 | v4.19.0 | v1.19.0 | v2.13.0 | v3.30.0 |
| 2024-07.1 | v3.28.0 | v4.18.0 | v1.18.0 | v2.12.0 | v3.29.1 |
| 2024-07 | v3.28.0 | v4.18.0 | v1.18.0 | v2.12.0 | v3.29.0 |
| 2024-04 | v3.27.0 | v4.17.0 | v1.17.0 | v2.11.0 | v3.28.0 |
| 2024-01 | v3.26.0 | v4.16.0 | v1.16.0 | v2.10.0 | v3.27.0 |
| 2023-10 | v3.25.0 | v4.15.0 | v1.15.0 | v2.9.0 | v3.26.0 |
| 2023-07.1 | v3.24.0 | v4.14.0 | v1.14.1 | v2.8.0 | v3.25.0 |
| 2023-07 | v3.24.0 | v4.14.0 | v1.14.0 | v2.8.0 | v3.25.0 |
| 2023-04 | v3.23.0 | v4.13.0 | v1.13.0 | v2.7.0 | v3.24.0 |
| 2023-01.1 | v3.22.1 | v4.12.1 | v1.12.0 | v2.6.1 | v3.23.1 |
| 2023-01 | v3.22.0 | v4.12.0 | v1.12.0 | v2.6.0 | v3.23.0 |
| — | — | v4.11.0 | — | v2.5.0 | v3.22.0 |
| 2022-10 | v3.20.0 | v4.10.0 | v1.10.0 | v2.4.0 | v3.21.0 |
| — | — | v4.9.0 | — | — | v3.20.0 |
| — | v3.19.0 | v4.8.1 | — | v2.3.0 | — |
| 2022-07.1 | v3.18.1 | v4.8.1 | v1.8.0 | v2.2.0 | v3.19.1 |
| 2022-07 | v3.18.0 | v4.8.0 | v1.8.0 | v2.2.0 | v3.19.0 |
| — | v3.17.0 | v4.7.0 | — | v2.1.0 | v3.18.0 |
| 2022-04 | v3.16.0 | v4.6.0 | v1.6.0 | v2.0.0 | v3.17.0 |
| — | v3.15.0 | v4.5.0 | v1.5.0 | — | v3.16.0 |
| — | v3.15.0 | v4.4.0 | v1.4.0 | — | v3.15.0 |
| — | v3.14.0 | v4.4.0 | v1.4.0 | v1.5 | v3.15.0 |
| 2022-01 | v3.14.0 | v4.3.0 | — | v1.4 | v3.14.0 |

## Quick Lookup

### Latest (2025-10 release)
- **HailoRT**: v4.23.0
- **TAPPAS**: v5.1.0
- **Model Zoo**: v2.17.0
- **Dataflow Compiler**: v3.33.0

### Current Local Setup Reference
For matching the locally installed HailoRT version, find the row in the table above. Components in the same row are guaranteed to be compatible.

Example: If `hailort 4.23.0` is installed, then:
- TAPPAS must be **v5.1.0**
- Model Zoo must be **v2.17.0**
- Dataflow Compiler must be **v3.33.0**

## Notes

- **Empty cells (—)** mean that component was not released as part of that AI SW Suite version.
- **HailoRT column** covers both the runtime library and the PCIe driver — they must always match.
- **Integration Tool** was discontinued after v1.20.0 (2025-01 release).
- For Hailo-10H (GenAI accelerator), refer to a separate compatibility chart.
- Always prefer installing via the **AI SW Suite** or **VPSP** to avoid version mismatch issues.