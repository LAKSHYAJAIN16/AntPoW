"""Mining loop: search phase (ACO) -> quality-weighted target -> hash phase
(nonce grinding), matching the mechanism diagram in paper/antchain.tex
Section 3.6. Runs in a background thread and restarts whenever the chain tip
changes underneath it (another node found the block first).
"""
from __future__ import annotations

import threading
import time

from .block import Block
from .chain import Blockchain
from .consensus import quality_target
from .mempool import Mempool
from .tsp import AntColonyOptimizer, generate_instance, nearest_neighbor_tour, reference_length

MAX_ACO_ITERATIONS = 300
NONCE_CHECK_INTERVAL = 4000


class Miner:
    def __init__(self, chain: Blockchain, mempool: Mempool, miner_address: str,
                 on_block_found, log=print):
        self.chain = chain
        self.mempool = mempool
        self.miner_address = miner_address
        self.on_block_found = on_block_found
        self.log = log
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def _loop(self) -> None:
        while not self._stop.is_set():
            block = self._mine_one_block()
            if block is not None:
                self.on_block_found(block)

    def _tip_changed(self, prev_hash: str) -> bool:
        return self.chain.tip.hash != prev_hash or self._stop.is_set()

    def _mine_one_block(self) -> Block | None:
        params = self.chain.params
        prev = self.chain.tip
        prev_hash = prev.hash
        target = self.chain.next_target()

        instance = generate_instance(prev_hash, params.n_cities)
        f_ref = reference_length(instance)
        t_opt = params.t_opt_frac * f_ref

        aco = AntColonyOptimizer(instance)
        best_tour, best_length = None, float("inf")
        for _ in range(MAX_ACO_ITERATIONS):
            if self._tip_changed(prev_hash):
                return None
            best_tour, best_length = aco.run(1)
            if best_tour is not None and best_length <= t_opt:
                break

        if best_tour is None or best_length > t_opt:
            best_tour = nearest_neighbor_tour(instance)
            best_length = instance.tour_length(best_tour)

        required_target = quality_target(target, params.lam, f_ref, best_length)
        txs = self.mempool.select(self.chain, params.max_txs_per_block)

        nonce = 0
        checked = 0
        while True:
            checked += 1
            if checked % NONCE_CHECK_INTERVAL == 0 and self._tip_changed(prev_hash):
                return None
            block = Block(
                index=prev.index + 1,
                prev_hash=prev_hash,
                timestamp=time.time(),
                transactions=txs,
                tour=best_tour,
                f_x=best_length,
                f_ref=f_ref,
                miner_address=self.miner_address,
                reward=params.block_reward,
                nonce=nonce,
            )
            block.finalize()
            if int(block.hash, 16) < required_target:
                self.log(
                    f"[miner] found block {block.index} f(x)={best_length:.4f} "
                    f"f_ref={f_ref:.4f} nonce={nonce} hash={block.hash[:16]}..."
                )
                return block
            nonce += 1
