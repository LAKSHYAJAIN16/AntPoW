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
from antchain_node.crypto import Wallet  # noqa: E402
from antchain_node.rpc import request as rpc_request  # noqa: E402
from antchain_node.transaction import Transaction  # noqa: E402

HERE = Path(__file__).resolve().parent
NET_DIR = HERE / "data" / "net"
STATE_FILE = NET_DIR / "network.json"


def safe_rpc(port: int, msg: dict) -> dict | None:
    try:
        return rpc_request("127.0.0.1", port, msg, timeout=2.0)
    except OSError:
        return None


def safe_status(host: str, port: int) -> dict | None:
    return safe_rpc(port, {"type": "get_status"})


def _require_state() -> dict:
    state = _load_state()
    if state is None:
        print("No network is currently deployed (no state file). Run `start` first.", file=sys.stderr)
        sys.exit(1)
    return state


def _node_by_index(state: dict, index: int) -> dict:
    for n in state["nodes"]:
        if n["index"] == index:
            return n
    print(f"error: no node with index {index} (deployed nodes are 0..{len(state['nodes'])-1})", file=sys.stderr)
    sys.exit(1)


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


def cmd_balances(args: argparse.Namespace) -> None:
    state = _require_state()
    # any reachable node can answer balance queries for any address; try each
    # node's own port first, falling back to the first reachable node.
    reachable_port = None
    for n in state["nodes"]:
        if safe_status("127.0.0.1", n["port"]) is not None:
            reachable_port = n["port"]
            break
    if reachable_port is None:
        print("No deployed nodes are reachable (all processes may have exited).", file=sys.stderr)
        sys.exit(1)

    for n in state["nodes"]:
        resp = safe_rpc(n["port"], {"type": "get_balance", "address": n["address"]}) or \
               safe_rpc(reachable_port, {"type": "get_balance", "address": n["address"]})
        role = "MINER" if n["mining"] else "peer "
        bal = f"{resp['balance']:.1f}" if resp else "?"
        print(f"node {n['index']} [{role}] {n['address']}  balance={bal}")


def cmd_send(args: argparse.Namespace) -> None:
    state = _require_state()
    from_node = _node_by_index(state, args.from_index)
    to_node = _node_by_index(state, args.to_index)
    wallet = Wallet.from_dict(json.loads(Path(from_node["wallet"]).read_text()))

    bal_resp = safe_rpc(from_node["port"], {"type": "get_balance", "address": wallet.address})
    if bal_resp is None:
        print(f"node {args.from_index} (port {from_node['port']}) is unreachable.", file=sys.stderr)
        sys.exit(1)

    tx = Transaction(
        sender=wallet.address, sender_pubkey=wallet.pubkey_hex,
        recipient=to_node["address"], amount=args.amount, nonce=bal_resp["nonce"],
    )
    tx.sign(wallet)
    result = safe_rpc(from_node["port"], {"type": "submit_tx", "tx": tx.to_dict()})
    if result and result.get("accepted"):
        print(f"Sent {args.amount} from node {args.from_index} -> node {args.to_index} "
              f"(tx {tx.tx_hash[:16]}...). It'll land once a block picks it up -- "
              f"check with: python deploy_network.py balances")
    else:
        print(f"Node rejected the transaction: {result}", file=sys.stderr)
        sys.exit(1)


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

    ba = sub.add_parser("balances", help="Print every deployed node's own wallet balance")
    ba.set_defaults(func=cmd_balances)

    se = sub.add_parser("send", help="Send coins from one deployed node's wallet to another, by index")
    se.add_argument("--from", dest="from_index", type=int, required=True, help="Sender node index (see `status`)")
    se.add_argument("--to", dest="to_index", type=int, required=True, help="Recipient node index")
    se.add_argument("--amount", type=float, required=True)
    se.set_defaults(func=cmd_send)

    sp = sub.add_parser("stop", help="Kill all deployed node processes")
    sp.set_defaults(func=cmd_stop)

    return p


def main() -> None:
    args = build_parser().parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
