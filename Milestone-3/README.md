# nbt-aad — Fork B (stable testbed)

Native wireless Android Auto daemon for the BMW NBT Evo ID6 head unit in a Toyota GR Supra (J29).
QNX 6.5 (GCC 4.4.2), ARMv7LE. The head unit hosts the connection; the phone does not need
"Start Head Unit Server".

**Version:** `v1.3.48-touchfix` (banner string in `src/main.cpp`)
**Base:** Milestone 2 (v1.3.29). No audio, no hard keys / rotary. Use the main `Fork` for audio.

## Status

| Feature | State |
|---|---|
| Wireless setup (HU-hosted: Bluetooth RFCOMM → Wi-Fi AP → TCP 5277) | Works (w: first attempt, proactive mode). Reliability across more runs still unproven |
| Video 1280x480 window, H.264 via mdev | Works, sharp |
| Touch (single finger) | Works, corners verified in w |
| Touch lost-"up" recovery | New in v1.3.48, car-untested |
| Multi-touch / pinch | Not working yet. Finger 1/2 layout is a guess, awaiting evidence (see Touch) |
| Sensors (driving status, night mode) | Works |
| Audio, mic, rotary, hard keys | Not in this fork |
| Stop iDrive reacting to touches | Not done |

## Build

On the QNX VM (qcc): `make all`. Do not add `-fPIC` (causes ELIBEXEC). QNX GCC 4.4.2 has no `__sync_*`/`__atomic_*`,
so use pthread mutexes, and headers must include `<stddef.h>` for `size_t`.

Tests in `tests/`: `test_sdr`, `test_wpp`, `test_touch`, `test_session_lifecycle` (400 session cycles, TSAN).

## Run (on the head unit)

```
./nbt-aad > /tmp/w.log ; /opt/sys/bin/LayerManagerControl set surface 256 visibility 1
```
HU-hosted wireless:
```
NBT_WIRELESS=1 ./nbt-aad > /tmp/w.log ; /opt/sys/bin/LayerManagerControl set surface 256 visibility 1
```
Then `cp /tmp/w.log /mnt/umass01200t7/`. Write logs to `/tmp` first (the USB stick has zero-filled logs before).
The second command restores the iDrive UI after the daemon exits. The daemon runs `pfctl -d` at start (`NBT_KEEP_PF=1` skips).

## How it works

1. **Wireless (`wireless_setup`, `dsi_client`).** Registers the SPP UUID via the DSI colibry client, opens `/dev/btspp0`
   (raw termios), sends WifiStartRequest and the AP info taken from the CarPlay config `iap2_Wifi.cfg`
   (SSID `Toyota51807 CarPlay`, uap0 172.16.224.1). The phone joins the AP and connects to TCP 5277.
   The btspp device is message-oriented: a short `read()` discards the rest of the message, so the reader
   always reads a large buffer and frames messages itself. The RFCOMM link stays open and serviced while the session runs.
   Handshake modes rotate on failures: `2` proactive (default first), `0` legacy, `1` version-first.
2. **Session (`aa_session`, `aa_cryptor`, `tcp_server`).** TLS 1.2 client (the head unit connects as TLS client), service
   discovery from `sdr_builder` (Toyota GR Supra identity), channels: 0 control, 1 sensors, 3/5/7 input, 6 video,
   9 audio (not consumed here), 12 media metadata.
3. **Video (`video_manager`).** Screen window `1280` (1280x480 @ 60, z-order 2000) plus an MM-Renderer context fed by the
   hardware decoder `/dev/name/local/mdev`. The factory CarPlay context `cpvideoctx*` must be purged at start or frames
   are accepted but nothing is drawn.
4. **Picture geometry.** The phone renders a **1280x720** canvas. The default is "crop mode": the phone is told to keep its UI in the
   top 480 rows (bottom inset 240), mdev and the MMR destination are 1280x720, and the 480-row panel shows the top 480 rows 1:1.
