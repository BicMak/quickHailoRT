#!/usr/bin/env python3
"""
Task-agnostic log analyzer.

Usage:
  python3 analyze_log.py <task> [path/to/log.csv]

  <task>: sahi_object_detection | classification | zero_shot_classification
          (혹은 config.yaml 의 task 키와 같은 이름)
  [csv] : 생략 시 local_data/output/<task>/logs/ 의 최신 파일을 사용

다음 4개 패널을 그립니다 (task 공통):
  1. per-iteration latency 시계열
  2. latency 분포 (p50/p95/p99)
  3. Hailo 칩 온도 시계열
  4. latency vs 온도 상관

레포트는 local_data/output/<task>/reports/<csv_stem>/ 아래에 저장됩니다.
"""

import argparse
import csv
import re
import sys
from datetime import datetime
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

REPO_ROOT = Path(__file__).resolve().parent.parent

# task → log line의 total=NNN(ms 포함/생략 무관) 또는 (NNN.NN%) 같은 정규식.
# 각 정규식은 named group "total_ms" 를 반드시 가져야 합니다.
TASK_PATTERNS = {
    "sahi_object_detection": re.compile(
        r"total=(?P<total_ms>[\d.]+)\s*ms"
    ),
    "classification": re.compile(
        r"\bresult:\s+\S+\s+->\s+.*?\((?P<conf>[\d.]+)%\)"  # latency 없음
    ),
    "zero_shot_classification": re.compile(
        r"\S+\s+->\s+.*?\((?P<conf>[\d.]+)%\)"
    ),
}

TEMP_RE = re.compile(r"ts0=(?P<ts0>[\d.]+)\s+C\s+ts1=(?P<ts1>[\d.]+)\s+C")

# Style constants (analyze_video_log.py 와 동일 dark theme)
BG, PANEL, GRID = "#0e1117", "#161b22", "#30363d"
FG, MUTED = "#e6edf3", "#8b949e"
CYAN, RED, ORANGE, YELLOW = "#38e1ff", "#ff4d4f", "#ffa657", "#f1e05a"


def latest_log(task: str) -> Path:
    log_dir = REPO_ROOT / "local_data" / "output" / task / "logs"
    files = sorted(log_dir.glob("*.csv"), key=lambda p: p.stat().st_mtime)
    if not files:
        sys.exit(f"No CSV logs found in {log_dir}")
    return files[-1]


def parse_log(path: Path, task: str):
    """CSV 한 줄씩 읽어서
       - per-iteration latency (total=.. ms 가 있는 줄만)
       - 온도 샘플
       을 DataFrame 두 개로 반환."""
    pattern = TASK_PATTERNS.get(task)
    if pattern is None:
        sys.exit(f"Unknown task '{task}'. Known: {list(TASK_PATTERNS)}")

    rows, temps = [], []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            msg = row.get("message", "")
            ts_str = row.get("timestamp", "")
            try:
                ts = datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")
            except ValueError:
                continue

            m = pattern.search(msg)
            if m and "total_ms" in m.groupdict():
                rows.append({"timestamp": ts, "total_ms": float(m["total_ms"])})

            tm = TEMP_RE.search(msg)
            if tm:
                temps.append((ts, float(tm["ts0"]), float(tm["ts1"])))

    df = pd.DataFrame(rows)
    df_temp = pd.DataFrame(temps, columns=["timestamp", "ts0", "ts1"]) if temps else pd.DataFrame()
    return df, df_temp


def robust_ylim(values, lo_pct=1.0, hi_pct=99.0, pad=0.05):
    v = np.asarray(values, dtype=float)
    v = v[np.isfinite(v)]
    if v.size == 0:
        return (0.0, 1.0)
    lo, hi = np.percentile(v, lo_pct), np.percentile(v, hi_pct)
    span = max(hi - lo, 1e-6)
    return (lo - span * pad, hi + span * pad)


def style_axes(ax):
    ax.set_facecolor(PANEL)
    for s in ax.spines.values():
        s.set_color(GRID)
    ax.tick_params(colors=MUTED, labelsize=8)
    ax.xaxis.label.set_color(MUTED)
    ax.yaxis.label.set_color(MUTED)
    ax.title.set_color(FG)
    ax.grid(True, color=GRID, linestyle="-", linewidth=0.5, alpha=0.6)
    ax.set_axisbelow(True)


