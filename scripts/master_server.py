#!/usr/bin/env python3
"""Lightweight master server for the ServerBrowser gem (stdlib only).

Dedicated servers POST /announce heartbeats; game clients GET /servers.
Entries expire when a server stops announcing (default 90 s TTL).

Run on any host reachable by servers and players:

    python3 scripts/master_server.py --port 27900

Then on game servers:   sb_master_url http://your.host:27900
                        sb_announce true
And on clients:         sb_master_url http://your.host:27900
"""
import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY = 4096
MAX_SERVERS = 1000

lock = threading.Lock()
servers = {}  # (ip, port) -> {entry..., "expires": t}


def sanitize(text, limit=64):
    return str(text)[:limit]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _reply(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") != "/servers" and self.path != "/servers":
            self._reply(404, {"error": "not found"})
            return
        now = time.time()
        with lock:
            stale = [key for key, entry in servers.items() if entry["expires"] < now]
            for key in stale:
                del servers[key]
            result = [
                {k: entry[k] for k in ("name", "address", "port", "map", "players", "max_players")}
                for entry in servers.values()
            ]
        self._reply(200, result)

    def do_POST(self):
        if self.path.rstrip("/") != "/announce":
            self._reply(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > MAX_BODY:
            self._reply(400, {"error": "bad body"})
            return
        try:
            data = json.loads(self.rfile.read(length))
            port = int(data["port"])
            if not (0 < port < 65536):
                raise ValueError("port out of range")
        except (ValueError, KeyError, json.JSONDecodeError):
            self._reply(400, {"error": "invalid announce"})
            return
        address = self.client_address[0]
        entry = {
            "name": sanitize(data.get("name", "unnamed")),
            "address": address,
            "port": port,
            "map": sanitize(data.get("map", "")),
            "players": max(0, int(data.get("players", 0))),
            "max_players": max(0, int(data.get("max_players", 0))),
            "expires": time.time() + self.server.ttl,
        }
        with lock:
            if (address, port) not in servers and len(servers) >= MAX_SERVERS:
                self._reply(503, {"error": "list full"})
                return
            servers[(address, port)] = entry
        self._reply(200, {"ok": True})

    def log_message(self, fmt, *args):  # quiet
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0", help="bind address (default all interfaces)")
    parser.add_argument("--port", type=int, default=27900, help="HTTP port (default 27900)")
    parser.add_argument("--ttl", type=float, default=90.0, help="seconds before a silent server expires")
    args = parser.parse_args()

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    httpd.ttl = args.ttl
    print(f"master server listening on {args.host}:{args.port} (ttl {args.ttl:.0f}s)")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
