import { NextResponse } from "next/server";
import { snapshot, stopNetwork } from "../shared";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function POST() {
  await stopNetwork();
  return NextResponse.json(snapshot());
}
