"""Blockchain state: validation, the account ledger, difficulty history, and
JSON persistence. Kept intentionally simple for a toy/educational chain:
- Single canonical chain in memory (no side-branch tree); a competing chain
  is only adopted if a peer presents one with strictly higher cumulative
  work, in which case we validate it wholesale and swap it in.
- State (balances, per-sender nonces) is rebuilt by replaying the canonical
  chain from genesis. Fine for chains of the length this project is meant
  to run (thousands of blocks, not millions).
"""
from __future__ import annotations

import json
import threading
from pathlib import Path

from .block import Block, genesis_block
from .consensus import ConsensusParams, MAX_TARGET, quality_target, retarget
from .tsp import generate_instance, reference_length

EPS = 1e-6


class ChainError(ValueError):
    pass


class Blockchain:
    def __init__(self, params: ConsensusParams, data_path: str | None = None):
        self.params = params
        self.data_path = Path(data_path) if data_path else None
        self.lock = threading.RLock()
        self.blocks: list[Block] = [genesis_block()]
        self.balances: dict[str, float] = {}
        self.nonces: dict[str, int] = {}

    # ---------------------------------------------------------------- misc
    @property
    def height(self) -> int:
        return self.blocks[-1].index

    @property
    def tip(self) -> Block:
        return self.blocks[-1]

    # ------------------------------------------------------------- replay
    def compute_targets(self, blocks: list[Block]) -> tuple[list[int], int]:
        """Replays retargeting from genesis over `blocks`. Returns
        (targets_used_for_blocks[1:], target_for_the_block_after_the_last).

        This is the ONLY place difficulty is computed, used identically by
        the live single-block mining path (try_extend, via next_target())
        and the full-chain catch-up path (validate_full_chain). They used to
        be two separate implementations that silently disagreed once a node
        fell behind and needed to validate historical blocks retroactively
        -- validate_full_chain applied retargeting, but next_target() never
        did, so every block after the first retarget boundary looked invalid
        to a node that had to catch up, permanently stalling it.
        """
        targets = []
        t = self.params.initial_target()
        for i in range(1, len(blocks)):
            targets.append(t)
            if i % self.params.retarget_interval == 0:
                start = blocks[i - self.params.retarget_interval]
                end = blocks[i]
                actual = end.timestamp - start.timestamp
                expected = self.params.retarget_interval * self.params.target_block_time
                t = retarget(t, actual, expected)
        return targets, t

    def next_target(self) -> int:
        with self.lock:
            _, t = self.compute_targets(self.blocks)
            return t

    @staticmethod
    def cumulative_work(targets: list[int]) -> int:
        return sum(MAX_TARGET // t for t in targets if t > 0)

    # --------------------------------------------------------- validation
    def validate_block(self, block: Block, prev: Block, target: int,
                        balances: dict[str, float], nonces: dict[str, int]) -> None:
        if block.index != prev.index + 1:
            raise ChainError("bad index")
        if block.prev_hash != prev.hash:
            raise ChainError("bad prev_hash")
        if block.timestamp < prev.timestamp:
            raise ChainError("timestamp not monotonic")
        if block.compute_hash() != block.hash:
            raise ChainError("hash does not match header contents")
        if abs(block.reward - self.params.block_reward) > EPS:
            raise ChainError("bad block reward")
        if len(block.transactions) > self.params.max_txs_per_block:
            raise ChainError("too many transactions")

        instance = generate_instance(block.prev_hash, self.params.n_cities)
        if not instance.is_valid_tour(block.tour):
            raise ChainError("invalid tour: not a permutation of all cities")
        f_x = instance.tour_length(block.tour)
        if abs(f_x - block.f_x) > EPS:
            raise ChainError("declared f(x) does not match recomputed tour length")
        f_ref = reference_length(instance)
        if abs(f_ref - block.f_ref) > EPS:
            raise ChainError("declared f_ref does not match recomputed reference")
        if f_x > self.params.t_opt_frac * f_ref + EPS:
            raise ChainError("solution does not meet quality gate T_opt")

        required_target = quality_target(target, self.params.lam, f_ref, f_x)
        if int(block.hash, 16) >= required_target:
            raise ChainError("hash does not meet quality-weighted target")

        # transactions: signature, sender==address(pubkey), balance, nonce order
        touched_nonces = dict(nonces)
        touched_balances = dict(balances)
        for tx in block.transactions:
            if not tx.verify():
                raise ChainError(f"invalid signature/address on tx {tx.tx_hash}")
            expected_nonce = touched_nonces.get(tx.sender, 0)
            if tx.nonce != expected_nonce:
                raise ChainError(f"bad nonce for {tx.sender}: expected {expected_nonce}, got {tx.nonce}")
            bal = touched_balances.get(tx.sender, 0.0)
            if bal < tx.amount:
                raise ChainError(f"insufficient balance for {tx.sender}")
            touched_balances[tx.sender] = bal - tx.amount
            touched_balances[tx.recipient] = touched_balances.get(tx.recipient, 0.0) + tx.amount
            touched_nonces[tx.sender] = expected_nonce + 1

        touched_balances[block.miner_address] = touched_balances.get(block.miner_address, 0.0) + block.reward
        balances.clear(); balances.update(touched_balances)
        nonces.clear(); nonces.update(touched_nonces)

    def validate_full_chain(self, blocks: list[Block]) -> tuple[dict, dict, list[int]]:
        if not blocks or blocks[0].hash != genesis_block().hash:
            raise ChainError("bad or missing genesis block")
        balances: dict[str, float] = {}
        nonces: dict[str, int] = {}
        targets, _ = self.compute_targets(blocks)
        for i in range(1, len(blocks)):
            self.validate_block(blocks[i], blocks[i - 1], targets[i - 1], balances, nonces)
        return balances, nonces, targets

    # ------------------------------------------------------------ mutate
    def try_extend(self, block: Block) -> bool:
        """Validate `block` as the new tip extending the current chain.
        Returns True and applies it if valid."""
        with self.lock:
            target = self.next_target()
            balances = dict(self.balances)
            nonces = dict(self.nonces)
            self.validate_block(block, self.tip, target, balances, nonces)
            self.blocks.append(block)
            self.balances = balances
            self.nonces = nonces
            self._persist()
            return True

    def try_replace(self, blocks: list[Block]) -> bool:
        """Adopt `blocks` as the new canonical chain if it is valid and has
        strictly more cumulative work than the current chain."""
        with self.lock:
            balances, nonces, targets = self.validate_full_chain(blocks)
            current_targets, _ = self.compute_targets(self.blocks)
            if self.cumulative_work(targets) <= self.cumulative_work(current_targets):
                return False
            self.blocks = blocks
            self.balances = balances
            self.nonces = nonces
            self._persist()
            return True

    def balance_of(self, address: str) -> float:
        with self.lock:
            return self.balances.get(address, 0.0)

    def next_nonce_for(self, address: str) -> int:
        with self.lock:
            return self.nonces.get(address, 0)

    # -------------------------------------------------------- persistence
    def _persist(self) -> None:
        if not self.data_path:
            return
        self.data_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.data_path, "w") as f:
            json.dump([b.to_dict() for b in self.blocks], f)

    def load(self) -> bool:
        if not self.data_path or not self.data_path.exists():
            return False
        with open(self.data_path) as f:
            raw = json.load(f)
        blocks = [Block.from_dict(d) for d in raw]
        balances, nonces, _ = self.validate_full_chain(blocks)
        self.blocks, self.balances, self.nonces = blocks, balances, nonces
        return True

    def to_dict_list(self) -> list[dict]:
        with self.lock:
            return [b.to_dict() for b in self.blocks]
