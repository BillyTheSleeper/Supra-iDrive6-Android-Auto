# nbt-aad — Fork D (audio), branched from Fork C v1.4.2-knob

Native wireless Android Auto daemon for the BMW NBT Evo ID6 head unit in a Toyota GR Supra (J29).
QNX 6.5 (GCC 4.4.2), ARMv7LE. The head unit hosts the connection; the phone does not need
"Start Head Unit Server".

**Version:** `v1.5.1-audio`
**Base:** Fork C v1.4.3-knob + the main Fork's staged A2I audio pipeline.

## Status

| Feature | State |
|---|---|
| Wireless setup (HU-hosted: Bluetooth RFCOMM → Wi-Fi AP → TCP 5277) | Works, default on (`NBT_WIRELESS=0` disables) |
| Video 1280x480 window, H.264 via mdev, crop mode, DPI 140 | Works (DPI 140 default, car-verified w9) |
| Touch (single finger) | Works |
| Multi-touch / pinch | Works (v1.4.3 decode, car-verified 2026-10-23) |
| Sensors (driving status, night mode) | Works |
| iDrive knob: rotate, push, tilt, hard keys | Works (car-verified nbt-25), default on (`NBT_KNOB=0` disables) |
| Audio (`NBT_AUDIO_STAGE`) | Stages 0-2 car-stable (a1-a3, 2026-10-23): PCM flows into AirPlayMain at real-time pace, not audible yet. See Audio below |
| Bluetooth audio mode (`NBT_AUDIO_BT=1`, v1.5.3) | **Car-verified 2026-10-23 (audio.log, audio2.log)**: AA media sink (ch 9) left out of the ServiceDiscovery, so the phone plays media over Bluetooth A2DP; daemon selects the car's BT source with `audioconn -b 0x3a 0x1`. Speech (ch 8) and mic (ch 14) must stay advertised: without them the phone ends the session (`No audio playback service` / `No audio/mic`). Overrides `NBT_BT_SRC`, `NBT_BT_SINK`, `NBT_BT_RELEASE=1`, `NBT_SDR_DROP` |
| Stop iDrive reacting to touches / keys | Not done |

## Build

On the QNX VM (qcc): `make all`. Do not add `-fPIC` (causes ELIBEXEC). QNX GCC 4.4.2 has no `__sync_*`/`__atomic_*`,
so use pthread mutexes, and headers must include `<stddef.h>` for `size_t`.

Tests in `tests/`: `test_sdr`, `test_wpp`, `test_touch`, `test_session_lifecycle` (400 session cycles, TSAN).

## Run (on the head unit)

```
./nbt-aad > /tmp/w.log ; /opt/sys/bin/LayerManagerControl set surface 256 visibility 1
```
HU-hosted wireless (Bluetooth -> Wi-Fi -> TCP) is the default since v1.4.2. `NBT_WIRELESS=0` restores the old outbound scanner.
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

### Touch frame layout (finger 0 and two-finger pinch verified)

`B0 04 80 02`, length 0x68, 16-byte header, then 12 records of 8 bytes `[00 c? 40 seq][4 data bytes]`.
The data bytes of records 4..11 form one 32-byte stream: `[flags][count]` then `count` contacts of 6 bytes
`x_hi x_lo y_hi y_lo size (state<<4 | id)`. State 1 down, 2 move, 3 up; `id` is a stable finger id (used as the
AA pointer id). x 0..1279, y 0..479. Contacts are packed, so finger 1 straddles records 6 and 7. Up to v1.4.2 the
daemon assumed a fixed 16-byte stride per finger, which is why the phone never saw a second pointer (pinch broken).
Parser: `src/touch_frame.hpp`, test `tests/test_touch_frame.cpp` against the real capture.

## iDrive plugin (Fork D, `plugin/`)

