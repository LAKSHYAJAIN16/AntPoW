"use client";

import { useEffect, useRef, useState } from "react";
import Link from "next/link";

type NodeStatus = {
  port: number;
  address: string;
  height?: number;
  tip?: string;
  mempool_size?: number;
  peers?: string[];
  balance?: number;
};
type Snapshot = { running: boolean; node1: NodeStatus | null; node2: NodeStatus | null };

const empty: Snapshot = { running: false, node1: null, node2: null };

function NodeCard({ title, info }: { title: string; info: NodeStatus }) {
  return (
    <article className="result">
      <div className="result-title">
        <h3>{title}</h3>
      </div>
      <div className="gini">
        <div>
          <span>Balance</span>
          <b>{info.balance ?? 0}</b>
        </div>
      </div>
      <dl className="metrics">
        <div><dt>Height</dt><dd>{info.height ?? "…"}</dd></div>
        <div><dt>Peers</dt><dd>{info.peers?.length ?? 0}</dd></div>
        <div><dt>Mempool</dt><dd>{info.mempool_size ?? 0}</dd></div>
        <div><dt>Tip</dt><dd>{info.tip ? `${info.tip.slice(0, 10)}…` : "…"}</dd></div>
      </dl>
      <p className="addr">{info.address}</p>
    </article>
  );
}

export default function LiveNetwork() {
  const [snap, setSnap] = useState<Snapshot>(empty);
  const [starting, setStarting] = useState(false);
  const [stopping, setStopping] = useState(false);
  const [sending, setSending] = useState(false);
  const [error, setError] = useState("");
  const [sendResult, setSendResult] = useState("");
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  async function poll() {
    try {
      const response = await fetch("/api/node/status");
      const data = await response.json();
      if (data.error) return;
      setSnap(data);
    } catch {
      // Transient poll failures are expected while a node process is
      // starting up or shutting down -- next tick will recover.
    }
  }

  useEffect(() => {
    poll();
    pollRef.current = setInterval(poll, 2000);
    return () => {
      if (pollRef.current) clearInterval(pollRef.current);
    };
  }, []);

  async function start() {
    setStarting(true);
    setError("");
    try {
      const response = await fetch("/api/node/start", { method: "POST" });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error ?? "Failed to start the network.");
      setSnap(data);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Failed to start the network.");
    } finally {
      setStarting(false);
    }
  }

  async function stop() {
    setStopping(true);
    try {
      await fetch("/api/node/stop", { method: "POST" });
    } finally {
      setSnap(empty);
      setSendResult("");
      setStopping(false);
    }
  }

  async function send() {
    setSending(true);
    setError("");
    try {
      const response = await fetch("/api/node/send", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ amount: 25 }),
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error ?? "Send failed.");
      setSendResult(data.raw ?? "Submitted.");
      poll();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Send failed.");
    } finally {
      setSending(false);
    }
  }

  const ready = snap.running && snap.node1 && snap.node2;

  return (
    <main>
      <header className="masthead">
        <div className="brand">
          <span className="ant-mark" aria-hidden="true">⌁</span>
          <div>
            <p>AntChain / C++ node network</p>
            <h1>Live network</h1>
          </div>
        </div>
        <div className="mast-actions">
          <nav aria-label="Primary navigation">
            <Link href="/">Workbench</Link>
            <Link href="/how-it-works">How it works</Link>
            <Link href="/network" aria-current="page">Live network</Link>
          </nav>
          <span className="status"><i /> Local execution</span>
        </div>
      </header>

      <section className="intro">
        <p className="kicker">The real thing, not the simulator</p>
        <h2>Watch two actual AntChain nodes mine, gossip, and agree.</h2>
        <p>
          This runs <code>cpp_node/</code> — a real, runnable node with Ed25519-signed
          transactions, real proof-of-work hashing, and a TCP gossip network — not the
          probabilistic simulator on the <Link href="/">Workbench</Link> page. Node&nbsp;1
          mines to its own wallet; node&nbsp;2 only peers with it and independently
          validates every block it receives.
        </p>
      </section>

      <div className="workspace">
        <div className="bench">
          <div className="bench-head">
            <div>
              <p className="kicker">Two local processes</p>
              <h2>Node control</h2>
            </div>
            {ready ? (
              <button className="reset" type="button" onClick={stop} disabled={stopping}>
                {stopping ? "Stopping…" : "Stop network"}
              </button>
            ) : null}
          </div>

          {ready && snap.node1 && snap.node2 ? (
            <>
              <div className="result-grid node-grid">
                <NodeCard title="Node 1 · mining" info={snap.node1} />
                <NodeCard title="Node 2 · peer" info={snap.node2} />
              </div>
              <button className="run" type="button" onClick={send} disabled={sending}>
                {sending ? <><b className="spinner" /> Sending…</> : "Send 25 from node 1 → node 2"}
              </button>
              <p className="run-note">
                Watch both balances and both heights — once the transaction is mined into a
                block on node 1 and gossiped over, node 2&apos;s independently-recomputed
                balance should match exactly.
              </p>
              {sendResult && <details open><summary>Last send result</summary><pre>{sendResult}</pre></details>}
            </>
          ) : (
            <>
              <p className="run-note">
                The first start builds <code>cpp_node/</code> with CMake, generates two fresh
                wallets, and launches two <code>antchain_node</code> processes on localhost
                (ports 17331/17332). Stopping tears both down; state doesn&apos;t persist
                across restarts of this dev server.
              </p>
              <button className="run" type="button" onClick={start} disabled={starting}>
                {starting ? <><b className="spinner" /> Starting network…</> : "Start network"}
              </button>
            </>
          )}
        </div>
        <aside className="reading">
          <p className="kicker">What you&apos;re seeing</p>
          <dl>
            <div><dt>Height</dt><dd>Blocks each node has independently validated and accepted.</dd></div>
            <div><dt>Peers</dt><dd>Live TCP gossip connections this node currently holds.</dd></div>
            <div><dt>Mempool</dt><dd>Signed transactions waiting to be mined into a block.</dd></div>
          </dl>
          <p className="caveat">
            Real Ed25519 signatures and BLAKE2b proof-of-work hashing — see{" "}
            <code>cpp_node/README.md</code> for exactly how this diverges from{" "}
            <code>node/</code>&apos;s Python implementation and why.
          </p>
        </aside>
      </div>

      {error && (
        <section className="notice error">
          <strong>Something went wrong.</strong>
          <pre>{error}</pre>
        </section>
      )}
    </main>
  );
}
