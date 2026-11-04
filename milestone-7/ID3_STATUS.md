# id3 (Intel NBT, iDrive 4/5) Android Auto — status & knowledge dump

Date: 2026-11-04. This is a working record of the effort to run NBT-AAD (Android Auto)
on the **id3** head unit — the Intel NBT (QNX Neutrino 6.5.0 x86, `hu-intel`), i.e. the
iDrive 4/5 generation, which never shipped native AA. Everything below is current as of
this writing and is meant to survive a pause in the work.

## TL;DR

- The NBT-AAD daemon **runs natively on the id3 (x86)** and **wired Android Auto works over
  USB**: AOA negotiation, TLS, ServiceDiscovery, all channels, and H.264 video streaming all
  succeed end-to-end (verified against a Xiaomi Redmi Note 10 Pro).
- The remaining gaps are: (1) the **"Android Auto" menu entry plugin** (installed but it
  crashed the HMI — needs a rebuild against the id3's C++ runtime), and (2) **video decode +
  display** (frames arrive but the id3 has no mm-renderer).

## Repo / branches

- Fork: `BillyTheSleeper/Supra-iDrive6-Android-Auto`, branch **`id3-x86`** (tracking upstream `main`).
- Upstream: `Sq00ky/Supra-iDrive6-Android-Auto` — we rebased onto its `main` (51 commits) which
  already contains the **USB accessory support** (`usb_accessory.cpp`, `usb_direct.cpp`, `usb_aoap.hpp`,
  `usb_log.hpp`) and an audio restructure (`src/audio/`).
- Parent repo: `BillyTheSleeper/pegasus`, branch `id-modes-desktop`, path `id_modes/nbt-aad`.

## What was done / what works

### 1. x86 port (committed to `id3-x86`)
- `milestone-7/Makefile`: added `ARCH=x86` (gcc_ntox86 targets) next to the default `armv7le`.
  Build with: `QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6 PATH=... make ARCH=x86 nbt-aad`.
- `milestone-7/lib-x86/`: the id3's own x86 libs pulled off the unit — `liba2i_client.so`,
  `libscreen.so.1`, `libssl.so.2`/`libcrypto.so.2` (OpenSSL 1.0.1b, has TLS 1.2). The SDP's
  OpenSSL 0.9.8m lacks `TLSv1_2_client_method`, so the id3's libs are required at link time.
- QNX SDP 6.5.0 is installed at `/opt/qnx650` (x86 target present).

### 2. DSI port (committed)
- The id3's DSI lib is `libsys_dsi_colibry.so.3` (older C variant), not the NBT Evo's
  `libsys_dsi_colibry.cpp-ne.so.8`. The C++ API is the same classes, but:
  - dlopen names must include `.so.3`;
  - `CDSIOStream::write(const void*,uint)` is absent → use `CDSIStreamBase::load(const void*,uint)`;
  - the stream ctor takes `CDSITCPSocket&` and drops the `ipcFd` param.
  With those, the DSI clients (Bluetooth GAP, MOST server/knob, Nuance voice) attach on the id3.
- Requires `/lib` in `LD_LIBRARY_PATH` for the DSI lib's dependencies.

### 3. Wired AA over USB (works)
Run the daemon with:
```
LD_LIBRARY_PATH=/opt/sys/lib:/lib:/usr/lib:/fs/sda0/lib/graphics/tunnelcreek \
  NBT_USB=1 NBT_USB_DIRECT=1 NBT_RX_TIMEOUT_S=0 /fs/sda0/opt/aa/nbt-aad
```
- The id3's own AOA drivers do the heavy lifting: `vdev-medialaunch` runs
  `/opt/mm/bin/aoap-switch`, which makes `/dev/android<bus><dev>`; a `devctl` switch makes the phone
  re-enumerate as `18d1:2d00`, then `devc-serusb-aoap` creates `/dev/oap<bus><dev>` (`read()`=bulk IN,
  `write()`=bulk OUT). Upstream's `usb_accessory.cpp` bridges `/dev/oap*` to a socketpair so `AASession`
  runs unchanged.
- Verified log flow: `phone found, AOA protocol 2` → `switch to accessory mode` → `/dev/oap01 open` →
  `Android Auto client connected over USB` → TLS handshake → ServiceDiscovery ("Toyota GR Supra 2020") →
  all 14 channels open → AV SETUP + VideoFocus → H.264 SPS/PPS + streaming frames.
- `NBT_USB_DIRECT=1` reads the phone through `libusbdi` directly, avoiding the media app's
  `OAPReaderThread` stealing transfers from `/dev/oap` (which caused TLS record sequence jumps).

## Open items / blockers

### A. Menu-entry plugin (installed, crashed the HMI)
- `milestone-7/id3_plugin/` (committed): a plugin for the id3's **hmi2012 App Framework**
  (`HmiMain3D` + `libAppFramework.A.so`/`libCoreFramework.A.so`), NOT the ID5/ID6 RHMI.
  Files: `NbtAad.manifest` (`.so` + Etch id 210), `NbtAad.xml`, `nbt_aad_app.cpp` (daemon control over
  UDP `127.0.0.1:52790` + `/dev/shmem/nbt-aad-state`), `Makefile`, `lib/` (the two framework `.so` for linking),
  `README.md`, `MENU_CODING.md`.
- Install path on the id3: `/fs/sda0/opt/hmi/data/ro/Plugins/NbtAad/` (manifest + xml + `libNbtAad.so` +
  `NbtAad_ALL_Texts.zip` + `NbtAad_ALL_Images.zip` — the zips are the upstream rhmiapp ones).
- **It crashed the HMI on load.** Likely cause: `libNbtAad.so` was built with `-Vgcc_ntox86_gpp`
  (GNU `libstdc++.so.6`), while the id3's HMI uses Dinkumware `libcpp-ne.so.4`. **Fix:** rebuild with
  `-Vgcc_ntox86` (Dinkumware) and pre-validate deps (`ntox86-objdump -p`) + `dlopen`-test from a probe
  before letting HmiMain3D auto-load. **Recovery if it boot-loops:** `rm -rf /fs/sda0/opt/hmi/data/ro/Plugins/NbtAad`.
- The id3 HMI plugin ABI differs from ID5/6: `pluginAppInfo` is **20 bytes** (not 24) and the
  `bmw::PluginApp` ctor is **4-arg**; `dlsym("pluginAppInfo")`, `create`/`destroy` at `+0x0c`/`+0x10`.

### B. Video decode + display (plan written, not implemented)
- `milestone-7/video_decode_id3.md` (committed): the id3 has **no** `libmmrndclient` (mm-renderer) and
  **no hardware H.264 decode**. The software decoder is **`libNmeVideoSW.so`** in `/fs/sda0/opt/mm/lib/cinemo/`
  (Cinemo NME): `NmeCreateCodecNvdec` + `"h264codec"`, AVC parsing in `libNmeBaseClasses.so`,
  colour-convert/blit in `libNmeVideoDevice.so`/`libNmeVideoRenderer.so`.
- Plan: phone H.264 → NME `h264codec` (NV12) → RGBA → the existing `libscreen` window, behind a
  `DECODE_NME_SW` backend gated to the id3 (leave mm-renderer for NBT Evo).
- Blockers: exact `INmeVideoCodec` vtable layout (needs Ghidra), NV12-vs-I420/stride, non-default
  `libscreen` path (`/fs/sda0/lib/graphics/tunnelcreek/`).

### C. Coding (A4A) — reportedly already set
- Storage: `/fs/sda1/opt/sys/persistence/` — `pers_NBTCarHUTwoSwt` (27 KB, the signed SWT coding),
  `pers_NbtDiagHuHighAppCodingController` (1107 B), `pers_NaviControllerSWTCtrl_CPersistence_0`.
  The coding is **signed** (DER/ASN.1 RSA/SHA at the end of `pers_NBTCarHUTwoSwt`).
- The A4A/Android-Auto bits (from the `BMW-NBT-Emulator` CAFD): `A4A` `0x3000/107/0x80`,
  `REMOTE_HMI` `0x3000/107/0x70`, `A4A_REMOTE_APPS` `0x3000/108/0x01`, `A4A_ANDROID` `0x3002/81/0x02`;
  CarPlay: `APPLE_ENHANCEMENTS` `0x3000/22/0x80`, `API_APPLE_AUTH_CHIP_ON_OFF` `0x3003/22/0x40`.
- User stated the A4A coding is already coded on this bench unit. The stock id3 firmware has **no**
  pre-existing AA/CarPlay row (only USB/Bluetooth/WLAN/AUX), so the menu entry must be *added* (our plugin),
  gated by these coding bits.

## Key facts / references

- **SSH**: `id3` alias → `root@100.126.171.31:2223` (bmw-ispi ENET forward), password `ts&SK412`.
  The id3's QNX shell has NO `head`, `wc`, `strings`; use `sshpass -p 'ts&SK412' ssh -o ConnectTimeout=20 id3 '...'`.
  Retry on timeouts (the ENET is flaky).
- **CarPlay Wi-Fi exists**: `uap0` at `172.16.222.1` (`ifconfig` needs `LD_LIBRARY_PATH=/lib`).
- **Build**: QNX SDP 6.5.0 at `/opt/qnx650`; x86 target `gcc_ntox86[_cpp|_gpp]`; host tests are Linux g++.
- **Upstream USB support**: release "USB Support - v1.8.72" (tag `Installer`), env `NBT_USB=1`, `NBT_USB_DIRECT=1`.
- **Reference projects**:
  - `f1xpl/aasdk` — the AASDK (AOAP USB + AA protobuf).
  - `jrammler/aa2acp` — AA→CarPlay bridge (uses AASDK).
  - `vandelayautollc/nbt-evo-toolkit` — NBT Evo reverse-engineering (coding FDL + signing tools).
  - `DAviS0d/BMW-NBT-Emulator` — runs the SAME `HU_NBT` QNX 6.5 x86 + `HmiMain3D` in QEMU; has the
    A4A/AA coding bits + the "External devices" AA/CarPlay row mechanism.
  - `DSIHarman/DSI` — the DSI spec (SGBluetoothGap is a standard interface).

## Next steps (when resumed)

1. **Plugin fix**: rebuild `libNbtAad.so` with `-Vgcc_ntox86` (Dinkumware libcpp), verify `NEEDED` libs,
   `dlopen`-test it from a probe binary, then reinstall + restart HMI.
2. **Video decode**: reverse-engineer the `INmeVideoCodec` vtable (Ghidra) + implement the NME `h264codec`
   NV12→RGBA→Screen path (or fall back to a cross-compiled software H.264 decoder).
3. **Coding verification**: decode `pers_NBTCarHUTwoSwt` to confirm the A4A bits are activ (and map the
   signature if they ever need changing).
