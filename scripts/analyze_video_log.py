#!/usr/bin/env python3
"""
Analyze a SAHI_object_detection_video CSV log and produce a single dashboard PNG
matching the HailoRT analysis style (dark theme, 5-panel grid).

Usage:
  python3 analyze_video_log.py [path/to/log.csv] [--out-dir report/]
"""

import argparse
import csv
import re
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOG_DIR = REPO_ROOT / "local_data" / "output" / "sahi_object_detection" / "logs"

FRAME_RE = re.compile(
    r"frame=(?P<frame>\d+)/(?P<total>\d+)\s+"
    r"slices=(?P<slices>\d+)\s+"
    r"slice=(?P<slice_ms>[\d.]+)\s+"
    r"prep=(?P<prep_ms>[\d.]+)\s+"
    r"infer=(?P<infer_ms>[\d.]+)\s+"
    r"post=(?P<post_ms>[\d.]+)\s+"
    r"total=(?P<total_ms>[\d.]+)\s+ms\s+"
    r"ema=(?P<ema_ms>[\d.]+)\s+ms\s+"
    r"\((?P<fps>[\d.]+)\s+FPS\)\s+"
    r"det=(?P<det_before>\d+)->(?P<det_after>\d+)"
)
TEMP_RE = re.compile(r"ts0=(?P<ts0>[\d.]+)\s+C\s+ts1=(?P<ts1>[\d.]+)\s+C")

# Style constants matching the reference image
BG       = "#0e1117"
PANEL    = "#161b22"
GRID     = "#30363d"
FG       = "#e6edf3"
MUTED    = "#8b949e"
CYAN     = "#38e1ff"
RED      = "#ff4d4f"
ORANGE   = "#ffa657"
YELLOW   = "#f1e05a"
GREEN    = "#7ee787"


def latest_log() -> Path:
    files = sorted(DEFAULT_LOG_DIR.glob("*.csv"), key=lambda p: p.stat().st_mtime)
    if not files:
        sys.exit(f"No CSV logs found in {DEFAULT_LOG_DIR}")
    return files[-1]


def parse_log(path: Path):
    frames = []
    temps = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            msg = row.get("message", "")
            ts_str = row.get("timestamp", "")
            try:
                ts = datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")
            except ValueError:
                continue
            m = FRAME_RE.search(msg)
            if m and "sahi-vid" in msg:
                g = m.groupdict()
                frames.append({
                    "timestamp": ts,
                    "frame":      int(g["frame"]),
                    "slices":     int(g["slices"]),
                    "slice_ms":   float(g["slice_ms"]),
                    "prep_ms":    float(g["prep_ms"]),
                    "infer_ms":   float(g["infer_ms"]),
                    "post_ms":    float(g["post_ms"]),
                    "total_ms":   float(g["total_ms"]),
                    "ema_ms":     float(g["ema_ms"]),
                    "fps":        float(g["fps"]),
                    "det_before": int(g["det_before"]),
                    "det_after":  int(g["det_after"]),
                })
                continue
            tm = TEMP_RE.search(msg)
            if tm:
                temps.append((ts, float(tm["ts0"]), float(tm["ts1"])))
    if not frames:
        sys.exit(f"No frame lines parsed from {path}")
    df = pd.DataFrame(frames)
    df_temp = pd.DataFrame(temps, columns=["timestamp", "ts0", "ts1"]) if temps else pd.DataFrame()
    return df, df_temp


def robust_ylim(values, lo_pct=1.0, hi_pct=99.0, pad=0.05):
    """Compute a y-limit that ignores extreme outliers so the bulk is visible.

    Uses percentile clipping (default 1st–99th), padded slightly.
    Returns (low, high)."""
    v = np.asarray(values, dtype=float)
    v = v[np.isfinite(v)]
    if v.size == 0:
        return (0.0, 1.0)
    lo = np.percentile(v, lo_pct)
    hi = np.percentile(v, hi_pct)
    span = max(hi - lo, 1e-6)
    return (lo - span * pad, hi + span * pad)


def style_axes(ax):
    ax.set_facecolor(PANEL)
    for spine in ax.spines.values():
        spine.set_color(GRID)
    ax.tick_params(colors=MUTED, labelsize=8)
    ax.xaxis.label.set_color(MUTED)
    ax.yaxis.label.set_color(MUTED)
    ax.title.set_color(FG)
    ax.grid(True, color=GRID, linestyle="-", linewidth=0.5, alpha=0.6)
    ax.set_axisbelow(True)


