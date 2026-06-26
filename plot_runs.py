# Plot 3 – Thread count (step) + λ smoothed (line)
#!/usr/bin/env python3
"""
plot_runs.py  –  Genera i plot delle run dalla pipeline C++.

Struttura CSV (una riga per item):
    send_time_ns, exit_time_ns,
    sender_to_A_s, service_time_A_s,
    A_to_B_s, service_time_B_s,
    B_to_sink_s, end_to_end_s,
    threads_A, threads_B, lambda

Uso:
    python3 plot_runs.py [--log-dir ./logs] [--out-dir ./plots] [file1.csv ...]

Se non vengono passati file CSV esplicitamente, lo script carica tutti i
run_*.csv presenti in --log-dir.
"""

import argparse
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")          # backend senza display
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
import pandas as pd


# ── colori coerenti ─────────────────────────────────────────────────────────
BP_STALL_COL    = "#D62728"
THEORETICAL_COL = "#FF7F0E"

INTER_COLOR  = "#4C72B0"
INTRA_COLOR  = "#DD8452"
THREAD_A_COL    = "#55A868"
THREAD_B_COL    = "#C44E52"
LAMBDA_COL      = "#8172B2"
LAMBDA_SMOOTH_W = 30   # finestra rolling per smorzare il rumore dell'EMA lambda
E2E_COL         = "#4C72B0"
QUEUE_A_COL     = "#2A9D8F"
QUEUE_B_COL     = "#E76F51"
E2E_SMOOTH_W    = 50   # finestra rolling per latenza e2e
THPUT_COL       = "#8B7355"
DEFAULT_BIN_S   = 1.0  # secondi per aggregazione robusta cross-run


RUN_GAP_S = 0.0  # nessun gap: tutte le run concatenate come un'unica serie


def load_all(csv_paths: list[Path]) -> pd.DataFrame:
    """Carica tutti i CSV in un unico DataFrame con asse temporale continuo."""
    frames: list[pd.DataFrame] = []
    t_offset = 0.0
    for p in csv_paths:
        df = pd.read_csv(p)
        # Scarta righe corrotte/incomplete (es. ultima riga tronca dopo stop brusco)
        # che possono causare exit_time << send_time e rompere l'asse temporale.
        valid_mask = (
            df["send_time_ns"].notna()
            & df["exit_time_ns"].notna()
            & (df["send_time_ns"] > 0)
            & (df["exit_time_ns"] >= df["send_time_ns"])
        )
        dropped = int((~valid_mask).sum())
        if dropped:
            print(f"  [warn] {p.name}: scartate {dropped} righe invalide")
        df = df.loc[valid_mask].copy()
        if df.empty:
            print(f"  [warn] {p.name}: nessuna riga valida, file ignorato")
            continue

        df["run_id"] = p.stem
        df["send_time_s"] = df["send_time_ns"] * 1e-9
        df["exit_time_s"] = df["exit_time_ns"] * 1e-9
        t0 = df["send_time_s"].min()
        df["t_send_rel"] = df["send_time_s"] - t0
        df["t_exit_rel"] = df["exit_time_s"] - t0
        df["t_global"]   = df["t_send_rel"] + t_offset
        t_offset += df["t_send_rel"].max() + RUN_GAP_S
        frames.append(df)

    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


def compute_throughput_per_run(df: pd.DataFrame, bin_s: float) -> pd.DataFrame:
    """Calcola throughput per bin per ogni run da t_exit_rel."""
    base = df[["run_id", "t_exit_rel"]].copy()
    base["t_bin"] = (base["t_exit_rel"] / bin_s).astype(int)
    per_run = (
        base.groupby(["run_id", "t_bin"], as_index=False).size()
        .rename(columns={"size": "count"})
    )
    per_run["throughput"] = per_run["count"] / bin_s
    return per_run[["run_id", "t_bin", "throughput"]]


def compute_input_rate_per_run(df: pd.DataFrame, bin_s: float) -> pd.DataFrame:
    """Calcola arrival rate effettivo per bin per ogni run da t_send_rel."""
    base = df[["run_id", "t_send_rel"]].copy()
    base["t_bin"] = (base["t_send_rel"] / bin_s).astype(int)
    per_run = (
        base.groupby(["run_id", "t_bin"], as_index=False).size()
        .rename(columns={"size": "count"})
    )
    per_run["input_rate"] = per_run["count"] / bin_s
    return per_run[["run_id", "t_bin", "input_rate"]]