5. **Touch (`touch_probe`).** The car sends touch as UDP to port 30567 (112-byte frames from 160.48.199.115:30501).
   Forwarded to the phone as input events on channel 7; the touchscreen is declared 1280x720.

### Touch frame layout (verified for finger 0)

`B0 04 80 02`, length 0x68, 16-byte header, then 12 entries of 8 bytes `[00 c1 40 idx][4 data bytes]`.
Finger 0: entry 4 data = `[counter, id, x_hi, x_lo]`, entry 5 data = `[y_hi, y_lo, size, state<<4]`.
State 1 down, 2 move, 3 up. x 0..1279, y 0..479. Entries 6/7 and 8/9 are assumed to be fingers 1 and 2 (stride 16),
unconfirmed: the daemon logs any frame with data in entries 6..11 as `[TouchFwd] MULTI frame`.

## Environment flags

| Flag | Effect |
|---|---|
| `NBT_WIRELESS=1` | HU-hosted wireless (accept-only, no outbound scan). Unset = connect out to a phone hosting its own server |
| `NBT_WPP_MODE=0\|1\|2` | Pin the handshake mode |
| `NBT_TOUCH_FWD=0` | Disable touch forwarding (default on) |
| `NBT_TOUCH_PROBE=1` | Passive probe: log raw touch frames only |
| `NBT_TOUCH_SX/SY/OX/OY` | Touch scale / offset (default 1.0 / 0) |
| `NBT_TOUCH_W/H` | Clamp range (default 1280x720) |
| `NBT_TOUCH_IDLE_MS` | Silence before a stuck finger is released (default 400) |
| `NBT_TOUCH_CFG_H` | Touchscreen height declared to the phone (default 720) |
| `NBT_VID_INSETS=top,bottom,left,right\|off` | Phone UI insets (default `0,240,0,0`) |
| `NBT_VID_DPI` | Override VideoConfig DPI to shrink the UI (e.g. 140, 130, 120) |
| `NBT_VID_RES`, `NBT_VID_MW`, `NBT_VID_MH` | Other VideoConfig overrides |
| `NBT_VID_SRC_H`, `NBT_VID_DST_H` | mdev height / MMR destination height (default 720) |
| `NBT_VIDEO_DUMP=1` | Dump the raw H.264 to `/tmp/aa_video.h264` (4 MB cap). Decode with ffmpeg |
| `NBT_NIGHT_MODE=1`, `NBT_DRIVING_STATUS=0` | Sensor values |
| `NBT_PURGE_ALL=1` | Destroy every MM-Renderer context instead of only `aadvideoctx*`/`cpvideoctx*` |
| `NBT_KEEP_PF=1` | Do not run `pfctl -d` |
| `NBT_HIDE_IDLE`, `NBT_VERBOSE`, `NBT_RX_TIMEOUT_S` | Misc |

Squashed (old) picture mode: `NBT_VID_INSETS=off NBT_VID_SRC_H=480 NBT_VID_DST_H=480 NBT_TOUCH_SY=1.5`.

## Known issues / next

- Rail icons overlap in crop mode (mic is hard to hit). Try `NBT_VID_DPI=140`, then 130 / 120.
- Pinch: needs a log with a real two-finger touch to confirm the finger-1 slot layout.
- iDrive still reacts to touches while Android Auto is shown (UDP stream goes to both).
- Audio, rotary / hard keys, OEM menu entry, autostart: in the main `Fork` or not started.
- Wireless: the phone sometimes answers `msg 7 00 00` and never sends msg 2 on older builds. Confirm over several runs with the current reader.

## Source layout

`src/`: `main`, `aa_session`, `aa_cryptor`, `tcp_server`, `video_manager`, `wireless_setup`, `dsi_client`, `bluetooth_manager`,
`touch_probe` (+ `touch_types.hpp`), `sdr_builder`, `crash_handler`, `audio_manager`, `input_manager`.
`include/dhu_sdp.h` is the canned service-discovery protobuf.