def make_dashboard(df: pd.DataFrame, df_temp: pd.DataFrame, log_name: str, out_path: Path):
    plt.rcParams.update({
        "figure.facecolor": BG,
        "axes.facecolor":   PANEL,
        "savefig.facecolor": BG,
        "text.color":       FG,
        "font.size":        9,
    })

    fig = plt.figure(figsize=(14, 10), dpi=130)
    gs = fig.add_gridspec(3, 2, height_ratios=[1.0, 1.0, 1.0], hspace=0.55, wspace=0.25)

    fig.suptitle(f"SAHI Object Detection Video Analysis — {log_name}",
                 color=FG, fontsize=13, fontweight="bold", y=0.98)

    # Identify warmup (first frame typically slower)
    warmup_mask = df["frame"] == df["frame"].min()
    steady = df[~warmup_mask]
    warm   = df[warmup_mask]

    # ===== Panel 1: Latency over time (full-width top) =====
    ax1 = fig.add_subplot(gs[0, :])
    style_axes(ax1)
    ax1.set_title("Per-frame Total Latency Over Time", fontsize=10, pad=6)
    ax1.scatter(steady["timestamp"], steady["total_ms"], s=10, color=CYAN, alpha=0.85,
                label="Steady-state", edgecolors="none")
    if len(warm):
        ax1.scatter(warm["timestamp"], warm["total_ms"], s=60, color=RED, marker="^",
                    label="Warmup (1st infer)", zorder=5)
    # Rolling mean
    if len(steady) >= 5:
        window = min(30, max(5, len(steady) // 4))
        rolling = steady["total_ms"].rolling(window, min_periods=1).mean()
        ax1.plot(steady["timestamp"], rolling, color=RED, linewidth=1.2,
                 label=f"{window}× rolling mean")
    ax1.set_ylabel("Latency (ms)")
    ax1.set_xlabel("Time")
    ax1.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    # Clip y-axis to ignore outliers so the bulk of frames is readable.
    ax1.set_ylim(robust_ylim(steady["total_ms"], lo_pct=0.5, hi_pct=99.0, pad=0.10))
    leg = ax1.legend(facecolor=PANEL, edgecolor=GRID, labelcolor=FG, fontsize=8, loc="upper right")
    for t in leg.get_texts():
        t.set_color(FG)

    # ===== Panel 2: Latency distribution (steady-state) =====
    ax2 = fig.add_subplot(gs[1, 0])
    style_axes(ax2)
    ax2.set_title("Latency Distribution (Steady-state)", fontsize=10, pad=6)
    data_all = steady["total_ms"].to_numpy() if len(steady) else df["total_ms"].to_numpy()
    # Drop top 1% so a few jitter spikes don't crush the bin resolution.
    hi_clip = np.percentile(data_all, 99)
    data = data_all[data_all <= hi_clip]
    ax2.hist(data, bins=30, color=CYAN, edgecolor=BG, linewidth=0.5)
    p50, p95, p99 = np.percentile(data_all, [50, 95, 99])
    ax2.axvline(p50, color=YELLOW, linestyle="--", linewidth=1.2, label=f"p50 = {p50:.2f} ms")
    ax2.axvline(p95, color=ORANGE, linestyle="--", linewidth=1.2, label=f"p95 = {p95:.2f} ms")
    ax2.axvline(p99, color=RED,    linestyle="--", linewidth=1.2, label=f"p99 = {p99:.2f} ms")
    ax2.set_xlabel("Latency (ms)")
    ax2.set_ylabel("Count")
    leg = ax2.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="upper right")
    for t in leg.get_texts():
        t.set_color(FG)

    # ===== Panel 3: Chip temperature =====
    ax3 = fig.add_subplot(gs[1, 1])
    style_axes(ax3)
    ax3.set_title("Hailo Chip Temperature Over Time", fontsize=10, pad=6)
    if not df_temp.empty:
        ax3.plot(df_temp["timestamp"], df_temp["ts0"], color=RED,    linewidth=1.2, label="TS0")
        ax3.plot(df_temp["timestamp"], df_temp["ts1"], color=ORANGE, linewidth=1.2, label="TS1")
        ax3.axhline(70, color=RED, linestyle="--", linewidth=0.8, alpha=0.7, label="70 °C warn")
        ax3.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
        leg = ax3.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="lower right")
        for t in leg.get_texts():
            t.set_color(FG)
    else:
        ax3.text(0.5, 0.5, "no temperature samples", ha="center", va="center",
                 color=MUTED, transform=ax3.transAxes)
    ax3.set_xlabel("Time")
    ax3.set_ylabel("Temperature (°C)")

    # ===== Panel 4: Latency per Section =====
    ax4 = fig.add_subplot(gs[2, 0])
    style_axes(ax4)
    n_sections = 4
    n = len(df)
    section_size = max(1, n // n_sections)
    ax4.set_title(f"Latency per Section ({section_size} frames each)", fontsize=10, pad=6)
    sections = []
    labels = []
    for i in range(n_sections):
        lo = i * section_size
        hi = (i + 1) * section_size if i < n_sections - 1 else n
        if lo >= n: break
        sec = df["total_ms"].iloc[lo:hi].to_numpy()
        sections.append(sec)
        labels.append(f"Section {i+1}\n({lo+1}–{hi})")
    bp = ax4.boxplot(sections, labels=labels, patch_artist=True, widths=0.55,
                     medianprops={"color": YELLOW, "linewidth": 1.4},
                     whiskerprops={"color": MUTED},
                     capprops={"color": MUTED},
                     flierprops={"marker": "o", "markerfacecolor": CYAN,
                                 "markeredgecolor": "none", "markersize": 4, "alpha": 0.6})
    for patch in bp["boxes"]:
        patch.set_facecolor(CYAN); patch.set_alpha(0.35); patch.set_edgecolor(CYAN)
    ax4.set_ylabel("Latency (ms)")
    # Tight y-range so section differences (e.g. throttling drift) are visible.
    if sections:
        all_sec = np.concatenate(sections)
        ax4.set_ylim(robust_ylim(all_sec, lo_pct=1.0, hi_pct=99.0, pad=0.15))

    # ===== Panel 5: Latency vs Temperature =====
    ax5 = fig.add_subplot(gs[2, 1])
    style_axes(ax5)
    ax5.set_title("Latency vs Temperature Correlation", fontsize=10, pad=6)
    if not df_temp.empty and len(df_temp) > 1:
        # nearest-time temperature for each frame
        df_sorted   = df.sort_values("timestamp")
        temp_sorted = df_temp.sort_values("timestamp")
        merged = pd.merge_asof(df_sorted, temp_sorted, on="timestamp", direction="nearest")
        x = merged["ts0"].to_numpy()
        y = merged["total_ms"].to_numpy()
        ax5.scatter(x, y, s=12, color=CYAN, alpha=0.7, edgecolors="none")
        if len(np.unique(x)) > 1:
            # Fit on outlier-trimmed data so a few spikes don't bend the trend.
            y_lo, y_hi = np.percentile(y, [1, 99])
            mask = (y >= y_lo) & (y <= y_hi)
            slope, intercept = np.polyfit(x[mask], y[mask], 1)
            xs = np.linspace(x.min(), x.max(), 50)
            ax5.plot(xs, slope * xs + intercept, color=RED, linewidth=1.4,
                     label=f"Trend (slope={slope:+.3f})")
            r = np.corrcoef(x[mask], y[mask])[0, 1]
            ax5.text(0.02, 0.96, f"r = {r:+.3f}",
                     transform=ax5.transAxes, va="top", color=RED,
                     fontsize=10, fontweight="bold")
            leg = ax5.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="upper right")
            for t in leg.get_texts():
                t.set_color(FG)
        ax5.set_xlabel("TS0 Temperature (°C)")
        ax5.set_ylim(robust_ylim(y, lo_pct=1.0, hi_pct=99.0, pad=0.15))
    else:
        ax5.text(0.5, 0.5, "no temperature samples", ha="center", va="center",
                 color=MUTED, transform=ax5.transAxes)
        ax5.set_xlabel("Temperature (°C)")
    ax5.set_ylabel("Latency (ms)")

    fig.savefig(out_path, dpi=130, bbox_inches="tight", facecolor=BG)
    plt.close(fig)


