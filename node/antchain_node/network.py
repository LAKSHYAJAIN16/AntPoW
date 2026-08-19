"""Minimal P2P layer: TCP sockets, newline-delimited JSON messages, flood
gossip. No peer discovery beyond the static --peers list, no NAT traversal,
no encryption -- adequate for running a handful of nodes on localhost or a
LAN, not for a public network."""
from __future__ import annotations

import json
import socket
import threading
from typing import Callable

Handler = Callable[[socket.socket, dict], None]


class Network:
    def __init__(self, host: str, port: int, on_message: Handler, log=print):
        self.host = host
        self.port = port
        self.on_message = on_message
        self.log = log
        self.peers: dict[str, socket.socket] = {}
        self.lock = threading.RLock()
        self._server_sock: socket.socket | None = None

    def start(self) -> None:
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((self.host, self.port))
        self._server_sock.listen(16)
        threading.Thread(target=self._accept_loop, daemon=True).start()
        self.log(f"[net] listening on {self.host}:{self.port}")

    def _accept_loop(self) -> None:
        while True:
            try:
                conn, addr = self._server_sock.accept()
            except OSError:
                return
            threading.Thread(target=self._handle_conn, args=(conn, None), daemon=True).start()

    def connect_to(self, host: str, port: int) -> bool:
        key = f"{host}:{port}"
        with self.lock:
            if key in self.peers:
                return True
        try:
            sock = socket.create_connection((host, port), timeout=5)
            sock.settimeout(None)  # create_connection() leaves its connect-timeout
            # applied to the socket; without resetting it, any read that goes
            # quiet for 5s (e.g. no new blocks/txs) raises TimeoutError and the
            # peer connection is silently dropped.
        except OSError as e:
            self.log(f"[net] connect to {key} failed: {e}")
            return False
        with self.lock:
            self.peers[key] = sock
        threading.Thread(target=self._handle_conn, args=(sock, key), daemon=True).start()
        return True

    def _remove_peer(self, sock: socket.socket) -> None:
        with self.lock:
            keys = [k for k, s in self.peers.items() if s is sock]
            for k in keys:
                del self.peers[k]
        try:
            sock.close()
        except OSError:
            pass

    def _handle_conn(self, sock: socket.socket, key: str | None) -> None:
        f = sock.makefile("r", encoding="utf-8")
        try:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue
                try:
                    if key is None and msg.get("type") == "hello" and "port" in msg:
                        key = f"{msg.get('host', sock.getpeername()[0])}:{msg['port']}"
                        with self.lock:
                            self.peers[key] = sock
                    self.on_message(sock, msg)
                except Exception as e:
                    # A single malformed/unexpected message must not kill the
                    # whole connection -- log and keep reading.
                    self.log(f"[net] error handling message {msg.get('type')!r} from {key}: {e!r}")
        except OSError as e:
            self.log(f"[net] connection to {key} dropped: {e!r}")
        finally:
            self._remove_peer(sock)

    def send(self, sock: socket.socket, msg: dict) -> bool:
        try:
            sock.sendall((json.dumps(msg) + "\n").encode("utf-8"))
            return True
        except OSError:
            self._remove_peer(sock)
            return False

    def broadcast(self, msg: dict, exclude: socket.socket | None = None) -> None:
        with self.lock:
            targets = [s for s in self.peers.values() if s is not exclude]
        for s in targets:
            self.send(s, msg)
