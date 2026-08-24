import { ChildProcess, execFile, spawn } from "child_process";
import { existsSync } from "fs";
import { mkdir, readFile } from "fs/promises";
import net from "net";
import path from "path";
import { promisify } from "util";

const exec = promisify(execFile);

const repositoryRoot = path.resolve(process.cwd(), "..");
const cppNodeRoot = path.join(repositoryRoot, "cpp_node");
const buildRoot = path.join(cppNodeRoot, "build");

const NODE1_PORT = 17331;
const NODE2_PORT = 17332;

export type NodeInfo = { port: number; address: string };
export type NetworkSnapshot = { running: boolean; node1: NodeInfo | null; node2: NodeInfo | null; runDir: string | null };

type RunningNetwork = {
  proc1: ChildProcess;
  proc2: ChildProcess;
  node1: NodeInfo;
  node2: NodeInfo;
  runDir: string;
};

// A plain module-scope singleton is enough here: this workbench runs as a
// single local `next dev` process (same assumption the existing /api/run
// route makes -- see web/README.md's "Local execution" framing), not a
// multi-worker deployment.
const globalState = globalThis as unknown as { __antchainNetwork?: RunningNetwork | null };
if (globalState.__antchainNetwork === undefined) globalState.__antchainNetwork = null;

function getNetwork(): RunningNetwork | null {
  return globalState.__antchainNetwork ?? null;
}
function setNetwork(n: RunningNetwork | null) {
  globalState.__antchainNetwork = n;
}

async function command(file: string, args: string[], cwd: string) {
  try {
    return await exec(file, args, { cwd, timeout: 120_000, windowsHide: true, maxBuffer: 1024 * 1024 });
  } catch (error) {
    const detail = error as { stderr?: string; stdout?: string; message?: string };
    throw new Error([detail.message, detail.stdout, detail.stderr].filter(Boolean).join("\n"));
  }
}

export async function resolveExecutable(): Promise<string> {
  const executableName = process.platform === "win32" ? "antchain_node.exe" : "antchain_node";
  const candidates = [path.join(buildRoot, "Release", executableName), path.join(buildRoot, executableName)];
  let executable = candidates.find(existsSync);
  if (!executable) {
    await command("cmake", ["-B", "build", "-DCMAKE_BUILD_TYPE=Release"], cppNodeRoot);
    await command("cmake", ["--build", "build", "--config", "Release"], cppNodeRoot);
    executable = candidates.find(existsSync);
  }
  if (!executable) throw new Error("CMake completed but antchain_node was not found.");
  return executable;
}

async function makeWallet(executable: string, cwd: string, outFile: string): Promise<string> {
  await command(executable, ["wallet-new", "--out", outFile], cwd);
  const wallet = JSON.parse(await readFile(path.join(cwd, outFile), "utf8"));
  return wallet.address as string;
}

// Speaks the same newline-delimited JSON protocol as cpp_node/src/rpc.cpp,
// directly from Node instead of shelling out to the CLI -- much cheaper for
// frequent status polling from the browser.
export function rpcRequest(port: number, msg: Record<string, unknown>, timeoutMs = 3000): Promise<any> {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: "127.0.0.1", port });
    let buffer = "";
    const timer = setTimeout(() => {
      socket.destroy();
      reject(new Error("RPC timed out"));
    }, timeoutMs);
    socket.on("connect", () => socket.write(JSON.stringify(msg) + "\n"));
    socket.on("data", (chunk) => {
      buffer += chunk.toString("utf8");
      const nl = buffer.indexOf("\n");
      if (nl !== -1) {
        clearTimeout(timer);
        socket.end();
        try {
          resolve(JSON.parse(buffer.slice(0, nl)));
        } catch (e) {
          reject(e);
        }
      }
    });
    socket.on("error", (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });
}

async function waitUntilReady(port: number, attempts = 20, delayMs = 250): Promise<void> {
  for (let i = 0; i < attempts; ++i) {
    try {
      await rpcRequest(port, { type: "get_status" }, 800);
      return;
    } catch {
      await new Promise((r) => setTimeout(r, delayMs));
    }
  }
  throw new Error(`node on port ${port} did not become ready`);
}

export async function startNetwork(): Promise<NetworkSnapshot> {
  const existing = getNetwork();
  if (existing) return snapshot();

  const executable = await resolveExecutable();
  const runDir = path.join(repositoryRoot, "web", "results", `node-network-${Date.now()}`);
  await mkdir(runDir, { recursive: true });

  const address1 = await makeWallet(executable, runDir, "wallet1.json");
  const address2 = await makeWallet(executable, runDir, "wallet2.json");

  const proc1 = spawn(
    executable,
    [
      "run", "--port", String(NODE1_PORT), "--data", path.join(runDir, "chain1.json"),
      "--wallet", path.join(runDir, "wallet1.json"), "--mine",
      "--difficulty-bits", "16", "--cities", "8",
    ],
    { cwd: runDir, windowsHide: true }
  );

  try {
    await waitUntilReady(NODE1_PORT);
  } catch (e) {
    proc1.kill();
    throw e;
  }

  const proc2 = spawn(
    executable,
    [
      "run", "--port", String(NODE2_PORT), "--peers", `127.0.0.1:${NODE1_PORT}`,
      "--data", path.join(runDir, "chain2.json"), "--difficulty-bits", "16", "--cities", "8",
    ],
    { cwd: runDir, windowsHide: true }
  );

  try {
    await waitUntilReady(NODE2_PORT);
  } catch (e) {
    proc1.kill();
    proc2.kill();
    throw e;
  }

  const network: RunningNetwork = {
    proc1, proc2, runDir,
    node1: { port: NODE1_PORT, address: address1 },
    node2: { port: NODE2_PORT, address: address2 },
  };
  setNetwork(network);
  return snapshot();
}

export function snapshot(): NetworkSnapshot {
  const n = getNetwork();
  if (!n) return { running: false, node1: null, node2: null, runDir: null };
  return { running: true, node1: n.node1, node2: n.node2, runDir: n.runDir };
}

export async function stopNetwork(): Promise<void> {
  const n = getNetwork();
  if (!n) return;
  n.proc1.kill();
  n.proc2.kill();
  setNetwork(null);
}

export function requireRunning(): RunningNetwork {
  const n = getNetwork();
  if (!n) throw new Error("network is not running");
  return n;
}

// Shells out to the CLI's `send` command rather than reimplementing Ed25519
// signing here -- it already looks up the current nonce, signs, and submits
// in one step (see cpp_node/src/main.cpp::cmd_send).
export async function sendFunds(amount: number): Promise<{ accepted: boolean; raw: string }> {
  const n = requireRunning();
  const executable = await resolveExecutable();
  const { stdout, stderr } = await command(
    executable,
    [
      "send", "--node", `127.0.0.1:${n.node1.port}`,
      "--wallet", path.join(n.runDir, "wallet1.json"),
      "--to", n.node2.address, "--amount", String(amount),
    ],
    n.runDir
  );
  const raw = `${stdout}${stderr}`;
  return { accepted: /^Submitted tx/m.test(stdout), raw };
}
