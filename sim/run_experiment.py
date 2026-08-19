#!/usr/bin/env python
"""CLI entry point for the AntChain 3-way experiment (SHA-PoW vs pure PoAO vs
Hybrid PoW+PoAO). Writes a summary CSV, a per-block CSV, and comparison plots
to --outdir.

Examples:
    python run_experiment.py --quick
    python run_experiment.py --miners 50 --blocks 80 --lambdas 0.5 1 3 10
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from antchain_sim.experiment import ExperimentConfig, run_full_experiment  # noqa: E402


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--miners", type=int, default=30)
    p.add_argument("--blocks", type=int, default=40)
    p.add_argument("--cities", type=int, default=16)
    p.add_argument("--max-ticks", type=int, default=40)
    p.add_argument("--target-block-time", type=float, default=15.0)
    p.add_argument("--strategic-fraction", type=float, default=0.15)
    p.add_argument("--lambdas", type=float, nargs="+", default=[0.5, 1.0, 3.0, 10.0])
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--estimate-optimum", action="store_true",
                    help="Also estimate true-optimum convergence ratio (slower: runs 2-opt per block).")
    p.add_argument("--quick", action="store_true", help="Tiny smoke-test configuration.")
    p.add_argument("--outdir", type=str, default=str(Path(__file__).resolve().parent.parent / "results"))
    return p.parse_args()


def main() -> None:
    args = parse_args()
    if args.quick:
        cfg = ExperimentConfig(
            n_miners=10, n_blocks=8, n_cities=8, max_ticks=20,
            lambdas=(1.0, 5.0), seed=args.seed,
        )
    else:
        cfg = ExperimentConfig(
            n_miners=args.miners,
            n_blocks=args.blocks,
            n_cities=args.cities,
            max_ticks=args.max_ticks,
            target_block_time=args.target_block_time,
            strategic_fraction=args.strategic_fraction,
            lambdas=tuple(args.lambdas),
            seed=args.seed,
            estimate_optimum=args.estimate_optimum,
        )

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    print(f"Running experiment: {cfg}")
    summary_df, blocks_df = run_full_experiment(cfg)

    summary_path = outdir / "summary.csv"
    blocks_path = outdir / "blocks.csv"
    summary_df.to_csv(summary_path)
    blocks_df.to_csv(blocks_path, index=False)

    pd_display_cols = [
        "fork_rate", "mean_block_time", "block_time_cv",
        "gini_rewards", "gini_hashrate", "gini_excess",
        "strategic_advantage", "useful_improvement_per_joule",
    ]
    print("\n=== Summary ===")
    print(summary_df[pd_display_cols].to_string(float_format=lambda x: f"{x:.4f}"))
    print(f"\nWrote {summary_path}")
    print(f"Wrote {blocks_path}")

    try:
        from antchain_sim.plotting import make_plots
        plot_paths = make_plots(summary_df, blocks_df, outdir)
        for pth in plot_paths:
            print(f"Wrote {pth}")
    except Exception as e:  # plotting is best-effort, never fail the run over it
        print(f"[warn] plotting failed: {e}")


if __name__ == "__main__":
    main()
