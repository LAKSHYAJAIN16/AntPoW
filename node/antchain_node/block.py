"""Block structure and hashing.

The block hash commits to prev_hash, transactions, the miner's TSP tour and
its length, the miner address, and the nonce -- exactly the
h = H(prev || x || f(x) || nonce) construction from the paper (transactions
and miner address are additionally committed so the block also functions as
a real ledger entry, not just a consensus artifact).
"""
from __future__ import annotations

import hashlib
import time
from dataclasses import dataclass, field

from .crypto import canonical_json
from .transaction import Transaction

COINBASE_SENDER = "COINBASE"


@dataclass
class Block:
    index: int
    prev_hash: str
    timestamp: float
    transactions: list[Transaction]
    tour: list[int]
    f_x: float
    f_ref: float
    miner_address: str
    reward: float
    nonce: int = 0
    hash: str = ""

    def tx_root(self) -> str:
        tx_hashes = [tx.tx_hash for tx in self.transactions]
        return hashlib.sha256(canonical_json({"tx_hashes": tx_hashes})).hexdigest()

    def header_payload(self) -> dict:
        return {
            "index": self.index,
            "prev_hash": self.prev_hash,
            "timestamp": self.timestamp,
            "tx_root": self.tx_root(),
            "tour": self.tour,
            "f_x": self.f_x,
            "miner_address": self.miner_address,
            "reward": self.reward,
            "nonce": self.nonce,
        }

    def compute_hash(self) -> str:
        return hashlib.sha256(canonical_json(self.header_payload())).hexdigest()

    def finalize(self) -> None:
        self.hash = self.compute_hash()

    def to_dict(self) -> dict:
        return {
            "index": self.index,
            "prev_hash": self.prev_hash,
            "timestamp": self.timestamp,
            "transactions": [tx.to_dict() for tx in self.transactions],
            "tour": self.tour,
            "f_x": self.f_x,
            "f_ref": self.f_ref,
            "miner_address": self.miner_address,
            "reward": self.reward,
            "nonce": self.nonce,
            "hash": self.hash,
        }

    @staticmethod
    def from_dict(d: dict) -> "Block":
        return Block(
            index=d["index"],
            prev_hash=d["prev_hash"],
            timestamp=d["timestamp"],
            transactions=[Transaction.from_dict(t) for t in d["transactions"]],
            tour=d["tour"],
            f_x=d["f_x"],
            f_ref=d["f_ref"],
            miner_address=d["miner_address"],
            reward=d["reward"],
            nonce=d["nonce"],
            hash=d["hash"],
        )


def genesis_block() -> Block:
    b = Block(
        index=0,
        prev_hash="0" * 64,
        timestamp=0.0,
        transactions=[],
        tour=[],
        f_x=0.0,
        f_ref=0.0,
        miner_address=COINBASE_SENDER,
        reward=0.0,
        nonce=0,
    )
    b.finalize()
    return b