def load_sinusoidal_config(config_path: str) -> dict:
    """Load per-mode sinusoidal parameters from config.yaml without external dependencies."""
    try:
        raw: dict = {}
        section = None
        with open(config_path) as f:
            for line in f:
                line = line.rstrip()
                if not line or line.lstrip().startswith("#"):
                    continue
                if not line.startswith(" "):
                    section = line.rstrip(":").strip()
                    raw[section] = {}
                elif section and ":" in line:
                    key, _, val = line.strip().partition(":")
                    val = val.split("#")[0].strip()
                    try:
                        raw[section][key.strip()] = float(val)
                    except ValueError:
                        raw[section][key.strip()] = val

        result = {}
        for mode in ("standard", "overload"):
            m = raw.get(mode, {})
            if {"base_rate_ms", "amplitude_ms", "period_s"}.issubset(m):
                result[mode] = {
                    "base_rate_ms": m["base_rate_ms"],
                    "amplitude_ms": m["amplitude_ms"],
                    "period_s":     m["period_s"],
                }
        return result
    except Exception:
        return {}


def _infer_mode(run_id: str) -> str:
    """Extract mode from run_id like 'run_standard_20250101_120000'."""
    parts = run_id.split("_")
    return parts[1] if len(parts) >= 2 else "standard"


def _theoretical_rate(t_s: np.ndarray, base_ms: float, amp_ms: float, period_s: float) -> np.ndarray:
    """Sinusoidal target arrival rate (items/s) matching the sender formula."""
    ia_ms = base_ms + amp_ms * np.sin(2.0 * np.pi * t_s / period_s)
    return 1000.0 / np.maximum(ia_ms, 1.0)


def compute_stall_proxy(df: pd.DataFrame, bin_s: float, sin_cfg: dict) -> pd.DataFrame:
    """
    Per time bin: stall_proxy = max(0, 1 - actual_rate / theoretical_rate).
    stall_proxy > 0 means the sender was throttled by back-pressure below its
    sinusoidal target. Returns p25/p50/p75 across runs plus the median theoretical rate.
    """
    in_per_run = compute_input_rate_per_run(df, bin_s)
    if in_per_run.empty or not sin_cfg:
        return pd.DataFrame(columns=["t", "p25", "p50", "p75", "theoretical_p50"])

    all_bins: list[pd.DataFrame] = []
    for run_id, grp in in_per_run.groupby("run_id"):
        mode = _infer_mode(run_id)
        params = sin_cfg.get(mode, sin_cfg.get("standard"))
        if params is None:
            continue
        grp = grp.sort_values("t_bin").copy()
        t_centers = (grp["t_bin"].values + 0.5) * bin_s
        theo = _theoretical_rate(t_centers,
                                 params["base_rate_ms"],
                                 params["amplitude_ms"],
                                 params["period_s"])
        grp["theoretical"] = theo
        grp["stall_proxy"] = (1.0 - grp["input_rate"] / grp["theoretical"]).clip(lower=0, upper=1.0)
        all_bins.append(grp[["t_bin", "input_rate", "theoretical", "stall_proxy"]])

    if not all_bins:
        return pd.DataFrame(columns=["t", "p25", "p50", "p75", "theoretical_p50"])

    combined = pd.concat(all_bins, ignore_index=True)
    q = (combined.groupby("t_bin")["stall_proxy"]
         .quantile([0.25, 0.5, 0.75])
         .unstack()
         .reset_index())
    q.columns = ["t_bin", "p25", "p50", "p75"]
    q["t"] = (q["t_bin"] + 0.5) * bin_s

    q_theo = (combined.groupby("t_bin")["theoretical"]
              .median()
              .reset_index()
              .rename(columns={"theoretical": "theoretical_p50"}))
    q_theo["t"] = (q_theo["t_bin"] + 0.5) * bin_s

    return q.merge(q_theo[["t_bin", "theoretical_p50"]], on="t_bin", how="left")


def aggregate_cross_run(df: pd.DataFrame, value_col: str, bin_s: float) -> pd.DataFrame:
    """Aggrega una metrica nel tempo in modo robusto tra run.

    1) mediana per run per bin temporale
    2) p25/p50/p75 tra run per ogni bin (run pesate uguali)
    """
    base = df[["run_id", "t_send_rel", value_col]].dropna().copy()
    if base.empty:
        return pd.DataFrame(columns=["t", "p25", "p50", "p75"])

    base["t_bin"] = (base["t_send_rel"] / bin_s).astype(int)
    per_run = (
        base.groupby(["run_id", "t_bin"], as_index=False)[value_col]
        .median()
        .rename(columns={value_col: "run_med"})
    )

    q = per_run.groupby("t_bin")["run_med"].quantile([0.25, 0.5, 0.75]).unstack()
    q.columns = ["p25", "p50", "p75"]
    q = q.reset_index()
    q["t"] = (q["t_bin"] + 0.5) * bin_s
    return q[["t", "p25", "p50", "p75"]]


