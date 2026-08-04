# Server Admin Gem (rcon)

rcon-style remote administration for dedicated servers: a
password-authenticated TCP console channel that executes engine console
commands remotely and streams their output back.

## Enable

```
scripts\o3de.bat enable-gem -gn ServerAdmin -pp <your project path>
```

Code gem: re-run CMake configure and rebuild the ServerLauncher.

## Server setup

Add to the server's cfg (e.g. `server.cfg`):

```
admin_password YOUR_LONG_RANDOM_SECRET
admin_enable true
admin_port 33470
```

The channel stays completely off unless `admin_enable` is true **and** a
password is set. Forward/allow TCP `admin_port` on the server's
firewall/router — ideally only from your own IP.

## Client

Use the shipped reference client:

```
set AIO3DE_RCON_PASSWORD=YOUR_LONG_RANDOM_SECRET
python scripts\rcon.py --host <server ip> --port 33470          # interactive
python scripts\rcon.py --host <server ip> -c "LoadLevel Levels/Arena2/Arena2.spawnable"
```

Any engine console command works: cvars (`sv_map`, `net_UdpUseEncryption`),
level changes (`LoadLevel ...`), match control, `disconnect`, etc.

## Protocol & security

- Line-based over TCP. On connect the server sends `CHALLENGE <hex nonce>`
  (32 random bytes from the OS CSPRNG); the client answers
  `AUTH <hex HMAC-SHA256(password, nonce)>` — the password never crosses
  the wire, and a sniffed response is useless for any other nonce
  (constant-time comparison server-side).
- After `OK`, each command line executes on the server's main thread; all
  console/log output produced during execution streams back, terminated by
  an `<<<END>>>` line.
- One admin session at a time; failed auth attempts are logged.
- Command lines and output are plaintext after auth — run it over a trusted
  network, an SSH tunnel (below), or a VPN for full confidentiality.

## SSH alternative / tunnel

If your server box runs SSH you have two good options:

1. **Plain SSH** — no gem needed: SSH in and drive the server console
   directly (e.g. run the ServerLauncher inside `tmux`/`screen` and attach:
   `tmux attach -t gameserver`, type console commands, detach with
   `Ctrl+B D`).
2. **rcon over an SSH tunnel** — keep `admin_port` bound but firewalled to
   localhost-only, then:
   ```
   ssh -L 33470:127.0.0.1:33470 user@server
   python scripts/rcon.py --host 127.0.0.1
   ```
   This adds SSH's encryption + key auth on top of the HMAC handshake.