def panel_latency_over_time(ax, df):
    style_axes(ax)
    ax.set_title("Per-iteration Latency Over Time", fontsize=10, pad=6)
    warm_mask = df.index == df.index.min()
    warm, steady = df[warm_mask], df[~warm_mask]
    ax.scatter(steady["timestamp"], steady["total_ms"], s=10, color=CYAN,
               alpha=0.85, edgecolors="none", label="Steady-state")
    if len(warm):
        ax.scatter(warm["timestamp"], warm["total_ms"], s=60, color=RED,
                   marker="^", zorder=5, label="Warmup")
    if len(steady) >= 5:
        w = min(30, max(5, len(steady) // 4))
        ax.plot(steady["timestamp"],
                steady["total_ms"].rolling(w, min_periods=1).mean(),
                color=RED, linewidth=1.2, label=f"{w}× rolling mean")
    ax.set_xlabel("Time"); ax.set_ylabel("Latency (ms)")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    ax.set_ylim(robust_ylim(steady["total_ms"], 0.5, 99.0, 0.10))
    leg = ax.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="upper right")
    for t in leg.get_texts():
        t.set_color(FG)


def panel_latency_dist(ax, df):
    style_axes(ax)
    ax.set_title("Latency Distribution", fontsize=10, pad=6)
    data = df["total_ms"].to_numpy()
    hi = np.percentile(data, 99)
    ax.hist(data[data <= hi], bins=30, color=CYAN, edgecolor=BG, linewidth=0.5)
    p50, p95, p99 = np.percentile(data, [50, 95, 99])
    for v, c, lbl in [(p50, YELLOW, "p50"), (p95, ORANGE, "p95"), (p99, RED, "p99")]:
        ax.axvline(v, color=c, linestyle="--", linewidth=1.2, label=f"{lbl} = {v:.2f} ms")
    ax.set_xlabel("Latency (ms)"); ax.set_ylabel("Count")
    leg = ax.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="upper right")
    for t in leg.get_texts():
        t.set_color(FG)


def panel_temperature(ax, df_temp):
    style_axes(ax)
    ax.set_title("Hailo Chip Temperature Over Time", fontsize=10, pad=6)
    if df_temp.empty:
        ax.text(0.5, 0.5, "no temperature samples", ha="center", va="center",
                color=MUTED, transform=ax.transAxes)
    else:
        ax.plot(df_temp["timestamp"], df_temp["ts0"], color=RED, linewidth=1.2, label="TS0")
        ax.plot(df_temp["timestamp"], df_temp["ts1"], color=ORANGE, linewidth=1.2, label="TS1")
        ax.axhline(70, color=RED, linestyle="--", linewidth=0.8, alpha=0.7, label="70 °C warn")
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
        leg = ax.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="lower right")
        for t in leg.get_texts():
            t.set_color(FG)
    ax.set_xlabel("Time"); ax.set_ylabel("Temperature (°C)")


def panel_corr(ax, df, df_temp):
    style_axes(ax)
    ax.set_title("Latency vs Temperature Correlation", fontsize=10, pad=6)
    if df_temp.empty or len(df_temp) < 2:
        ax.text(0.5, 0.5, "no temperature samples", ha="center", va="center",
                color=MUTED, transform=ax.transAxes)
        ax.set_xlabel("Temperature (°C)"); ax.set_ylabel("Latency (ms)")
        return
    merged = pd.merge_asof(df.sort_values("timestamp"),
                           df_temp.sort_values("timestamp"),
                           on="timestamp", direction="nearest")
    x, y = merged["ts0"].to_numpy(), merged["total_ms"].to_numpy()
    ax.scatter(x, y, s=12, color=CYAN, alpha=0.7, edgecolors="none")
    if len(np.unique(x)) > 1:
        lo, hi = np.percentile(y, [1, 99])
        mask = (y >= lo) & (y <= hi)
        slope, intercept = np.polyfit(x[mask], y[mask], 1)
        xs = np.linspace(x.min(), x.max(), 50)
        ax.plot(xs, slope * xs + intercept, color=RED, linewidth=1.4,
                label=f"Trend (slope={slope:+.3f})")
        r = np.corrcoef(x[mask], y[mask])[0, 1]
        ax.text(0.02, 0.96, f"r = {r:+.3f}", transform=ax.transAxes, va="top",
                color=RED, fontsize=10, fontweight="bold")
        leg = ax.legend(facecolor=PANEL, edgecolor=GRID, fontsize=8, loc="upper right")
        for t in leg.get_texts():
            t.set_color(FG)
        ax.set_ylim(robust_ylim(y, 1.0, 99.0, 0.15))
    ax.set_xlabel("TS0 Temperature (°C)"); ax.set_ylabel("Latency (ms)")


