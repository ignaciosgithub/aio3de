# VoiceChat Gem

Team voice chat: microphone capture (Microphone gem), G.711 mu-law compression,
a lightweight UDP relay hosted by the dedicated server, team-channel routing,
and per-talker playback through MiniAudio (independent volume per remote talker,
so multiple talkers never cumulatively raise the volume).

## How it works

- Each client opens a UDP socket to the relay and sends a join packet with its
  team channel id. While push-to-talk is held, 20 ms 16 kHz mono frames are
  captured from the microphone, mu-law encoded (2:1) and sent to the relay.
- The dedicated server hosts the relay (`voice_host true`): it tags each
  client's audio with a talker id and forwards it only to clients on the
  **same channel** (use the player's team id). Stale clients expire after 15 s.
- Receiving clients decode into a per-talker ring buffer played through the
  MiniAudio engine, each with its own `ma_sound` volume.
  `VoiceChatNotificationBus::OnTalkerActive` fires for HUD speaker indicators.

Bandwidth: 16 kHz * 1 byte = ~16 kB/s (~128 kbit/s) upstream per talking
player, only while talking (optionally gated further by `voice_vad_threshold`).

## Server setup

In the dedicated server's cfg:

```
voice_host true
voice_port 33452
```

Open/forward UDP `voice_port` alongside the game port.

## Client API (`VoiceChatRequestBus`, Lua/Script Canvas reflected)

- `ConnectVoice(address, port, channel)` / `DisconnectVoice()`
- `SetChannel(channel)` — switch team channel (rejoins the relay)
- `SetTalking(bool)` — push-to-talk; starts/stops the microphone session
- `SetMuted(bool)` / `IsMuted()` — mute incoming voice
- `SetVoiceVolume(volume)` — applied independently to every remote talker

The ArenaShooter kit ships `Scripts/ArenaShooter/VoiceChat.lua` wiring this to
`VoiceTalk` (hold V / d-pad up) and `VoiceMute` (M / d-pad down) input events.

## CVars

| CVar | Default | Meaning |
|---|---|---|
| `voice_host` | `false` | Host the relay on this (server) process |
| `voice_port` | `33452` | Relay UDP port |
| `voice_vad_threshold` | `0` | RMS gate while talking (0 = send everything) |

## Security note

The voice channel is a separate plain UDP stream: it is **not** protected by
the game's DTLS transport. It carries only compressed audio (no gameplay
state), and the relay only forwards audio from addresses that joined a channel.
For confidentiality, tunnel it (VPN/WireGuard) or keep voice on trusted
networks; integrating voice into the encrypted game transport is future work.

Because the stream is unauthenticated, both ends validate what they receive:
the relay accepts only well-formed packets no larger than one audio frame and
caps concurrent clients, and the client ignores voice from any address other
than the relay it joined and caps how many talker playback objects it will
allocate. So a hostile sender can waste bandwidth but cannot inject audio into
a client that never joined its relay, nor grow client memory without bound.
