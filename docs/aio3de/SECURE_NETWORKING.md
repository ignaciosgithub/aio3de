# Secure Networking: RSA handshakes and tamper-resistant traffic

How to harden client↔server traffic for a networked game (e.g. the
[ArenaShooterNet](../../Gems/ArenaShooterNet/README.md) arena shooter) so that
data between user and server is hard to tamper with, replay, or spoof.

## The two layers of anti-cheat

1. **Server authority (mandatory)** — encryption alone does not stop cheating:
   a hacked client can still *send* dishonest data over a perfectly encrypted
   channel. The ArenaShooterNet gem therefore keeps all gameplay state on the
   server: clients send only inputs; positions, shots, damage, health, fire
   rates are computed and validated server-side.
2. **Transport hardening (this document)** — makes the wire itself
   tamper-proof: an attacker between client and server (or a tool modifying
   packets in flight) cannot read, alter, or replay traffic.

## What the engine ships

AzNetworking has built-in **DTLS** (UDP, used by the Multiplayer gem) and
**TLS** (TCP) via OpenSSL. The default cipher suite is
`ECDHE-RSA-AES256-GCM-SHA384`, which gives exactly the "RSA handshake"
pattern done right:

- **RSA** authenticates the handshake: the server proves its identity by
  signing the key exchange with its RSA private key; clients verify against
  the server's certificate. RSA is *not* used to encrypt game packets —
  that would be slow and malleable.
- **ECDHE** derives a fresh per-session symmetric key (forward secrecy —
  captured traffic can't be decrypted later even if the RSA key leaks).
- **AES-256-GCM** encrypts *and authenticates* every packet: any bit flipped
  in transit fails authentication and the packet is dropped.
- **DTLS sequence windows** reject replayed packets.
- **Certificate pinning** (`net_SslEnablePinning`, on by default) requires the
  remote endpoint's public certificate to exactly match the local copy —
  a man-in-the-middle with a different (even validly signed) cert is refused.

## Setup

### 1. Generate an RSA key pair + certificate

Run the helper (requires the `openssl` CLI, bundled with Git for Windows):

```
python scripts/generate_network_certs.py --out certs/
```

or by hand:

```
openssl req -x509 -newkey rsa:2048 -keyout serverkey.pem -out servercert.pem \
        -days 365 -nodes -subj "/CN=myserver"
```

This produces:
- `serverkey.pem` — the RSA **private key**. Deploy to the **server only**.
  Never commit it or ship it to clients.
- `servercert.pem` — the public certificate. Deploy to server **and** clients
  (clients need it for pinning).

Place the files where the launcher can resolve them — paths are resolved
relative to the project's asset cache (`@products@`), so put them in your
project's asset folder (e.g. `<project>/Certificates/`) and let the Asset
Processor copy them.

### 2. Enable encryption via cvars

In the server's `server.cfg` (before `host`):

```
net_UdpUseEncryption true
net_SslExternalCertificateFile Certificates/servercert.pem
net_SslExternalPrivateKeyFile Certificates/serverkey.pem
host
```

In the client's `client.cfg` (before `connect`):

```
net_UdpUseEncryption true
net_SslExternalCertificateFile Certificates/servercert.pem
connect <server ip>:33450
```

Both sides must agree on `net_UdpUseEncryption` — an encrypted server rejects
plaintext clients and vice versa.

### 3. Useful hardening cvars

| cvar | default | meaning |
| --- | --- | --- |
| `net_SslEnablePinning` | true | remote cert must byte-match the local copy |
| `net_SslValidateExpiry` | true | reject expired certificates |
| `net_SslAllowSelfSigned` | true | accept self-signed certs that are otherwise trusted (fine for pinned game servers; set false if you use a CA) |
| `net_SslMaxCertDepth` | 3 | max chain depth |
| `net_SslCertCiphers` | ECDHE-RSA-AES256-GCM-SHA384 | key-exchange/cipher suite |
| `net_RotateCookieTimer` | 50ms | DTLS cookie rotation (DDoS/spoof mitigation during handshake) |

For server-to-server links use the equivalent `net_SslInternal*` cvars.

## Threat model summary

| Attack | Mitigation |
| --- | --- |
| Reading traffic (sniffing aim/positions of others) | AES-256-GCM encryption |
| Modifying packets in flight | GCM authentication tag — tampered packets dropped |
| Replaying old packets (e.g. repeated "shoot") | DTLS anti-replay window |
| Impersonating the server | RSA cert signature + pinning |
| Decrypting captured traffic later | ECDHE forward secrecy |
| Hacked client sending dishonest but well-formed data | **not solvable by crypto** — server authority (ArenaShooterNet) validates inputs, fire rates, hits server-side |
| Hooked client keeping the wire valid while lying locally | audit challenges (below) — detection, not prevention |

## Audit challenges (detection layer)

Even with an authenticated transport, a skilled attacker can hook the game
*above* the encryption layer and let the legitimate client produce valid
packets. Encryption cannot prove an untrusted client is honest — so the
ArenaShooterNet gem adds an audit layer on top of server authority:

```
audit request  (server → client, reliable):
    challenge_id | random_nonce            + response deadline

audit response (client → server, reliable):
    challenge_id | position | health | rolling_input_hash
    | HMAC-SHA256(session_audit_key,
                  challenge_id || nonce || position || health || input_hash)
```

- The per-session audit key comes from the OS CSPRNG and is delivered over
  the DTLS-protected reliable channel at spawn.
- Challenges arrive at unpredictable random intervals; the response must
  echo the fresh nonce, so answers cannot be precomputed or replayed.
- The server verifies the HMAC in constant time, then checks the reported
  state against its own authoritative simulation.
- Failed, missed, or divergent audits accumulate strikes; enough strikes
  disconnects the client.

Per-packet integrity, sequence numbers, the replay window, and session-key
rotation for the *gameplay* traffic itself are already provided by DTLS
(`net_UdpUseEncryption true`) — the AES-256-GCM tag on every datagram is the
per-packet MAC, and with AES-NI hardware its cost is negligible.

Honest caveat: audits are a *detection* mechanism. An attacker can maintain a
fake "clean" state solely for answering audits; the real defense remains the
server-authoritative simulation, which never trusts client-reported hits,
positions, or health in the first place. See the Network Arena Audit section
of [`Gems/ArenaShooterNet/README.md`](../../Gems/ArenaShooterNet/README.md)
for component setup.

## Key management rules

- The private key lives only on the server. Never commit `*.pem` private keys
  to source control; add them to `.gitignore`.
- Rotate certificates by regenerating and redeploying both files; pinned
  clients must receive the new public cert (ship it in a patch).
- For public matchmade games, use a CA-signed certificate and set
  `net_SslAllowSelfSigned false`.
