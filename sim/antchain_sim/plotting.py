"""Comparison plots across conditions: fairness, security, and efficiency."""
from __future__ import annotations

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


def make_plots(summary_df: pd.DataFrame, blocks_df: pd.DataFrame, outdir: Path) -> list[Path]:
    outdir = Path(outdir)
    paths = []

    fig, axes = plt.subplots(2, 2, figsize=(11, 8))
    labels = summary_df.index.tolist()
    x = range(len(labels))

    ax = axes[0, 0]
    ax.bar(x, summary_df["gini_rewards"], label="Gini(rewards)")
    ax.axhline(summary_df["gini_hashrate"].iloc[0], color="black", linestyle="--", label="Gini(hashrate) baseline")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_title("Reward inequality vs. hashrate-share baseline")
    ax.legend(fontsize=8)

    ax = axes[0, 1]
    ax.bar(x, summary_df["strategic_advantage"], color="tab:orange")
    ax.axhline(1.0, color="black", linestyle="--")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_title("Strategic-miner advantage (reward share / hashrate share)")

    ax = axes[1, 0]
    ax.bar(x, summary_df["fork_rate"], color="tab:red")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_title("Fork rate")

    ax = axes[1, 1]
    ax.bar(x, summary_df["useful_improvement_per_joule"], color="tab:green")
    ax.set_xticks(list(x)); ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_title("Useful optimization per unit compute")

    fig.tight_layout()
    p1 = outdir / "comparison_summary.png"
    fig.savefig(p1, dpi=150)
    plt.close(fig)
    paths.append(p1)

    fig2, ax2 = plt.subplots(figsize=(8, 5))
    for label, grp in blocks_df.groupby("label"):
        ax2.plot(grp["block_index"], grp["block_time_ticks"], marker="o", markersize=3, alpha=0.7, label=label)
    ax2.set_xlabel("block index")
    ax2.set_ylabel("block time (ticks)")
    ax2.set_title("Block time series by condition")
    ax2.legend(fontsize=7)
    fig2.tight_layout()
    p2 = outdir / "block_times.png"
    fig2.savefig(p2, dpi=150)
    plt.close(fig2)
    paths.append(p2)

    return paths
