import { execFile } from "child_process";
import { mkdir, readFile } from "fs/promises";
import { existsSync } from "fs";
import { NextResponse } from "next/server";
import path from "path";
import { promisify } from "util";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const exec = promisify(execFile);
const repositoryRoot = path.resolve(process.cwd(), "..");
const cppRoot = path.join(repositoryRoot, "cpp");
const buildRoot = path.join(cppRoot, "build");

type RunInput = { miners: number; blocks: number; cities: number; maxTicks: number; strategicFraction: number; lambdas: number[]; seed: number };

function finiteBetween(value: unknown, minimum: number, maximum: number) {
  return typeof value === "number" && Number.isFinite(value) && value >= minimum && value <= maximum;
}

function validate(body: unknown): RunInput | null {
  if (!body || typeof body !== "object") return null;
  const b = body as Record<string, unknown>;
  if (!finiteBetween(b.miners, 2, 1000) || !finiteBetween(b.blocks, 1, 10000) || !finiteBetween(b.cities, 4, 100) ||
      !finiteBetween(b.maxTicks, 1, 500) || !finiteBetween(b.strategicFraction, 0, 1) || !finiteBetween(b.seed, 0, 4294967295) ||
      !Array.isArray(b.lambdas) || b.lambdas.length < 1 || b.lambdas.length > 8 || !b.lambdas.every((l) => finiteBetween(l, 0.01, 100))) return null;
  return b as RunInput;
}

function parseCsv(source: string) {
  const [header, ...rows] = source.trim().split(/\r?\n/);
  const keys = header.split(",");
  return rows.filter(Boolean).map((line) => Object.fromEntries(keys.map((key, index) => [key, line.split(",")[index]])));
}

async function command(file: string, args: string[], cwd: string) {
  try {
    return await exec(file, args, { cwd, timeout: 120_000, windowsHide: true, maxBuffer: 1024 * 1024 });
  } catch (error) {
    const detail = error as { stderr?: string; stdout?: string; message?: string };
    throw new Error([detail.message, detail.stdout, detail.stderr].filter(Boolean).join("\n"));
  }
}

export async function POST(request: Request) {
  const input = validate(await request.json().catch(() => null));
  if (!input) return NextResponse.json({ error: "Invalid run configuration." }, { status: 400 });
  const runId = `run-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  const outputDir = path.join(repositoryRoot, "web", "results", runId);
  try {
    await mkdir(outputDir, { recursive: true });
    const executable = process.platform === "win32" ? path.join(buildRoot, "Release", "antchain_experiment.exe") : path.join(buildRoot, "antchain_experiment");
    if (!existsSync(executable)) {
      await command("cmake", ["-B", "build", "-DCMAKE_BUILD_TYPE=Release"], cppRoot);
      await command("cmake", ["--build", "build", "--config", "Release"], cppRoot);
    }
    const args = ["--miners", String(input.miners), "--blocks", String(input.blocks), "--cities", String(input.cities), "--max-ticks", String(input.maxTicks), "--strategic-fraction", String(input.strategicFraction), "--seed", String(input.seed), "--outdir", outputDir, "--lambdas", ...input.lambdas.map(String)];
    const { stdout, stderr } = await command(executable, args, cppRoot);
    const summary = parseCsv(await readFile(path.join(outputDir, "cpp_summary.csv"), "utf8"));
    return NextResponse.json({ runId, summary, log: `${stdout}${stderr ? `\n${stderr}` : ""}` });
  } catch (error) {
    return NextResponse.json({ error: error instanceof Error ? error.message : "The simulator could not be run." }, { status: 500 });
  }
}
