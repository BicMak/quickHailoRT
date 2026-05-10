import re
import glob
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.dates import DateFormatter
import matplotlib.ticker as ticker
from datetime import datetime

LOG_FILE = "logs/2026-05-09_212316.csv"
OUTPUT   = "logs/analysis_212316.png"

latency_records = []
temp_records = []

for path in [LOG_FILE]:
    with open(path) as f:
        for line in f:
            line = line.strip()

            # latency: "infer done: 23.95 ms"
            m = re.search(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+).*infer done: ([\d.]+) ms", line)
            if m:
                ts = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")
                latency_records.append({"timestamp": ts, "latency_ms": float(m.group(2))})
                continue

            # temperature: "temperature: ts0=56.8 C ts1=57.1 C"
            m = re.search(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+).*temperature: ts0=([\d.]+) C ts1=([\d.]+) C", line)
            if m:
                ts = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")
                temp_records.append({
                    "timestamp": ts,
                    "ts0": float(m.group(2)),
                    "ts1": float(m.group(3)),
                })

lat_df = pd.DataFrame(latency_records).sort_values("timestamp").reset_index(drop=True)
tmp_df = pd.DataFrame(temp_records).sort_values("timestamp").reset_index(drop=True)

# Separate warmup (first inference per session is always slow ~24ms)
warmup_mask = lat_df["latency_ms"] > 15
steady_lat = lat_df[~warmup_mask]["latency_ms"]
warmup_lat = lat_df[warmup_mask]["latency_ms"]

print(f"=== Latency ({len(lat_df)} samples) ===")
print(f"  All    — mean={lat_df['latency_ms'].mean():.2f}  median={lat_df['latency_ms'].median():.2f}  p95={lat_df['latency_ms'].quantile(.95):.2f}  p99={lat_df['latency_ms'].quantile(.99):.2f}  max={lat_df['latency_ms'].max():.2f} ms")
print(f"  Steady — mean={steady_lat.mean():.2f}  median={steady_lat.median():.2f}  p95={steady_lat.quantile(.95):.2f}  p99={steady_lat.quantile(.99):.2f}  max={steady_lat.max():.2f} ms")
print(f"  Warmup — count={len(warmup_lat)}  mean={warmup_lat.mean():.2f} ms")

print(f"\n=== Temperature ({len(tmp_df)} samples) ===")
print(f"  ts0 — mean={tmp_df['ts0'].mean():.1f}  min={tmp_df['ts0'].min():.1f}  max={tmp_df['ts0'].max():.1f} °C")
print(f"  ts1 — mean={tmp_df['ts1'].mean():.1f}  min={tmp_df['ts1'].min():.1f}  max={tmp_df['ts1'].max():.1f} °C")

# ── Figure ──────────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(16, 14), facecolor="#0f1117")
gs = gridspec.GridSpec(3, 2, figure=fig, hspace=0.45, wspace=0.35,
                       left=0.07, right=0.97, top=0.93, bottom=0.07)

ACCENT = "#00d4ff"
WARM   = "#ff6b6b"
GREEN  = "#69db7c"
YELLOW = "#ffd43b"
TEXT   = "#e0e0e0"
GRID   = "#2a2a3a"

def style_ax(ax, title):
    ax.set_facecolor("#1a1b26")
    ax.tick_params(colors=TEXT, labelsize=9)
    ax.xaxis.label.set_color(TEXT)
    ax.yaxis.label.set_color(TEXT)
    ax.title.set_color(TEXT)
    ax.set_title(title, fontsize=11, fontweight="bold", pad=8)
    for spine in ax.spines.values():
        spine.set_edgecolor("#333355")
    ax.grid(color=GRID, linewidth=0.5)

# 1. Latency over time (scatter)
ax1 = fig.add_subplot(gs[0, :])
ax1.scatter(lat_df[~warmup_mask]["timestamp"], lat_df[~warmup_mask]["latency_ms"],
            s=6, alpha=0.5, color=ACCENT, label="Steady-state")
ax1.scatter(lat_df[warmup_mask]["timestamp"], lat_df[warmup_mask]["latency_ms"],
            s=25, alpha=0.8, color=WARM, marker="^", label="Warmup (1st infer)")
# rolling mean on steady
roll = lat_df[~warmup_mask].set_index("timestamp")["latency_ms"].rolling("30s").mean()
ax1.plot(roll.index, roll.values, color=GREEN, linewidth=1.4, label="30 s rolling mean")
ax1.xaxis.set_major_formatter(DateFormatter("%H:%M"))
ax1.set_xlabel("Time")
ax1.set_ylabel("Latency (ms)")
ax1.legend(facecolor="#1a1b26", edgecolor="#333355", labelcolor=TEXT, fontsize=9)
style_ax(ax1, "Inference Latency Over Time")