# ════════════════════════════════════════════════════════════════════════════
# Plot 1 – Boxplot latenze inter-stage  (tutti i dati, una box per stage)
# ════════════════════════════════════════════════════════════════════════════
def plot_inter_stage_boxplot(df: pd.DataFrame, out_dir: Path):
    inter_cols = {
        "Sender→A": "sender_to_A_s",
        "A→B":      "A_to_B_s",
        "B→Sink":   "B_to_sink_s",
    }
    n_stages = len(inter_cols)
    fig, axes = plt.subplots(1, n_stages, figsize=(4 * n_stages, 5), sharey=False)
    if n_stages == 1:
        axes = [axes]

    for ax, (stage_name, col) in zip(axes, inter_cols.items()):
        data = [df[col].dropna().values * 1e3]   # ms, lista con un elemento
        # outlier come puntini piccoli (dati reali di accodamento sotto carico)
        bp = ax.boxplot(data, tick_labels=[stage_name], patch_artist=True,
                        medianprops=dict(color="black", linewidth=1.5),
                        flierprops=dict(marker=".", markersize=2,
                                        markerfacecolor=INTER_COLOR, alpha=0.3),
                        whis=(5, 95))  # baffi al 5°-95° percentile
        for patch in bp["boxes"]:
            patch.set_facecolor(INTER_COLOR)
            patch.set_alpha(0.7)
        ax.set_title(stage_name, fontsize=11)
        ax.set_ylabel("Latenza inter-stage (ms)" if ax is axes[0] else "")
        ax.tick_params(axis="x", labelbottom=False, bottom=False)
        ax.yaxis.set_minor_locator(mticker.AutoMinorLocator())
        ax.grid(axis="y", linestyle="--", alpha=0.5)

    fig.suptitle("Latenze inter-stage", fontsize=13, y=1.01)
    fig.tight_layout()
    out = out_dir / "01_inter_stage_boxplot.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 2 – Boxplot latenze intra-stage  (tutti i dati, una box per stage)
# ════════════════════════════════════════════════════════════════════════════
def plot_intra_stage_boxplot(df: pd.DataFrame, out_dir: Path):
    intra_cols = {
        "Service A": "service_time_A_s",
        "Service B": "service_time_B_s",
    }
    n_stages = len(intra_cols)
    fig, axes = plt.subplots(1, n_stages, figsize=(4 * n_stages, 5), sharey=False)
    if n_stages == 1:
        axes = [axes]

    for ax, (stage_name, col) in zip(axes, intra_cols.items()):
        data = [df[col].dropna().values * 1e3]   # ms
        bp = ax.boxplot(data, tick_labels=[stage_name], patch_artist=True,
                        medianprops=dict(color="black", linewidth=1.5),
                        flierprops=dict(marker=".", markersize=2,
                                        markerfacecolor=INTRA_COLOR, alpha=0.3),
                        whis=(5, 95))
        for patch in bp["boxes"]:
            patch.set_facecolor(INTRA_COLOR)
            patch.set_alpha(0.7)
        ax.set_title(stage_name, fontsize=11)
        ax.set_ylabel("Service time (ms)" if ax is axes[0] else "")
        ax.tick_params(axis="x", labelbottom=False, bottom=False)
        ax.yaxis.set_minor_locator(mticker.AutoMinorLocator())
        ax.grid(axis="y", linestyle="--", alpha=0.5)

    fig.suptitle("Latenze intra-stage", fontsize=13, y=1.01)
    fig.tight_layout()
    out = out_dir / "02_intra_stage_boxplot.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 3 – Thread count (step) + arrival rate effettivo (line)
