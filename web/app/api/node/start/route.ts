import { NextResponse } from "next/server";
import { startNetwork } from "../shared";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function POST() {
  try {
    const snapshot = await startNetwork();
    return NextResponse.json(snapshot);
  } catch (error) {
    return NextResponse.json({ error: error instanceof Error ? error.message : "Failed to start the network." }, { status: 500 });
  }
}
