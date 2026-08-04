# ServerBrowser gem

Internet server discovery: dedicated servers announce themselves to a
lightweight master server, clients fetch the list and join — all scriptable
from Lua/Script Canvas. Direct `connect <ip>:<port>` keeps working as a
fallback.

## 1. Run the master server

Any host reachable by servers and players (stdlib only, no dependencies):

```bash
python3 scripts/master_server.py --port 27900
```

Servers expire from the list 90 s after their last heartbeat (`--ttl`).
The service speaks plain HTTP — put it behind a firewall rule or reverse
proxy as needed; it never handles secrets (the list is public by design).

## 2. Announce your dedicated server

In `server.cfg` (or via rcon):

```
sb_master_url http://your.host:27900
sb_server_name My Arena Server
sb_game_port 33450
sb_max_players 16
sb_announce true
```

The server heartbeats every `sb_announce_interval` (30 s) seconds. The
master server records the announcing IP, so the listed address is the
server's public address automatically. Update `sb_map` / `sb_players` from
your game logic or rcon to keep the listing fresh.

## 3. Browse and join from the client

Set `sb_master_url` on the client too, then from Lua / Script Canvas:

```lua
ServerBrowserRequestBus.Broadcast.RefreshServerList()
-- ServerBrowserNotificationBus handler receives:
--   OnServerListEntry(name, address, port, map, players, maxPlayers)  (one per server)
--   OnServerListRefreshed(count)   or   OnServerListError(message)
ServerBrowserRequestBus.Broadcast.JoinServer(address, port)  -- runs `connect address:port`
```

The ArenaShooter kit ships `ServerBrowserMenu.lua` (put it on the game
manager entity): **F9** / gamepad **back** opens the browser and refreshes,
up/down select, **Enter**/**A** joins, left/right refresh.