def make_dashboard(df, df_temp, title, out_path: Path):
    plt.rcParams.update({
        "figure.facecolor": BG, "axes.facecolor": PANEL,
        "savefig.facecolor": BG, "text.color": FG, "font.size": 9,
    })
    fig = plt.figure(figsize=(14, 9), dpi=130)
    gs = fig.add_gridspec(2, 2, hspace=0.45, wspace=0.25)
    fig.suptitle(title, color=FG, fontsize=13, fontweight="bold", y=0.98)

    panel_latency_over_time(fig.add_subplot(gs[0, :]), df)
    panel_latency_dist     (fig.add_subplot(gs[1, 0]), df)
    panel_temperature      (fig.add_subplot(gs[1, 1]), df_temp)
    # correlation panel은 latency가 task에 있을 때만 의미있음 — 일단 자리 잡지 않음
    fig.savefig(out_path, dpi=130, bbox_inches="tight", facecolor=BG)
    plt.close(fig)


def make_corr_dashboard(df, df_temp, title, out_path: Path):
    plt.rcParams.update({
        "figure.facecolor": BG, "axes.facecolor": PANEL,
        "savefig.facecolor": BG, "text.color": FG, "font.size": 9,
    })
    fig = plt.figure(figsize=(7, 5), dpi=130)
    ax = fig.add_subplot(1, 1, 1)
    fig.suptitle(title, color=FG, fontsize=12, fontweight="bold")
    panel_corr(ax, df, df_temp)
    fig.savefig(out_path, dpi=130, bbox_inches="tight", facecolor=BG)
    plt.close(fig)


def print_summary(df, df_temp, path):
    print(f"\n=== {path.name} ===")
    print(f"samples: {len(df)}")
    if "total_ms" in df.columns and len(df):
        s = df["total_ms"]
        print(f"total_ms  mean={s.mean():.2f}  p50={s.median():.2f}  "
              f"p95={s.quantile(0.95):.2f}  p99={s.quantile(0.99):.2f}  max={s.max():.2f}")
        print(f"mean rate = {1000.0 / s.mean():.2f} /s")
    if not df_temp.empty:
        print(f"TS0 temp  min={df_temp['ts0'].min():.1f}  max={df_temp['ts0'].max():.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("task", help=f"task name (one of: {', '.join(TASK_PATTERNS)})")
    ap.add_argument("log", nargs="?", help="CSV log path (default: latest in local_data/output/<task>/logs/)")
    args = ap.parse_args()

    log_path = Path(args.log) if args.log else latest_log(args.task)
    if not log_path.exists():
        sys.exit(f"Log not found: {log_path}")

    df, df_temp = parse_log(log_path, args.task)
    print_summary(df, df_temp, log_path)

    out_dir = REPO_ROOT / "local_data" / "output" / args.task / "reports" / log_path.stem
    out_dir.mkdir(parents=True, exist_ok=True)

    title = f"{args.task} — {log_path.stem}"

    if "total_ms" in df.columns and len(df):
        make_dashboard(df, df_temp, title, out_dir / "dashboard.png")
        if not df_temp.empty:
            make_corr_dashboard(df, df_temp, title, out_dir / "latency_vs_temp.png")
        df.to_csv(out_dir / "iterations.csv", index=False)
    else:
        print("⚠ no per-iteration latency found in this log — skipping latency panels")
        if not df_temp.empty:
            # 온도만 있을 땐 온도 패널만이라도 저장
            plt.rcParams.update({
                "figure.facecolor": BG, "axes.facecolor": PANEL,
                "savefig.facecolor": BG, "text.color": FG, "font.size": 9,
            })
            fig = plt.figure(figsize=(10, 5), dpi=130)
            panel_temperature(fig.add_subplot(1, 1, 1), df_temp)
            fig.suptitle(title, color=FG, fontsize=12, fontweight="bold")
            fig.savefig(out_dir / "temperature.png", dpi=130, bbox_inches="tight", facecolor=BG)
            plt.close(fig)

    if not df_temp.empty:
        df_temp.to_csv(out_dir / "temperature.csv", index=False)

    print(f"\nReport: {out_dir}")


if __name__ == "__main__":
    main()