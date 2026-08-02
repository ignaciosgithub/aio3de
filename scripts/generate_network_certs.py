#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""Generates an RSA key pair + self-signed certificate for AzNetworking DTLS/TLS.

Produces serverkey.pem (private key: server only, never commit or ship to
clients) and servercert.pem (public certificate: deploy to server and clients
for pinning). See docs/aio3de/SECURE_NETWORKING.md for the cvar setup.

Usage:
    python scripts/generate_network_certs.py --out certs/ [--cn myserver] [--days 365] [--bits 2048]

Requires the `openssl` command-line tool (bundled with Git for Windows).
"""

import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default="certs", help="output directory (default: certs)")
    parser.add_argument("--cn", default="gameserver", help="certificate common name (default: gameserver)")
    parser.add_argument("--days", type=int, default=365, help="validity in days (default: 365)")
    parser.add_argument("--bits", type=int, default=2048, help="RSA key size (default: 2048)")
    args = parser.parse_args()

    openssl = shutil.which("openssl")
    if not openssl:
        print("ERROR: `openssl` not found on PATH.")
        print("On Windows it ships with Git: try C:\\Program Files\\Git\\usr\\bin\\openssl.exe")
        return 1

    os.makedirs(args.out, exist_ok=True)
    key_path = os.path.join(args.out, "serverkey.pem")
    cert_path = os.path.join(args.out, "servercert.pem")
    for path in (key_path, cert_path):
        if os.path.exists(path):
            print(f"ERROR: {path} already exists - refusing to overwrite. Delete it first to rotate.")
            return 1

    cmd = [
        openssl, "req", "-x509",
        "-newkey", f"rsa:{args.bits}",
        "-keyout", key_path,
        "-out", cert_path,
        "-days", str(args.days),
        "-nodes",
        "-subj", f"/CN={args.cn}",
    ]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        return result.returncode

    print()
    print(f"Wrote {key_path}  (RSA private key: SERVER ONLY - never commit, never ship to clients)")
    print(f"Wrote {cert_path} (public certificate: deploy to server AND clients for pinning)")
    print()
    print("Server cfg:  net_UdpUseEncryption true")
    print("             net_SslExternalCertificateFile <path in asset cache>/servercert.pem")
    print("             net_SslExternalPrivateKeyFile  <path in asset cache>/serverkey.pem")
    print("Client cfg:  net_UdpUseEncryption true")
    print("             net_SslExternalCertificateFile <path in asset cache>/servercert.pem")
    return 0


if __name__ == "__main__":
    sys.exit(main())
