"""CLI entry point: wallet management, running a node, sending transactions,
and querying a running node."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .consensus import ConsensusParams
from .crypto import Wallet
from .node import Node
from .rpc import request
from .transaction import Transaction


def _load_wallet(path: str) -> Wallet:
    with open(path) as f:
        return Wallet.from_dict(json.load(f))


def cmd_wallet_new(args: argparse.Namespace) -> None:
    w = Wallet.generate()
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w") as f:
        json.dump(w.to_dict(), f, indent=2)
    print(f"Wrote wallet to {out}")
    print(f"Address: {w.address}")


def cmd_wallet_address(args: argparse.Namespace) -> None:
    w = _load_wallet(args.wallet)
    print(w.address)


def _parse_peers(peers_arg: str | None) -> list[tuple[str, int]]:
    if not peers_arg:
        return []
    out = []
    for entry in peers_arg.split(","):
        entry = entry.strip()
        if not entry:
            continue
        host, port = entry.rsplit(":", 1)
        out.append((host, int(port)))
    return out


def cmd_run(args: argparse.Namespace) -> None:
    params = ConsensusParams(
        n_cities=args.cities,
        t_opt_frac=args.t_opt_frac,
        lam=args.lam,
        difficulty_bits=args.difficulty_bits,
        retarget_interval=args.retarget_interval,
        target_block_time=args.target_block_time,
        block_reward=args.block_reward,
    )
    miner_address = None
    if args.mine:
        if not args.wallet:
            print("error: --mine requires --wallet (block rewards need a recipient address)", file=sys.stderr)
            sys.exit(1)
        miner_address = _load_wallet(args.wallet).address

    node = Node(
        host=args.host, port=args.port, params=params,
        data_path=args.data, miner_address=miner_address, mine=args.mine,
    )
    peers = _parse_peers(args.peers)
    node.start(peers)
    print(f"[node] running on {args.host}:{args.port}, height={node.chain.height}")
    if miner_address:
        print(f"[node] mining to {miner_address}")
    node.run_forever()


def cmd_send(args: argparse.Namespace) -> None:
    wallet = _load_wallet(args.wallet)
    host, port = args.node.rsplit(":", 1)
    port = int(port)
    balance_resp = request(host, port, {"type": "get_balance", "address": wallet.address})
    nonce = args.nonce if args.nonce is not None else balance_resp.get("nonce", 0)
    tx = Transaction(
        sender=wallet.address, sender_pubkey=wallet.pubkey_hex,
        recipient=args.to, amount=args.amount, nonce=nonce,
    )
    tx.sign(wallet)
    result = request(host, port, {"type": "submit_tx", "tx": tx.to_dict()})
    if result.get("accepted"):
        print(f"Submitted tx {tx.tx_hash}")
    else:
        print(f"Node rejected tx {tx.tx_hash} (insufficient balance, bad nonce, or bad signature)", file=sys.stderr)
        sys.exit(1)


def cmd_balance(args: argparse.Namespace) -> None:
    host, port = args.node.rsplit(":", 1)
    resp = request(host, int(port), {"type": "get_balance", "address": args.address})
    print(resp["balance"])


def cmd_status(args: argparse.Namespace) -> None:
    host, port = args.node.rsplit(":", 1)
    resp = request(host, int(port), {"type": "get_status"})
    print(json.dumps(resp, indent=2))


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="antchain-node", description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)

    w = sub.add_parser("wallet-new", help="Generate a new wallet keypair")
    w.add_argument("--out", required=True, help="Path to write the wallet JSON file")
    w.set_defaults(func=cmd_wallet_new)

    wa = sub.add_parser("wallet-address", help="Print a wallet's address")
    wa.add_argument("--wallet", required=True)
    wa.set_defaults(func=cmd_wallet_address)

    r = sub.add_parser("run", help="Run a node")
    r.add_argument("--host", default="127.0.0.1")
    r.add_argument("--port", type=int, required=True)
    r.add_argument("--peers", default="", help="Comma-separated host:port list of peers to connect to")
    r.add_argument("--data", default=None, help="Path to persist the chain as JSON")
    r.add_argument("--wallet", default=None, help="Wallet file to receive block rewards (required with --mine)")
    r.add_argument("--mine", action="store_true")
    r.add_argument("--cities", type=int, default=10)
    r.add_argument("--t-opt-frac", dest="t_opt_frac", type=float, default=1.15)
    r.add_argument("--lam", type=float, default=3.0)
    r.add_argument("--difficulty-bits", type=int, default=20,
                    help="Initial target = 2^256 >> difficulty_bits. Lower = easier/faster blocks.")
    r.add_argument("--retarget-interval", type=int, default=10)
    r.add_argument("--target-block-time", type=float, default=8.0)
    r.add_argument("--block-reward", type=float, default=50.0)
    r.set_defaults(func=cmd_run)

    s = sub.add_parser("send", help="Send a signed transaction to a running node")
    s.add_argument("--node", required=True, help="host:port of a running node")
    s.add_argument("--wallet", required=True)
    s.add_argument("--to", required=True)
    s.add_argument("--amount", type=float, required=True)
    s.add_argument("--nonce", type=int, default=None, help="Override sender nonce (default: ask the node)")
    s.set_defaults(func=cmd_send)

    b = sub.add_parser("balance", help="Query an address's balance from a running node")
    b.add_argument("--node", required=True)
    b.add_argument("--address", required=True)
    b.set_defaults(func=cmd_balance)

    st = sub.add_parser("status", help="Query a running node's chain status")
    st.add_argument("--node", required=True)
    st.set_defaults(func=cmd_status)

    return p


def main(argv: list[str] | None = None) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
