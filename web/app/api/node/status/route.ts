import { NextResponse } from "next/server";
import { rpcRequest, snapshot } from "../shared";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

export async function GET() {
  const current = snapshot();
  if (!current.running || !current.node1 || !current.node2) {
    return NextResponse.json(current);
  }
  try {
    const [status1, status2, balance1, balance2] = await Promise.all([
      rpcRequest(current.node1.port, { type: "get_status" }),
      rpcRequest(current.node2.port, { type: "get_status" }),
      rpcRequest(current.node1.port, { type: "get_balance", address: current.node1.address }),
      rpcRequest(current.node2.port, { type: "get_balance", address: current.node2.address }),
    ]);
    return NextResponse.json({
      running: true,
      node1: { ...current.node1, height: status1.height, tip: status1.tip, mempool_size: status1.mempool_size, peers: status1.peers, balance: balance1.balance },
      node2: { ...current.node2, height: status2.height, tip: status2.tip, mempool_size: status2.mempool_size, peers: status2.peers, balance: balance2.balance },
    });
  } catch (error) {
    return NextResponse.json({ error: error instanceof Error ? error.message : "Status query failed." }, { status: 500 });
  }
}
