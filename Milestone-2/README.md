# NBT-AAD: Native Wireless Android Auto Daemon for BMW NBT Evo (Toyota Supra A90)

## Overview
`nbt-aad` is a native C++ background daemon designed to run directly on the **BMW NBT Evo (iDrive 6)** QNX Neutrino RTOS head unit (Toyota GR Supra A90 / J29 chassis).

It bypasses Apple CarPlay hardware dongles and phone-side hacks by acting as a native Android Auto receiver (`aasdk`) hooked directly into the vehicle's hardware APIs:
* **Video:** Rendered via QNX Screen Windowing System (`libscreen.so`) into a high Z-order surface cloned from `/pps/services/multimedia/renderer/context/cpvideoctx16838/` (`1440x540` @ 60fps).
* **Audio Playback:** Streamed into Harman's `A2I_Client` (`liba2i_client.so`) endpoints for Media (`/input_stereo_AirPlayMain`) and Navigation guidance (`/input_stereo_AirPlayAlt`).
* **Microphone Input:** Captured via `/output_mono_AirPlayUL` directly from the vehicle's hardware DSP with **pre-processed Acoustic Echo Cancellation (AEC)**.
* **Input Intercept:** Subscribes to DSI CommonAPI service `CentralControlSystem35up` (Service ID `0xE5`, Instance `0x10`) over `/proc/mount/srv/servicebroker` for debounced iDrive rotary knob, tilt, and button events.
* **Wireless Transport:** Accepts TCP connections on port `5277` over the car's factory 5GHz Wi-Fi hotspot (`Toyota51807 CarPlay`).

---

## Target Specifications
* **Operating System:** QNX Neutrino 6.5.1 SP1 / 6.6.0
* **Target Architecture:** ARMv7LE (`armv7le`, Cortex-A15, 32-bit Little Endian, VFPv4/NEON)
* **Compiler:** QNX Momentics IDE 4.7 / QNX SDP 6.5 SP1 (`ntoarmv7-gcc` 4.8.x)
* **Runtime Dependencies on Head Unit:**
  * `libc.so.3`
  * `libcpp-ne.so.4` (or `libstdc++.so.6`)
  * `libscreen.so.1`
  * `/opt/mm/lib/liba2i_client.so`
  * `/opt/hmi/ID5/re/lib/lib/libsys_dsi_servicebroker.cpp-ne.so`
  * `/opt/hmi/ID5/re/lib/lib/libsys_dsi_base.cpp-ne.so`
  * `libsocket.so.3`
  * `libssl.so.2` / `libcrypto.so.2`

---

## Building in QNX Momentics 4.7 (Windows VM)
1. Copy or mount this `Android-Auto-Project` folder into your QNX Momentics VM.
2. In Momentics:
   * Select **File -> Import -> General -> Existing Projects into Workspace**.
   * Browse to this directory and import.
3. Configure target architecture as `armv7le` (Release).
4. Point external include and library search paths to the sysroot extracted from `D:\re\BACKUP-DO-NOT-TOUCH\net\hu-omap\`.
5. Build target `nbt-aad`.

---

## Deployment to Vehicle
1. Copy `nbt-aad` to a FAT32 USB drive.
2. Plug into the Supra's armrest USB port and SSH into the head unit:
   ```sh
   # Mount filesystem read-write
   mount -uw /fs/sda0

   # Copy daemon to executable storage
   cp /fs/usb0/nbt-aad /opt/car/bin/nbt-aad
   chmod +x /opt/car/bin/nbt-aad

   # Remount read-only
   mount -ur /fs/sda0

   # Run daemon in background
   /opt/car/bin/nbt-aad &
   ```
