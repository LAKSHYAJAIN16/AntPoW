"use client";

import { FormEvent, useMemo, useState } from "react";

type Row = Record<string, string>;
type Config = { miners: number; blocks: number; cities: number; maxTicks: number; strategicFraction: number; lambdas: string; seed: number };
const defaults: Config = { miners: 30, blocks: 200, cities: 16, maxTicks: 40, strategicFraction: 0.15, lambdas: "0.5, 1, 3, 10", seed: 7 };
const numberFormat = new Intl.NumberFormat("en-US", { maximumFractionDigits: 3 });

function Field({ label, hint, value, onChange, step = 1 }: { label: string; hint: string; value: number; onChange: (value: number) => void; step?: number }) {
  return <label className="field"><span>{label}</span><input type="number" value={value} step={step} onChange={(event) => onChange(Number(event.target.value))} /><small>{hint}</small></label>;
}

function metric(row: Row, key: string) { return Number(row[key] ?? 0); }
function display(value: number, suffix = "") { return `${numberFormat.format(value)}${suffix}`; }

export default function Workbench() {
  const [config, setConfig] = useState(defaults);
  const [rows, setRows] = useState<Row[]>([]);
  const [log, setLog] = useState("");
  const [error, setError] = useState("");
  const [running, setRunning] = useState(false);
  const maxGini = useMemo(() => Math.max(...rows.map((row) => metric(row, "gini_rewards")), 0.01), [rows]);
  const update = <K extends keyof Config>(key: K, value: Config[K]) => setConfig((current) => ({ ...current, [key]: value }));

  async function run(event: FormEvent) {
    event.preventDefault(); setRunning(true); setError(""); setLog("");
    const lambdas = config.lambdas.split(",").map((value) => Number(value.trim())).filter(Number.isFinite);
    try {
      const response = await fetch("/api/run", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ ...config, lambdas }) });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error ?? "Run failed.");
      setRows(data.summary); setLog(data.log);
    } catch (reason) { setError(reason instanceof Error ? reason.message : "Run failed."); }
    finally { setRunning(false); }
  }

  return <main>
    <header className="masthead"><div className="brand"><span className="ant-mark" aria-hidden="true">⌁</span><div><p>AntChain / C++ research simulator</p><h1>Experiment workbench</h1></div></div><span className="status"><i /> Local execution</span></header>
    <section className="intro"><p className="kicker">Useful proof-of-work, inspected</p><h2>Measure the trade-off between optimization, fairness, and consensus stability.</h2><p>Configure an experiment, then compile and run the dependency-free C++ simulator on this machine. Results stay local and are written under <code>web/results</code>.</p></section>
    <div className="workspace">
      <form className="bench" onSubmit={run}>
        <div className="bench-head"><div><p className="kicker">Run configuration</p><h2>Set the colony</h2></div><button className="reset" type="button" onClick={() => setConfig(defaults)}>Reset</button></div>
        <div className="fields">
          <Field label="Miners" hint="Population size" value={config.miners} onChange={(value) => update("miners", value)} />
          <Field label="Blocks" hint="Per condition" value={config.blocks} onChange={(value) => update("blocks", value)} />
          <Field label="Cities" hint="TSP instance size" value={config.cities} onChange={(value) => update("cities", value)} />
          <Field label="Max ticks" hint="Fallback horizon" value={config.maxTicks} onChange={(value) => update("maxTicks", value)} />
          <Field label="Strategic share" hint="0–1 of population" value={config.strategicFraction} step={0.01} onChange={(value) => update("strategicFraction", value)} />
          <Field label="Seed" hint="Reproducible run" value={config.seed} onChange={(value) => update("seed", value)} />
          <label className="field full"><span>Hybrid λ sweep</span><input value={config.lambdas} onChange={(event) => update("lambdas", event.target.value)} /><small>Comma-separated quality-weight multipliers</small></label>
        </div>
        <button className="run" disabled={running}>{running ? <><b className="spinner" /> Running experiment…</> : "Run C++ experiment"}</button>
        <p className="run-note">The first run builds <code>cpp/</code> with CMake. Subsequent runs reuse the executable.</p>
      </form>
      <aside className="reading"><p className="kicker">Reading the board</p><dl><div><dt>Reward Gini</dt><dd>How unevenly blocks are distributed. Lower is fairer.</dd></div><div><dt>Fork rate</dt><dd>How often simultaneous candidates compete. Lower is steadier.</dd></div><div><dt>Useful work</dt><dd>TSP improvement generated per energy proxy.</dd></div></dl><p className="caveat">This is a research simulator—not the networked toy cryptocurrency in <code>node/</code>.</p></aside>
    </div>
    {error && <section className="notice error"><strong>Run unavailable.</strong><pre>{error}</pre></section>}
    {rows.length > 0 && <section className="results" aria-live="polite"><div className="results-head"><div><p className="kicker">Latest run</p><h2>Six mechanisms, one shared workload</h2></div><span>{rows.length} conditions complete</span></div><div className="result-grid">{rows.map((row) => <article className="result" key={row.label}><div className="result-title"><h3>{row.label.replace("_", " ")}</h3><span className={row.mode}>{row.mode === "hybrid" ? "hybrid" : row.mode === "sha_pow" ? "SHA-PoW" : "pure PoAO"}</span></div><div className="gini"><div><span>Reward Gini</span><b>{display(metric(row, "gini_rewards"))}</b></div><div className="bar"><i style={{ width: `${Math.max(4, metric(row, "gini_rewards") / maxGini * 100)}%` }} /></div></div><dl className="metrics"><div><dt>Fork rate</dt><dd>{display(metric(row, "fork_rate") * 100, "%")}</dd></div><div><dt>Mean block time</dt><dd>{display(metric(row, "mean_block_time"))} ticks</dd></div><div><dt>Strategic advantage</dt><dd>{display(metric(row, "strategic_advantage"))}×</dd></div><div><dt>Useful / joule</dt><dd>{metric(row, "useful_improvement_per_joule").toExponential(2)}</dd></div></dl></article>)}</div><details><summary>Execution log</summary><pre>{log}</pre></details></section>}
  </main>;
}