def print_summary(df, df_temp, path):
    print(f"\n=== {path.name} ===")
    print(f"frames: {len(df)}   slices/frame: {df['slices'].iloc[0]}")
    s = df["total_ms"]
    print(f"total ms  mean={s.mean():.2f}  p50={s.median():.2f}  "
          f"p95={s.quantile(0.95):.2f}  p99={s.quantile(0.99):.2f}  max={s.max():.2f}")
    print(f"mean FPS = {1000.0 / s.mean():.2f}")
    if not df_temp.empty:
        print(f"TS0 temp  min={df_temp['ts0'].min():.1f}  max={df_temp['ts0'].max():.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?", help="CSV log path (default: latest)")
    ap.add_argument("--out-dir", default=None,
                    help="output dir (default: report/<log_stem>/)")
    args = ap.parse_args()

    log_path = Path(args.log) if args.log else latest_log()
    if not log_path.exists():
        sys.exit(f"Log not found: {log_path}")

    out_dir = Path(args.out_dir) if args.out_dir else (REPO_ROOT / "report" / log_path.stem)
    out_dir.mkdir(parents=True, exist_ok=True)

    df, df_temp = parse_log(log_path)
    print_summary(df, df_temp, log_path)

    out_png = out_dir / f"dashboard_{log_path.stem}.png"
    make_dashboard(df, df_temp, log_path.stem, out_png)

    df.to_csv(out_dir / "frames.csv", index=False)
    if not df_temp.empty:
        df_temp.to_csv(out_dir / "temperature.csv", index=False)

    print(f"\nDashboard: {out_png}")


if __name__ == "__main__":
    main()