# 2. Latency histogram (steady-state only)
ax2 = fig.add_subplot(gs[1, 0])
ax2.hist(steady_lat, bins=60, color=ACCENT, alpha=0.85, edgecolor="none")
for q, lbl, col in [(0.50, "p50", GREEN), (0.95, "p95", YELLOW), (0.99, "p99", WARM)]:
    v = steady_lat.quantile(q)
    ax2.axvline(v, color=col, linewidth=1.5, linestyle="--", label=f"{lbl}={v:.2f} ms")
ax2.set_xlabel("Latency (ms)")
ax2.set_ylabel("Count")
ax2.legend(facecolor="#1a1b26", edgecolor="#333355", labelcolor=TEXT, fontsize=9)
style_ax(ax2, "Latency Distribution (Steady-state)")

# 3. Temperature over time
ax3 = fig.add_subplot(gs[1, 1])
ax3.plot(tmp_df["timestamp"], tmp_df["ts0"], color=WARM,    linewidth=1.2, label="TS0")
ax3.plot(tmp_df["timestamp"], tmp_df["ts1"], color=YELLOW,  linewidth=1.2, label="TS1", alpha=0.8)
ax3.axhline(70, color="#ff4444", linewidth=1, linestyle=":", label="70 °C warn")
ax3.xaxis.set_major_formatter(DateFormatter("%H:%M"))
ax3.set_xlabel("Time")
ax3.set_ylabel("Temperature (°C)")
ax3.legend(facecolor="#1a1b26", edgecolor="#333355", labelcolor=TEXT, fontsize=9)
style_ax(ax3, "Hailo Chip Temperature Over Time")

# 4. Box plot — 250개씩 4 섹션으로 분할 (warmup 제외)
ax4 = fig.add_subplot(gs[2, 0])
steady_df = lat_df[~warmup_mask].reset_index(drop=True)
chunk = 250
sections = [steady_df.iloc[i*chunk:(i+1)*chunk]["latency_ms"].values for i in range(4)]
sec_labels = [f"Section {i+1}\n({i*chunk+1}–{(i+1)*chunk})" for i in range(4)]
bp = ax4.boxplot(sections, tick_labels=sec_labels, patch_artist=True,
                 medianprops=dict(color=GREEN, linewidth=2),
                 flierprops=dict(marker=".", color=WARM, markersize=4, alpha=0.6),
                 whiskerprops=dict(color=TEXT), capprops=dict(color=TEXT),
                 boxprops=dict(facecolor="#2a3a5a", edgecolor=ACCENT))
for i, (sec, box) in enumerate(zip(sections, bp["boxes"])):
    ax4.text(i+1, np.median(sec) + 0.05, f"med={np.median(sec):.2f}",
             ha="center", va="bottom", color=GREEN, fontsize=8)
ax4.set_xlabel("Section (250 inferences each)")
ax4.set_ylabel("Latency (ms)")
ax4.tick_params(axis='x', labelsize=8)
style_ax(ax4, "Latency per Section (250 infer each)")

# 5. Latency vs Temperature scatter
ax5 = fig.add_subplot(gs[2, 1])
# align by nearest timestamp
if len(tmp_df) > 0 and len(lat_df) > 0:
    merged = pd.merge_asof(
        lat_df[~warmup_mask][["timestamp","latency_ms"]].sort_values("timestamp"),
        tmp_df[["timestamp","ts0"]].sort_values("timestamp"),
        on="timestamp", tolerance=pd.Timedelta("5s"), direction="nearest"
    ).dropna()
    if len(merged) > 10:
        ax5.scatter(merged["ts0"], merged["latency_ms"], s=8, alpha=0.35, color=ACCENT)
        # trend line
        z = np.polyfit(merged["ts0"], merged["latency_ms"], 1)
        p = np.poly1d(z)
        xs = np.linspace(merged["ts0"].min(), merged["ts0"].max(), 100)
        ax5.plot(xs, p(xs), color=WARM, linewidth=2, label=f"Trend (slope={z[0]:.3f})")
        corr = merged["ts0"].corr(merged["latency_ms"])
        ax5.text(0.05, 0.92, f"r = {corr:.3f}", transform=ax5.transAxes,
                 color=YELLOW, fontsize=10, fontweight="bold")
        ax5.legend(facecolor="#1a1b26", edgecolor="#333355", labelcolor=TEXT, fontsize=9)
ax5.set_xlabel("TS0 Temperature (°C)")
ax5.set_ylabel("Latency (ms)")
style_ax(ax5, "Latency vs Temperature Correlation")

fig.suptitle("HailoRT Inference Analysis — 2026-05-09_212316", fontsize=14,
             fontweight="bold", color=TEXT, y=0.97)

plt.savefig(OUTPUT, dpi=150, bbox_inches="tight", facecolor=fig.get_facecolor())
print(f"\nSaved → {OUTPUT}")
