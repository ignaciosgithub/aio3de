# Arena Shooter Networking Gem

Server-authoritative multiplayer for the ArenaShooter example, built on the
engine's **Multiplayer** gem (AzNetworking). Two multiplayer components:

- **Network Arena Player** — client-predicted movement + server-validated
  shooting. The autonomous client samples the ArenaShooter input events
  (`MoveForward`, `MoveRight`, `LookX`, `LookY`, `Shoot` from
  `arenashooter.inputbindings`) into network inputs. Movement runs through the
  networked PhysX character controller on both the predicting client and the
  server (mispredictions are corrected automatically). **Shots are resolved
  only on the server**: the client transmits trigger state, the server
  enforces the fire interval and performs the hitscan raycast — a hacked
  client cannot report hits, damage, or ignore cooldowns.
- **Network Arena Health** — health lives on the server and replicates to all
  clients (clients cannot set it). Damage comes from server-local `Damage`
  events raised by the player component's server-side shot resolution. Death
  stashes the body and respawns at the spawn point after a delay.

## Enable

```
scripts\o3de.bat enable-gem -gn ArenaShooterNet -pp <your project path>
```

Code gem: re-run CMake configure and rebuild. Requires the **Multiplayer**,
**PhysX5** and **StartingPointInput** gems (and the ArenaShooter gem for the
input bindings asset).

## Player prefab

The networked player entity needs:

1. **PhysX Character Controller**
2. **Network Binding** (Multiplayer gem)
3. **Network Transform**
4. **Network Character**
5. **Network Arena Player** (this gem)
6. **Network Arena Health** (this gem)
7. **Input** component with `arenashooter.inputbindings`

Register the prefab as the spawnable player: set the Multiplayer gem's player
spawner (e.g. the **Simple Network Player Spawner** component on a level
entity) to this prefab.

## Running server + client

Build the launchers (`<Project>.ServerLauncher`, `<Project>.GameLauncher`),
then:

```
# machine A (server)
<Project>.ServerLauncher.exe --console-command-file=server.cfg
# server.cfg: host   (add: sv_port 33450 to pick a port)

# machine B (client)
<Project>.GameLauncher.exe --console-command-file=client.cfg
# client.cfg: connect <server ip>:33450
```

For internet play, forward the UDP port on the server's router. Test locally
first (server + client on one machine, `connect 127.0.0.1`).

## Anti-cheat model

- The server owns all gameplay state: position (character controller runs
  server-side), health, damage, fire rate.
- Clients send only inputs; a tampered client can at most send legal inputs.
- Transport hardening (RSA-authenticated handshake + encrypted/authenticated
  packets via DTLS): see
  [`docs/aio3de/SECURE_NETWORKING.md`](../../docs/aio3de/SECURE_NETWORKING.md).
- Detection layer: the **Network Arena Audit** component (below).

## Network Arena Audit (audit challenges)

Add the **Network Arena Audit** component to the player prefab (next to
Network Arena Player) to enable unpredictable audit challenges:

- On spawn the server generates a random per-session audit key (OS CSPRNG)
  and delivers it over the reliable (DTLS-protected) channel.
- At random intervals (`Min/Max Challenge Interval`) the server sends a
  random nonce challenge with a response deadline.
- The client answers with an HMAC-SHA256-authenticated snapshot: the nonce,
  its position, its health, and a rolling FNV-1a hash of every raw local
  input event.
- The server verifies the tag (constant-time compare) and checks the
  reported state against its authoritative simulation (`Position Tolerance`,
  `Health Tolerance`).
- Failed, missed, or mismatching audits accumulate strikes; at `Max Strikes`
  the client is disconnected if `Kick On Failure` is set (otherwise logged).

All audit RPCs are reliable, so packet loss delays but never drops a
challenge or response — an honest client on a lossy link accumulates at most
an occasional deadline strike, never `Max Strikes`.

Honest scope: audits *detect* hooked or dishonest clients; they cannot
*prevent* them (a sophisticated cheat can maintain a clean shadow state just
for audits). Prevention is the server-authoritative simulation above.