`libhmiaad.so` + `NbtAadPlugin.xml` is a native HmiMain3D plugin bundle (no certificates involved). HmiMain3D loads
every `hmiplugins/*.xml` at boot; each name in the XML must be exported as `<Name>Struct` =
`{create, destroy, "GCC, Version 4.4.2"}`. ABI notes are in `plugin/plugin_abi.hpp`. Built with the `_gpp`
(GNU libstdc++) variant because the HMI uses libstdc++, not the daemon's Dinkumware C++ library.

| Stage | State |
|---|---|
| 1 Load test: bundle + `CNbtAadGate` plugin only log to `/dev/shmem/nbt-aad-plugin.log` | Built, host-tested, car-untested |
| 2 Input gate: iDrive ignores knob/keys/touch while AA is shown (daemon <-> plugin socket) | Not started |
| 3 Media/Radio source entry starts `/opt/aa/nbt-aad` (hook-free: HMI timer + menu registry) | v1.5.4: built, host-checked, car-untested |
| 3b Own text + icon, return to iDrive without killing the daemon | Not started |

Stage 3 (v1.5.4, hook-free): nothing in HmiMain3D is modified. The plugin resolves `CHmiMainDispatcher` through the
plugin framework, waits for the HMI processing unit (`CHmiAppProcUnit`) and arms a timer on it with iDrive's own
timer service, so its code runs on the HMI thread. There it looks up the `ST_Com_Selection` menu in
`CHmiMenuProviderRegistry` and, once Communication has built it, adds a `CHmiGenericMenuEntry` (iDrive's constructor)
through the menu's own `addEntry`. The entry is our own subclass whose execute starts `/opt/aa/nbt-aad`.
Exported services are found with `dlsym`; the two non-exported HmiMain3D functions (registry getter, entry
constructor) are checked against the expected code first, and on a mismatch the plugin only logs.
The daemon is single-instance: a second start sends `show` to the running daemon over UDP 127.0.0.1:52790 and exits
(`NBT_CTL=0` off, `NBT_CTL_PORT` port).

| File on the head unit | Purpose |
|---|---|
| `/opt/aa/nbt-aad` | The daemon. If missing, the plugin adds no entry |
| `/opt/aa/nbt-aad.env` | Optional, one `NAME=value` per line for the daemon. Default when absent: `NBT_AUDIO_BT=1` |
| `/opt/aa/menu.cfg` | Optional `menu=`, `text=0x..`, `icon=0x..`, `focus=0x..`, `order=0x..`. Defaults: `menu=ST_Ent_Selection` (Media/Radio sources), order 0x1c00 (between Bluetooth audio 0x1bae and USB 0x1d7d), CarPlay text 0x200148d0, Mobile devices icon 0x50000513, focus 0x7aa0. `menu=ST_Com_Selection` + `order=0x590` = Communication menu |
| `/opt/aa/menu.disable` | If present, no entry (takes effect at the next boot) |
| `/dev/shmem/nbt-aad-plugin.log`, `/dev/shmem/nbt-aad.log` | Plugin log, daemon output (appended) |

Menu images (stage 3, reversed 2026-10-23): the HMI registers runtime images with
`CHmiDynamicStreamResourceProvider::registerDynamicResource(type, stream)` or
`CHmiDynamicFileResourceProvider::registerDynamicResource(type, path, bool)` (both exported by `libhmidb.so`);
`SHmiResourceHandle::guessType` accepts PNG, JPG, BMP, DDS and raw ARGB. Supra (j29 ID6L) main-menu content
previews are 549x484 RGBA PNGs (grey line glyph on a dark soft vignette); small row icons are 63x54 RGBA.
Our images are in `plugin/res/` (`aa_content_preview_549x484.png`, `aa_row_icon_63x54.png`, `aa_statusbar_54x54.png`),
generated by `plugin/res/make_icons.py` (Pillow): a road-in-perspective "A", original artwork matched to the j29 style.

`plugin/plugin_check` loads the library exactly like the framework (dlsym, compiler check, create, start, stop,
destroy) so it can be validated on the car before installing. Remove the plugin by deleting `NbtAadPlugin.xml`
from `hmiplugins` and rebooting.

## Environment flags

| Flag | Effect |
|---|---|
| `NBT_WIRELESS=0` | Disable HU-hosted wireless (default on since v1.4.2). Then the daemon connects out to a phone hosting its own server |
| `NBT_WPP_MODE=0\|1\|2` | Pin the handshake mode |
| `NBT_TOUCH_FWD=0` | Disable touch forwarding (default on) |
| `NBT_KNOB=0` | Disable iDrive knob, tilt and hard keys to the phone (default on since v1.4.3; observe-only MOST client; needs touch forwarding on) |
| `NBT_KNOB_INVERT=1` | Reverse the rotary direction |
| `NBT_KEY_<SLOT>=n` | Android keycode for a hard key, 0 = ignore. SLOT = MENU OPTIONS BACK COM MEDIA NAV MAP; defaults 3 HOME, 0, 4 BACK, 5 CALL, 65537 MEDIA, 65538 NAV, 65538 NAV |
| `NBT_KNOB_INST`, `NBT_KNOB_RAW=1` | Controller instance (hex, default 03); dump every raw MOST event |
| `NBT_TOUCH_PROBE=1` | Passive probe: log raw touch frames only |
| `NBT_TOUCH_SX/SY/OX/OY` | Touch scale / offset (default 1.0 / 0) |
| `NBT_TOUCH_W/H` | Clamp range (default 1280x720) |
| `NBT_TOUCH_IDLE_MS` | Silence before a stuck finger is released (default 400) |
| `NBT_TOUCH_CFG_H` | Touchscreen height declared to the phone (default 720) |
| `NBT_VID_INSETS=top,bottom,left,right\|off` | Phone UI insets (default `0,240,0,0`) |
| `NBT_VID_DPI` | VideoConfig DPI (default 140, car-verified; `0` = phone default) |
| `NBT_VID_RES`, `NBT_VID_MW`, `NBT_VID_MH` | Other VideoConfig overrides |
| `NBT_VID_SRC_H`, `NBT_VID_DST_H` | mdev height / MMR destination height (default 720) |
| `NBT_VIDEO_DUMP=1` | Dump the raw H.264 to `/tmp/aa_video.h264` (4 MB cap). Decode with ffmpeg |
| `NBT_NIGHT_MODE=1`, `NBT_DRIVING_STATUS=0` | Sensor values |
| `NBT_PURGE_ALL=1` | Destroy every MM-Renderer context instead of only `aadvideoctx*`/`cpvideoctx*` |
| `NBT_KEEP_PF=1` | Do not run `pfctl -d` |
| `NBT_HIDE_IDLE`, `NBT_VERBOSE`, `NBT_RX_TIMEOUT_S` | Misc |

Squashed (old) picture mode: `NBT_VID_INSETS=off NBT_VID_SRC_H=480 NBT_VID_DST_H=480 NBT_TOUCH_SY=1.5`.

## Known issues / next

- Rail icons overlapped in crop mode at the phone's 160 DPI; fixed by the DPI 140 default (v1.4.2).
- Pinch: needs a log with a real two-finger touch to confirm the finger-1 slot layout.
- iDrive still reacts to touches while Android Auto is shown (UDP stream goes to both).
- Audio, rotary / hard keys, OEM menu entry, autostart: in the main `Fork` or not started.
- Wireless: the phone sometimes answers `msg 7 00 00` and never sends msg 2 on older builds. Confirm over several runs with the current reader.

## Source layout

`src/`: `main`, `aa_session`, `aa_cryptor`, `tcp_server`, `video_manager`, `wireless_setup`, `dsi_client`, `bluetooth_manager`,
`touch_probe` (+ `touch_types.hpp`), `sdr_builder`, `crash_handler`, `input_manager`, `ccs_input` (+ `ccs_decoder.hpp`),
`nbt_env.hpp`, audio: `audio_manager` (stages), `a2i_stream` (factory A2I playback/capture loops), `pcm_utils`
(ring buffer + resampler, host test `tests/test_pcm.cpp`), `audio_route` (`audioconn` wrapper).
`include/dhu_sdp.h` is the canned service-discovery protobuf.

## test_controls (stand-alone controller monitor)

Separate binary built by `make all` (like `test_display`). Observe-only: registers as a MOST client on `MOSTSubSystem.DSYSMOSTServer` 4.5,
registers FBlock keys E510/E503/E505/E512/E514/E501/E502/1502 and logs every MOST message the iDrive controller produces (rotary, joystick, hard keys, touchpad).

```
./test_controls -t 120 -r > /tmp/c.log
```

`-t N` seconds, `-k HEX,HEX` own key list, `-r` also dump every raw DSI event. Wire format notes are in `src/most_msg.hpp`; decoder test `tests/test_most_msg.cpp`.

## iDrive controller decode (car run 2026-10-22, `tests/fixtures/controls_2026-10-22_e5.txt`)

MOST FBlock 0xE5 instance 0x03, op Status, addressed to the HMI (dst 0x2023); our client receives copies.

| fkt | Bytes | Meaning |
|---|---|---|
| 0xC46 | 8 | `[0]` counter, `[1..2]` BE16 detent position (+1 clockwise), `[4]` tilt as a 12-step clock (1 up, 4 right, 7 down, 10 left, 0 centred), `[3]` centre push (1 down / 0 up, verified 2026-10-23), `[7]` 0xff |
| 0xC47 / 0xC4F | 7 / 8 | One byte per hard key (0 released, 1 pressed, 2 held). Slots: 0 MENU, 1 OPTIONS, 2 BACK, 3 COM, 4 MEDIA, 5 NAV, 6 MAP |

`tests/test_ccs_decoder.cpp` replays the run: 7 detents clockwise, 11 counter-clockwise, then up, down, right and left tilts, then media, com, menu, map, nav, back and options.

## License

NBT-AAD is licensed under the **PolyForm Noncommercial License 1.0.0** (`SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`).
The full text is embedded in `src/main.cpp` / `src/license_text.hpp` and printed by `./nbt-aad --license`.

Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

NBT-AAD is free of charge. Selling it, or bundling it with anything sold, is not permitted.
If you paid for it, you were scammed: official releases are only published at https://github.com/Sq00ky/Supra-iDrive6-Android-Auto.

## Native iDrive app (rhmiapp/) - menu entry without touching HmiMain3D

`rhmiapp/` builds NBT-AAD as an iDrive **PluginApp**, the framework BMW uses for its own add-on apps
(`data/ro/PluginsCoded/SecurityMenu`, `ActiveSeating`). HmiMain3D loads every `Plugins/<Name>/<Name>.manifest`
under `/opt/hmi/ID5/data/ro` and `/var/opt/hmi/data/ID5` at boot; the entry button and its placement come from
`NbtAad.xml`, so iDrive creates the row itself. Pressing it starts `/opt/aa/nbt-aad`.

| File | Purpose |
|---|---|
| `libNbtAad.so` | app code (`pluginAppInfo` entry point, raAction 2 -> start daemon) |
| `NbtAad.manifest` | tells CAppManager what to load |
| `NbtAad.xml` | entry button; default `applicationType="Custom"` inserted into the Media/Radio source list (native state 20 = ST_Ent_Selection) after the Bluetooth audio row (focus 2270) |
| `NbtAad_Multimedia.xml` | alternative: rename to `NbtAad.xml` for iDrive's own Multimedia placement rule |
| `NbtAad_ALL_Texts.zip`, `NbtAad_ALL_Images.zip` | text/icon DBs (regenerate with `python3 rhmiapp/make_db.py`) |

Build: copy `libAppFramework.A.so` and `libCoreFramework.A.so` from the dump's `opt/hmi/ID5/lib` to `rhmiapp/lib/`,
then `make rhmiapp`. Install into `/opt/hmi/ID5/data/ro/Plugins/NbtAad/` and reboot.
Requirements: HmiMain3D only runs PluginApps when A4A, A4A_REMOTE_APPS, REMOTE_HMI or TOUCH_COMMAND is coded.
Off switch: `touch /opt/aa/app.disable`. Log: `/dev/shmem/nbt-aad-app.log`.
