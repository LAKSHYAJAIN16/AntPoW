"""Pending-transaction pool. Admission here is a courtesy pre-check (valid
signature, plausible nonce/balance against the current tip); the real check
happens again at block-validation time, since mempool state can go stale."""
from __future__ import annotations

import threading

from .chain import Blockchain
from .transaction import Transaction


class Mempool:
    def __init__(self):
        self.lock = threading.RLock()
        self._by_hash: dict[str, Transaction] = {}

    def add(self, tx: Transaction, chain: Blockchain) -> bool:
        with self.lock:
            if tx.tx_hash in self._by_hash:
                return False
            if not tx.verify():
                return False
            if chain.balance_of(tx.sender) < tx.amount:
                return False
            self._by_hash[tx.tx_hash] = tx
            return True

    def remove_all(self, txs: list[Transaction]) -> None:
        with self.lock:
            for tx in txs:
                self._by_hash.pop(tx.tx_hash, None)

    def select(self, chain: Blockchain, max_count: int) -> list[Transaction]:
        """Greedily select pending txs whose sender-nonce sequence is
        currently satisfiable, respecting per-sender ordering."""
        with self.lock:
            pending = list(self._by_hash.values())
        by_sender: dict[str, list[Transaction]] = {}
        for tx in pending:
            by_sender.setdefault(tx.sender, []).append(tx)
        for txs in by_sender.values():
            txs.sort(key=lambda t: t.nonce)

        selected: list[Transaction] = []
        next_nonce = {s: chain.next_nonce_for(s) for s in by_sender}
        balances = {s: chain.balance_of(s) for s in by_sender}
        progressed = True
        while progressed and len(selected) < max_count:
            progressed = False
            for sender, txs in by_sender.items():
                for tx in txs:
                    if tx in selected:
                        continue
                    if tx.nonce == next_nonce[sender] and balances[sender] >= tx.amount:
                        selected.append(tx)
                        next_nonce[sender] += 1
                        balances[sender] -= tx.amount
                        progressed = True
                        if len(selected) >= max_count:
                            return selected
                    break  # only the head of each sender's queue can progress
        return selected

    def __len__(self) -> int:
        with self.lock:
            return len(self._by_hash)