# ════════════════════════════════════════════════════════════════════════════
def plot_threads_lambda(df: pd.DataFrame, out_dir: Path, bin_s: float):
    fig, ax1 = plt.subplots(figsize=(14, 5))
    ax2 = ax1.twinx()

    agg_a = aggregate_cross_run(df, "threads_A", bin_s)
    agg_b = aggregate_cross_run(df, "threads_B", bin_s)
    agg_in = compute_input_rate_per_run(df, bin_s)

    q_in = (
        agg_in.groupby("t_bin")["input_rate"]
        .quantile([0.25, 0.5, 0.75])
        .unstack()
    )
    q_in.columns = ["p25", "p50", "p75"]
    q_in = q_in.reset_index()
    q_in["t"] = (q_in["t_bin"] + 0.5) * bin_s

    # thread count robusto: mediana + banda IQR
    ax1.step(agg_a["t"], agg_a["p50"], where="mid",
             color=THREAD_A_COL, linewidth=1.6, label="Thread A (p50)")
    ax1.fill_between(agg_a["t"], agg_a["p25"], agg_a["p75"],
                     color=THREAD_A_COL, alpha=0.15)
    ax1.step(agg_b["t"], agg_b["p50"], where="mid",
             color=THREAD_B_COL, linewidth=1.6, label="Thread B (p50)")
    ax1.fill_between(agg_b["t"], agg_b["p25"], agg_b["p75"],
                     color=THREAD_B_COL, alpha=0.12)
    ax1.set_ylabel("Numero di repliche (thread)")
    ax1.set_xlabel("Tempo relativo nella run (s)")
    ax1.yaxis.set_major_locator(mticker.MaxNLocator(integer=True))

    # arrival rate effettivo: mediana + banda IQR
    ax2.plot(q_in["t"], q_in["p50"], color=LAMBDA_COL,
             linewidth=1.5, alpha=0.95, label="Arrival rate (p50)")
    ax2.fill_between(q_in["t"], q_in["p25"], q_in["p75"],
                     color=LAMBDA_COL, alpha=0.15)
    ax2.set_ylabel("Arrival rate effettivo (items/s)")

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=9)

    ax1.grid(axis="y", linestyle="--", alpha=0.4)
    fig.suptitle("Thread count e arrival rate effettivo (mediana + IQR tra run)", fontsize=12)
    fig.tight_layout()

    out = out_dir / "03_threads_lambda.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 4 – Latenza E2E nel tempo (stile simile al plot 3)
# ════════════════════════════════════════════════════════════════════════════
def plot_e2e_latency(df: pd.DataFrame, out_dir: Path, bin_s: float):
    fig, ax1 = plt.subplots(figsize=(14, 5))
    ax2 = ax1.twinx()

    agg_e2e = aggregate_cross_run(df, "end_to_end_s", bin_s)
    ax1.plot(agg_e2e["t"], agg_e2e["p50"], color=E2E_COL,
             linewidth=1.8, alpha=0.95, label="E2E (p50)")
    ax1.fill_between(agg_e2e["t"], agg_e2e["p25"], agg_e2e["p75"],
                     color=E2E_COL, alpha=0.16)

    # code nello stesso grafico (asse secondario) se presenti nel CSV
    if {"queue_len_A", "queue_len_B"}.issubset(df.columns):
        agg_qa = aggregate_cross_run(df, "queue_len_A", bin_s)
        agg_qb = aggregate_cross_run(df, "queue_len_B", bin_s)
        ax2.step(agg_qa["t"], agg_qa["p50"], where="mid",
                 color=QUEUE_A_COL, linewidth=1.2, alpha=0.9, label="Queue A (p50)")
        ax2.fill_between(agg_qa["t"], agg_qa["p25"], agg_qa["p75"],
                         color=QUEUE_A_COL, alpha=0.12)
        ax2.step(agg_qb["t"], agg_qb["p50"], where="mid",
                 color=QUEUE_B_COL, linewidth=1.2, alpha=0.9, label="Queue B (p50)")
        ax2.fill_between(agg_qb["t"], agg_qb["p25"], agg_qb["p75"],
                         color=QUEUE_B_COL, alpha=0.1)
        ax2.set_ylabel("Lunghezza coda stimata (items)")
        ax2.yaxis.set_major_locator(mticker.MaxNLocator(integer=True))

    ax1.set_xlabel("Tempo relativo nella run (s)")
    ax1.set_ylabel("Latenza E2E (s)")
    ax1.set_title("Latenza E2E + lunghezza code (mediana + IQR tra run)", fontsize=12)
    ax1.grid(linestyle="--", alpha=0.35)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=9)
    fig.tight_layout()

    out = out_dir / "04_e2e_latency.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 5 – Throughput nel tempo (aggregato tra run)
