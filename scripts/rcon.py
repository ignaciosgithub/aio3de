"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""

#!/usr/bin/env python3
"""
Remote admin (rcon) client for aio3de dedicated servers running the
ServerAdmin gem.

Authentication is an HMAC-SHA256 challenge-response: the server sends a
random nonce, the client answers with HMAC(password, nonce) - the password
never crosses the wire.

Usage:
    # interactive console
    python scripts/rcon.py --host 203.0.113.7 --port 33470

    # one-shot command
    python scripts/rcon.py --host 203.0.113.7 -c "sv_terminate"

The password is read from the AIO3DE_RCON_PASSWORD environment variable or
prompted for interactively (never pass it on the command line - it would
land in the shell history and process list).
"""
import argparse
import getpass
import hashlib
import hmac
import os
import socket
import sys

END_MARKER = "<<<END>>>"


def read_line(sock_file):
    line = sock_file.readline()
    if not line:
        raise ConnectionError("server closed the connection")
    return line.rstrip("\r\n")


def connect(host, port, password):
    sock = socket.create_connection((host, port), timeout=10)
    sock_file = sock.makefile("rw", encoding="utf-8", newline="\n")

    challenge = read_line(sock_file)
    if not challenge.startswith("CHALLENGE "):
        raise ProtocolError(f"unexpected greeting: {challenge!r}")
    nonce = challenge.split(" ", 1)[1]

    tag = hmac.new(password.encode(), nonce.encode(), hashlib.sha256).hexdigest()
    sock_file.write(f"AUTH {tag}\n")
    sock_file.flush()

    verdict = read_line(sock_file)
    if verdict != "OK":
        raise PermissionError("authentication rejected (wrong password?)")
    return sock, sock_file


class ProtocolError(Exception):
    pass


def run_command(sock_file, command):
    sock_file.write(command + "\n")
    sock_file.flush()
    while True:
        line = read_line(sock_file)
        if line == END_MARKER:
            return
        print(line)


def main():
    parser = argparse.ArgumentParser(description="aio3de remote admin (rcon) client")
    parser.add_argument("--host", required=True, help="server address")
    parser.add_argument("--port", type=int, default=33470, help="admin port (default 33470)")
    parser.add_argument("-c", "--command", action="append",
                        help="run this command and exit (repeatable)")
    args = parser.parse_args()

    password = os.environ.get("AIO3DE_RCON_PASSWORD") or getpass.getpass("rcon password: ")

    try:
        sock, sock_file = connect(args.host, args.port, password)
    except (OSError, ProtocolError, PermissionError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    try:
        if args.command:
            for command in args.command:
                run_command(sock_file, command)
        else:
            print("connected - type console commands ('quit' to exit)")
            while True:
                try:
                    command = input("rcon> ").strip()
                except (EOFError, KeyboardInterrupt):
                    break
                if not command:
                    continue
                if command in ("quit", "exit"):
                    break
                run_command(sock_file, command)
    except ConnectionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
