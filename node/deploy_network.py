#!/usr/bin/env python
"""Launcher for a real multi-node AntChain toy network: start N node
processes (a chosen subset mining), track them, report live status across
all of them, and stop them cleanly. Replaces hand-launching each node in its
own terminal.

Usage:
    python deploy_network.py start  --nodes 5 --miners 2
    python deploy_network.py status
    python deploy_network.py stop
"""
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from antchain_node.rpc import request as rpc_request  # noqa: E402

HERE = Path(__file__).resolve().parent
NET_DIR = HERE / "data" / "net"
STATE_FILE = NET_DIR / "network.json"


def safe_status(host: str, port: int) -> dict | None:
    try:
        return rpc_request(host, port, {"type": "get_status"}, timeout=2.0)
    except OSError:
        return None


def _load_state() -> dict | None:
    if not STATE_FILE.exists():
        return None
    with open(STATE_FILE) as f:
        return json.load(f)


def _save_state(state: dict) -> None:
    NET_DIR.mkdir(parents=True, exist_ok=True)
    with open(STATE_FILE, "w") as f:
        json.dump(state, f, indent=2)


def _peers_for(topology: str, i: int, n: int, base_port: int) -> list[str]:
    """host:port strings this node should dial at startup. Gossip fills in
    full connectivity from there, so the topology only needs to be
    connected, not complete."""
    if i == 0:
        return []
    if topology == "star":
        return [f"127.0.0.1:{base_port}"]
    if topology == "ring":
        return [f"127.0.0.1:{base_port + i - 1}"]
    if topology == "mesh":
        return [f"127.0.0.1:{base_port + j}" for j in range(i)]
    raise ValueError(f"unknown topology {topology!r}")


def cmd_start(args: argparse.Namespace) -> None:
    if _load_state() is not None:
        print("A network is already running (or wasn't cleanly stopped). "
              "Run `python deploy_network.py stop` first.", file=sys.stderr)
        sys.exit(1)

    NET_DIR.mkdir(parents=True, exist_ok=True)
    logs_dir = NET_DIR / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)

    nodes = []
    for i in range(args.nodes):
        port = args.base_port + i
        wallet_path = NET_DIR / f"wallet{i}.json"
        data_path = NET_DIR / f"chain{i}.json"
        is_miner = i < args.miners

        if not wallet_path.exists():
            subprocess.run(
                [sys.executable, "-m", "antchain_node", "wallet-new", "--out", str(wallet_path)],
                cwd=HERE, check=True, capture_output=True,
            )
        address = json.loads(wallet_path.read_text())["address"]

        peers = _peers_for(args.topology, i, args.nodes, args.base_port)
        cmd = [
            sys.executable, "-u", "-m", "antchain_node", "run",
            "--host", "127.0.0.1", "--port", str(port),
            "--data", str(data_path),
            "--wallet", str(wallet_path),
            "--cities", str(args.cities),
            "--t-opt-frac", str(args.t_opt_frac),
            "--lam", str(args.lam),
            "--difficulty-bits", str(args.difficulty_bits),
            "--retarget-interval", str(args.retarget_interval),
            "--target-block-time", str(args.target_block_time),
            "--block-reward", str(args.block_reward),
        ]
        if peers:
            cmd += ["--peers", ",".join(peers)]
        if is_miner:
            cmd += ["--mine"]

        log_path = logs_dir / f"node{i}.log"
        log_f = open(log_path, "w")
        proc = subprocess.Popen(cmd, cwd=HERE, stdout=log_f, stderr=subprocess.STDOUT)
        nodes.append({
            "index": i, "port": port, "pid": proc.pid, "mining": is_miner,
            "wallet": str(wallet_path), "address": address, "log": str(log_path),
            "peers": peers,
        })
        print(f"[deploy] node {i}: port={port} mining={is_miner} address={address} pid={proc.pid}")
        time.sleep(0.3)  # stagger startup so early nodes are listening before later ones dial in

    _save_state({
        "nodes": nodes,
        "params": vars(args) | {"func": None},
    })
    print(f"\nStarted {args.nodes} nodes ({args.miners} mining). "
          f"Logs in {logs_dir}. State in {STATE_FILE}.")
    print("Check on it with: python deploy_network.py status")


def cmd_status(args: argparse.Namespace) -> None:
    state = _load_state()
    if state is None:
        print("No network is currently deployed (no state file). Run `start` first.")
        return
    for n in state["nodes"]:
        st = safe_status("127.0.0.1", n["port"])
        role = "MINER" if n["mining"] else "peer "
        if st is None:
            print(f"node {n['index']} [{role}] port={n['port']}: unreachable (process may have exited)")
        else:
            print(f"node {n['index']} [{role}] port={n['port']}: height={st['height']} "
                  f"tip={st['tip'][:12]}... peers={len(st['peers'])} mempool={st['mempool_size']}")


def cmd_stop(args: argparse.Namespace) -> None:
    state = _load_state()
    if state is None:
        print("No network is currently deployed.")
        return
    for n in state["nodes"]:
        try:
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/PID", str(n["pid"]), "/F"], capture_output=True)
            else:
                os.kill(n["pid"], signal.SIGTERM)
            print(f"[deploy] stopped node {n['index']} (pid={n['pid']})")
        except (ProcessLookupError, OSError) as e:
            print(f"[deploy] node {n['index']} (pid={n['pid']}) already gone: {e}")
    STATE_FILE.unlink(missing_ok=True)
    print("Network stopped, state file removed. Chain/wallet data left in place under data/net/.")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("start", help="Launch a network of N nodes")
    s.add_argument("--nodes", type=int, default=5)
    s.add_argument("--miners", type=int, default=2, help="First N nodes (by index) mine")
    s.add_argument("--base-port", type=int, default=7001)
    s.add_argument("--topology", choices=["star", "ring", "mesh"], default="mesh",
                    help="star: all dial node 0. ring: each dials the previous node. "
                         "mesh: each dials every earlier node (most robust for a small toy net).")
    s.add_argument("--cities", type=int, default=8)
    s.add_argument("--t-opt-frac", dest="t_opt_frac", type=float, default=1.15)
    s.add_argument("--lam", type=float, default=3.0)
    s.add_argument("--difficulty-bits", type=int, default=16)
    s.add_argument("--retarget-interval", type=int, default=10)
    s.add_argument("--target-block-time", type=float, default=8.0)
    s.add_argument("--block-reward", type=float, default=50.0)
    s.set_defaults(func=cmd_start)

    st = sub.add_parser("status", help="Poll all deployed nodes and print a live table")
    st.set_defaults(func=cmd_status)

    sp = sub.add_parser("stop", help="Kill all deployed node processes")
    sp.set_defaults(func=cmd_stop)

    return p


def main() -> None:
    args = build_parser().parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