# ════════════════════════════════════════════════════════════════════════════
def plot_throughput(df: pd.DataFrame, out_dir: Path, bin_s: float):
    fig, ax = plt.subplots(figsize=(14, 5))

    tput_per_run = compute_throughput_per_run(df, bin_s)
    if tput_per_run.empty:
        print("  Skipping throughput plot: no data")
        return

    q = (
        tput_per_run.groupby("t_bin")["throughput"]
        .quantile([0.25, 0.5, 0.75])
        .unstack()
    )
    q.columns = ["p25", "p50", "p75"]
    q = q.reset_index()
    q["t"] = (q["t_bin"] + 0.5) * bin_s

    ax.plot(q["t"], q["p50"], color=THPUT_COL, linewidth=1.8, alpha=0.95, label="Throughput (p50)")
    ax.fill_between(q["t"], q["p25"], q["p75"],
                    color=THPUT_COL, alpha=0.15)

    ax.set_xlabel("Tempo relativo nella run (s)")
    ax.set_ylabel("Throughput in uscita (items/s)")
    ax.set_title("Throughput nel tempo (mediana + IQR tra run)", fontsize=12)
    ax.grid(linestyle="--", alpha=0.35)
    ax.legend(loc="upper right", fontsize=9)
    fig.tight_layout()

    out = out_dir / "05_throughput.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 6 – Arrival effettivo vs Throughput effettivo vs λ EMA
# ════════════════════════════════════════════════════════════════════════════
def plot_arrival_vs_throughput(df: pd.DataFrame, out_dir: Path, bin_s: float):
    fig, ax = plt.subplots(figsize=(14, 5))

    in_per_run = compute_input_rate_per_run(df, bin_s)
    out_per_run = compute_throughput_per_run(df, bin_s)

    if in_per_run.empty or out_per_run.empty:
        print("  Skipping arrival-vs-throughput plot: no data")
        return

    q_in = (
        in_per_run.groupby("t_bin")["input_rate"]
        .quantile([0.25, 0.5, 0.75])
        .unstack()
        .reset_index()
    )
    q_in.columns = ["t_bin", "p25", "p50", "p75"]
    q_in["t"] = (q_in["t_bin"] + 0.5) * bin_s

    q_out = (
        out_per_run.groupby("t_bin")["throughput"]
        .quantile([0.25, 0.5, 0.75])
        .unstack()
        .reset_index()
    )
    q_out.columns = ["t_bin", "p25", "p50", "p75"]
    q_out["t"] = (q_out["t_bin"] + 0.5) * bin_s

    in_col  = "#1F77B4"
    out_col = "#2CA02C"
    lam_col = "#9467BD"

    ax.plot(q_in["t"], q_in["p50"], color=in_col, linewidth=1.7, label="Arrival effettivo (p50)")
    ax.fill_between(q_in["t"], q_in["p25"], q_in["p75"], color=in_col, alpha=0.14)

    ax.plot(q_out["t"], q_out["p50"], color=out_col, linewidth=1.9, label="Throughput effettivo (p50)")
    ax.fill_between(q_out["t"], q_out["p25"], q_out["p75"], color=out_col, alpha=0.14)

    # λ EMA dal controller (colonna "lambda" nel CSV): mostra il lag dello stimatore
    if "lambda" in df.columns:
        agg_lam = aggregate_cross_run(df, "lambda", bin_s)
        if not agg_lam.empty:
            ax.plot(agg_lam["t"], agg_lam["p50"], color=lam_col,
                    linewidth=1.4, linestyle="--", label="λ EMA controller (p50)")
            ax.fill_between(agg_lam["t"], agg_lam["p25"], agg_lam["p75"],
                            color=lam_col, alpha=0.10)

    ax.set_xlabel("Tempo relativo nella run (s)")
    ax.set_ylabel("Rate (items/s)")
    ax.set_title("Arrival effettivo vs Throughput vs λ EMA controller", fontsize=12)
    ax.grid(linestyle="--", alpha=0.35)
    ax.legend(loc="upper right", fontsize=9)
    fig.tight_layout()

    out = out_dir / "06_arrival_vs_throughput.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 7 – Arrival rate effettivo vs tasso teorico + stall proxy back-pressure
