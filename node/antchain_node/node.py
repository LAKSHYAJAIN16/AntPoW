"""Ties together chain, mempool, miner, and network into one running node."""
from __future__ import annotations

import socket
import threading
import time

from .block import Block
from .chain import Blockchain, ChainError
from .consensus import ConsensusParams
from .mempool import Mempool
from .miner import Miner
from .network import Network
from .transaction import Transaction


class Node:
    def __init__(self, host: str, port: int, params: ConsensusParams,
                 data_path: str | None, miner_address: str | None,
                 mine: bool, log=print):
        self.host = host
        self.port = port
        self.log = log
        self.chain = Blockchain(params, data_path=data_path)
        self.chain.load()
        self.mempool = Mempool()
        self.network = Network(host, port, self._on_message, log=log)
        self.miner_address = miner_address
        self.miner = (
            Miner(self.chain, self.mempool, miner_address, self._on_block_found, log=log)
            if mine and miner_address else None
        )

    # ------------------------------------------------------------- start
    def start(self, peers: list[tuple[str, int]]) -> None:
        self.network.start()
        for h, p in peers:
            if self.network.connect_to(h, p):
                self.network.send(self.network.peers[f"{h}:{p}"], {
                    "type": "hello", "host": self.host, "port": self.port, "height": self.chain.height,
                })
                self.network.send(self.network.peers[f"{h}:{p}"], {"type": "get_chain"})
        if self.miner is not None:
            self.miner.start()
            self.log(f"[node] mining enabled, rewards -> {self.miner_address}")

    # --------------------------------------------------------- callbacks
    def _on_block_found(self, block: Block) -> None:
        try:
            self.chain.try_extend(block)
            self.mempool.remove_all(block.transactions)
            self.network.broadcast({"type": "new_block", "block": block.to_dict()})
            self.log(f"[chain] height={self.chain.height} tip={self.chain.tip.hash[:16]}...")
        except ChainError as e:
            self.log(f"[miner] mined a block that failed self-validation: {e}")

    def _on_message(self, sock: socket.socket, msg: dict) -> None:
        t = msg.get("type")
        if t == "hello":
            return
        if t == "get_chain":
            self.network.send(sock, {"type": "chain", "blocks": self.chain.to_dict_list()})
        elif t == "chain":
            self._handle_full_chain(msg.get("blocks", []))
        elif t == "new_block":
            self._handle_new_block(msg.get("block"), origin=sock)
        elif t == "new_tx":
            self._handle_new_tx(msg.get("tx"), origin=sock)
        elif t == "get_balance":
            self.network.send(sock, {
                "type": "balance", "address": msg["address"],
                "balance": self.chain.balance_of(msg["address"]),
                "nonce": self.chain.next_nonce_for(msg["address"]),
            })
        elif t == "get_status":
            self.network.send(sock, {
                "type": "status", "height": self.chain.height, "tip": self.chain.tip.hash,
                "mempool_size": len(self.mempool), "peers": list(self.network.peers.keys()),
            })
        elif t == "submit_tx":
            try:
                tx = Transaction.from_dict(msg["tx"])
                ok = self.mempool.add(tx, self.chain)
                if ok:
                    self.network.broadcast({"type": "new_tx", "tx": tx.to_dict()}, exclude=sock)
                self.network.send(sock, {"type": "submit_tx_result", "accepted": ok, "tx_hash": tx.tx_hash})
            except Exception as e:
                self.network.send(sock, {"type": "submit_tx_result", "accepted": False, "error": repr(e)})

    def _handle_new_block(self, block_dict: dict | None, origin: socket.socket | None = None) -> None:
        if not block_dict:
            return
        block = Block.from_dict(block_dict)
        if block.hash == self.chain.tip.hash:
            return
        if block.prev_hash != self.chain.tip.hash:
            self.log(f"[chain] received block {block.index} that doesn't extend our tip; requesting full chain")
            self.network.broadcast({"type": "get_chain"})
            return
        try:
            self.chain.try_extend(block)
            self.mempool.remove_all(block.transactions)
            self.network.broadcast({"type": "new_block", "block": block.to_dict()}, exclude=origin)
            self.log(f"[chain] accepted block {block.index} from network, height={self.chain.height}")
        except ChainError as e:
            self.log(f"[chain] rejected block {block.index}: {e}")

    def _handle_full_chain(self, blocks_raw: list[dict]) -> None:
        if not blocks_raw:
            return
        try:
            blocks = [Block.from_dict(d) for d in blocks_raw]
        except (KeyError, TypeError):
            return
        try:
            if self.chain.try_replace(blocks):
                self.log(f"[chain] adopted longer/heavier chain, height={self.chain.height}")
        except ChainError as e:
            self.log(f"[chain] rejected candidate chain: {e}")

    def _handle_new_tx(self, tx_dict: dict | None, origin: socket.socket | None = None) -> None:
        if not tx_dict:
            return
        tx = Transaction.from_dict(tx_dict)
        if self.mempool.add(tx, self.chain):
            self.network.broadcast({"type": "new_tx", "tx": tx.to_dict()}, exclude=origin)

    def run_forever(self) -> None:
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            self.log("[node] shutting down")
            if self.miner is not None:
                self.miner.stop()
