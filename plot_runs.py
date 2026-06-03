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
INTER_COLOR  = "#4C72B0"
INTRA_COLOR  = "#DD8452"
THREAD_A_COL    = "#55A868"
THREAD_B_COL    = "#C44E52"
LAMBDA_COL      = "#8172B2"
LAMBDA_SMOOTH_W = 30   # finestra rolling per smorzare il rumore dell'EMA lambda


RUN_GAP_S = 0.0  # nessun gap: tutte le run concatenate come un'unica serie


def load_all(csv_paths: list[Path]) -> pd.DataFrame:
    """Carica tutti i CSV in un unico DataFrame con asse temporale continuo."""
    frames: list[pd.DataFrame] = []
    t_offset = 0.0
    for p in csv_paths:
        df = pd.read_csv(p)
        df["send_time_s"] = df["send_time_ns"] * 1e-9
        df["exit_time_s"] = df["exit_time_ns"] * 1e-9
        t0 = df["send_time_s"].iloc[0]
        df["t_send_rel"] = df["send_time_s"] - t0
        df["t_exit_rel"] = df["exit_time_s"] - t0
        df["t_global"]   = df["t_send_rel"] + t_offset
        t_offset += df["t_send_rel"].max() + RUN_GAP_S
        frames.append(df)
    return pd.concat(frames, ignore_index=True)


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
        bp = ax.boxplot(data, labels=[stage_name], patch_artist=True,
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
        bp = ax.boxplot(data, labels=[stage_name], patch_artist=True,
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
# Plot 3 – Thread count (step) + λ smoothed (line)  –  tutte le run
#           sull'asse globale, con linee verticali di separazione tra run
# ════════════════════════════════════════════════════════════════════════════
def plot_threads_lambda(df: pd.DataFrame, out_dir: Path):
    fig, ax1 = plt.subplots(figsize=(14, 5))
    ax2 = ax1.twinx()

    t = df["t_global"].values

    # thread count: step plot (valori discreti)
    ax1.step(t, df["threads_A"].values, where="post",
             color=THREAD_A_COL, linewidth=1.6, label="Thread A")
    ax1.step(t, df["threads_B"].values, where="post",
             color=THREAD_B_COL, linewidth=1.6, label="Thread B", linestyle="--")
    ax1.set_ylabel("Numero di repliche (thread)")
    ax1.set_xlabel("Tempo globale (s)")
    ax1.yaxis.set_major_locator(mticker.MaxNLocator(integer=True))

    # λ: smoothing globale con rolling mean
    lambda_smooth = (
        pd.Series(df["lambda"].values)
        .rolling(window=LAMBDA_SMOOTH_W, center=True, min_periods=1)
        .mean()
        .values
    )

    ax2.plot(t, lambda_smooth, color=LAMBDA_COL,
             linewidth=1.4, alpha=0.9, label="λ (smoothed)")
    ax2.set_ylabel("Tasso di arrivo λ (items/s)")

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=9)

    ax1.grid(axis="y", linestyle="--", alpha=0.4)
    fig.suptitle("Thread count e carico in arrivo", fontsize=12)
    fig.tight_layout()

    out = out_dir / "03_threads_lambda.pdf"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"  Salvato: {out}")


# ════════════════════════════════════════════════════════════════════════════
# Plot 4 – E2E scatter: x = t_send_rel, y = t_exit_rel
#           Colore = latenza E2E (ms); diagonale y=x come riferimento
# ════════════════════════════════════════════════════════════════════════════
def plot_e2e_scatter(df: pd.DataFrame, out_dir: Path):
    fig, ax = plt.subplots(figsize=(12, 6))

    e2e_ms = df["end_to_end_s"].values * 1e3
    sc = ax.scatter(
        df["t_global"].values,
        df["t_global"].values + df["end_to_end_s"].values,
        c=e2e_ms, cmap="plasma", s=4, alpha=0.7,
        vmin=np.percentile(e2e_ms, 2), vmax=np.percentile(e2e_ms, 98)
    )
    cbar = fig.colorbar(sc, ax=ax)
    cbar.set_label("Latenza E2E (ms)")

    # linea di riferimento y = x (latenza zero)
    t = df["t_global"].values
    ax.plot([t.min(), t.max()], [t.min(), t.max()],
            color="gray", linewidth=0.8, linestyle="--", label="y = x (latency=0)")
    ax.legend(fontsize=8)

    ax.set_xlabel("Tempo di invio (s)")
    ax.set_ylabel("Tempo di uscita dalla pipeline (s)")
    ax.set_title("E2E: tempo di invio vs tempo di uscita", fontsize=12)
    ax.grid(linestyle="--", alpha=0.3)
    fig.tight_layout()

    out = out_dir / "04_e2e_scatter.pdf"
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

    print(f"\nGenero plot in {out_dir} ...")
    plot_inter_stage_boxplot(df, out_dir)
    plot_intra_stage_boxplot(df, out_dir)
    plot_threads_lambda(df, out_dir)
    plot_e2e_scatter(df, out_dir)

    print("\nDone.")


if __name__ == "__main__":
    main()