# ════════════════════════════════════════════════════════════════════════════
def plot_bp_evidence(df: pd.DataFrame, out_dir: Path, bin_s: float, sin_cfg: dict):
    fig, ax1 = plt.subplots(figsize=(14, 5))
    ax2 = ax1.twinx()

    # ── arrival rate effettivo ───────────────────────────────────────────────
    in_per_run = compute_input_rate_per_run(df, bin_s)
    q_in = (in_per_run.groupby("t_bin")["input_rate"]
            .quantile([0.25, 0.5, 0.75])
            .unstack()
            .reset_index())
    q_in.columns = ["t_bin", "p25", "p50", "p75"]
    q_in["t"] = (q_in["t_bin"] + 0.5) * bin_s

    ax1.plot(q_in["t"], q_in["p50"], color="#1F77B4", linewidth=1.8,
             label="Arrival rate effettivo (p50)")
    ax1.fill_between(q_in["t"], q_in["p25"], q_in["p75"], color="#1F77B4", alpha=0.14)

    # ── tasso teorico sinusoidale + stall proxy ──────────────────────────────
    q_stall = compute_stall_proxy(df, bin_s, sin_cfg)

    if not q_stall.empty:
        if "theoretical_p50" in q_stall.columns:
            ax1.plot(q_stall["t"], q_stall["theoretical_p50"],
                     color=THEORETICAL_COL, linewidth=1.4, linestyle="--",
                     label="Tasso teorico sinusoidale")

        ax2.fill_between(q_stall["t"], 0, q_stall["p50"],
                         color=BP_STALL_COL, alpha=0.30, label="BP stall proxy (p50)")
        ax2.plot(q_stall["t"], q_stall["p50"],
                 color=BP_STALL_COL, linewidth=1.3)
        ax2.fill_between(q_stall["t"], q_stall["p25"], q_stall["p75"],
                         color=BP_STALL_COL, alpha=0.12)
        ax2.set_ylabel("Back-pressure stall fraction  [0 – 1]", color=BP_STALL_COL)
        ax2.tick_params(axis="y", colors=BP_STALL_COL)
        ax2.set_ylim(0, 1.05)
    else:
        print("  [warn] plot 07: nessun parametro sinusoidale trovato, "
              "stall proxy omesso (controlla --config)")

    ax1.set_xlabel("Tempo relativo nella run (s)")
    ax1.set_ylabel("Arrival rate (items/s)")
    ax1.grid(linestyle="--", alpha=0.35)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=9)

    fig.suptitle(
        "Back-pressure: arrival rate effettivo vs tasso teorico  (stall proxy = 1 − effettivo/teorico)",
        fontsize=11,
    )
    fig.tight_layout()
    out = out_dir / "07_bp_arrival_stall.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# main
# ════════════════════════════════════════════════════════════════════════════
def main():
    parser = argparse.ArgumentParser(description="Plot pipeline latency runs")
    parser.add_argument("--log-dir", default="./logs",
                        help="Cartella con i CSV delle run (default: ./logs)")
    parser.add_argument("--out-dir", default="./plots",
                        help="Cartella di output per i PDF (default: ./plots)")
    parser.add_argument("--bin-s", type=float, default=DEFAULT_BIN_S,
                        help="Ampiezza bin temporale in secondi per aggregazione cross-run")
    parser.add_argument("--config", default="config.yaml",
                        help="Path al config.yaml con i parametri sinusoidali (default: config.yaml)")
    parser.add_argument("csvfiles", nargs="*",
                        help="File CSV specifici (sovrascrive --log-dir)")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Raccolta file CSV
    if args.csvfiles:
        csv_paths = [Path(f) for f in args.csvfiles]
    else:
        csv_paths = sorted(log_dir.glob("run_*.csv"))

    if not csv_paths:
        print(f"Nessun CSV trovato in {log_dir}. Specifica i file o controlla --log-dir.")
        sys.exit(1)

    print(f"Carico {len(csv_paths)} run:")
    for p in csv_paths:
        print(f"  {p}")

    df = load_all(csv_paths)
    print(f"  → {len(df):,} righe totali")

    sin_cfg = load_sinusoidal_config(args.config)
    if not sin_cfg:
        print(f"  [warn] config.yaml non trovato in '{args.config}': plot 07 senza tasso teorico")

    print(f"\nGenero plot in {out_dir} ...")
    plot_inter_stage_boxplot(df, out_dir)
    plot_intra_stage_boxplot(df, out_dir)
    plot_threads_lambda(df, out_dir, args.bin_s)
    plot_e2e_latency(df, out_dir, args.bin_s)
    plot_throughput(df, out_dir, args.bin_s)
    plot_arrival_vs_throughput(df, out_dir, args.bin_s)
    plot_bp_evidence(df, out_dir, args.bin_s, sin_cfg)

    print("\nDone.")


if __name__ == "__main__":
    main()
