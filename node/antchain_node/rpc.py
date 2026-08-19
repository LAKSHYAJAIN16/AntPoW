"""One-shot request/response helper for CLI commands (send/balance/status)
talking to an already-running node over the same JSON-lines protocol used
for peer gossip."""
from __future__ import annotations

import json
import socket


def request(host: str, port: int, msg: dict, timeout: float = 5.0) -> dict:
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall((json.dumps(msg) + "\n").encode("utf-8"))
        f = sock.makefile("r", encoding="utf-8")
        line = f.readline()
        if not line:
            raise ConnectionError("no response from node")
        return json.loads(line)
