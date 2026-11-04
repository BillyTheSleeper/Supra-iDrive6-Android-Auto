# id3 plugin — "Android Auto" iDrive menu entry (Intel NBT / iDrive 4/5, QNX x86)

This directory contains the id3-side plugin that adds an **Android Auto** entry to the iDrive menu and
drives the ported NBT-AAD daemon. It mirrors the daemon-control logic of the upstream
`plugin/nbt_aad_plugin.cpp` and `rhmiapp/nbt_aad_app.cpp`, but is written against the **id3's older HMI
framework**, which is *not* the ID5/ID6 RHMI framework.

## Files

| file | purpose |
|------|---------|
| `NbtAad.manifest`   | plugin manifest (name / type / `.so` path / connector / `DescFile`) |
| `NbtAad.xml`        | UI description (entry button + a menu state with Start / Force Stop / Restart) |
| `nbt_aad_app.cpp`   | the plugin's shared library: daemon control (start/show/quit/restart) + status polling |
| `Makefile`          | builds `libNbtAad.so` for QNX x86 (`qcc -Vgcc_ntox86_gpp`) |
| `lib/`              | copies of the id3's own `libAppFramework.A.so` / `libCoreFramework.A.so`, for linking only |

Build (toolchain at `/opt/qnx650/...`, QNX 6.5.0 x86):

```
export QNX_HOST=/opt/qnx650/host/linux/x86
export QNX_TARGET=/opt/qnx650/target/qnx6
export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
make ARCH=x86          # -> libNbtAad.so
```

It compiles (`libNbtAad.so`, ELF32 i386, `pluginAppInfo` 20 bytes, 4-arg `bmw::PluginApp` ctor).

## The id3 HMI plugin framework (what was found)

The id3's HMI is **not** ID5/ID6 RHMI. There is no `/opt/hmi/ID*`. The HMI is one binary,
`/fs/sda0/opt/hmi/bin/zipped/HmiMain3D` (~25 MB), linked against the older "hmi2012" App Framework in
`/fs/sda0/opt/hmi/lib/libAppFramework.A.so` + `libCoreFramework.A.so`. Plugins live under
`/fs/sda0/opt/hmi/data/ro/Plugins/`.

Two plugin examples exist:

1. **`TerminalUIApp`** (`data/ro/Plugins/TerminalUIApp/`) — a built-in demo app whose UI is driven by the
   `hmi2012` legacy `CTerminalUISubsystem` that is compiled *into* HmiMain3D. Its manifest is
   `TerminalUIApp.terminalui` and uses `<file>{staticLibrary}</file>`.
2. **`SecurityMenu`** (`data/ro/app/hmisettings/Plugins/SecurityMenu/`) — a *real*, loadable `App` plugin.
   Its manifest is `SecurityMenu.manifest` with `<file>{pluginDir}/libSecurityMenu.so</file>` and it ships
   a shared object.

### Manifest format

`<Name>.manifest` is the manifest the plugin loader reads (HmiMain3D contains the literal string
`.manifest`; the `.terminalui` extension is only the TerminalUIApp special case). It is XML:

```xml
<plugin>
  <name>NbtAad</name>
  <type>App</type>
  <file>{pluginDir}/libNbtAad.so</file>
  <config>
    <autoStart>yes</autoStart>
    <dependencies>
      <pluginAppConnector><type>Etch</type><id>210</id><asyncCallbacks>yes</asyncCallbacks></pluginAppConnector>
    </dependencies>
    <parameters>
      <DescFile>{pluginDir}/NbtAad.xml</DescFile>
      <TextDBFile>{pluginDir}/NbtAad_ALL_Texts.zip</TextDBFile>
      <ImageDBFile>{pluginDir}/NbtAad_ALL_Images.zip</ImageDBFile>
    </parameters>
  </config>
</plugin>
```

### `{staticLibrary}` meaning

`<file>{staticLibrary}</file>` means the plugin's code is **statically linked into HmiMain3D** (as the
`TerminalUIApp` subsystem is). It is *not* a path you can point at a `.so`; the only working way to add a
new plugin at runtime is a **shared object** named by `<file>{pluginDir}/libX.so</file>` (the SecurityMenu
pattern). That is what this plugin does.

### Discovery / loading

- HmiMain3D's `CAppManager` scans every `<ro|rw>/Plugins/<Name>/<Name>.manifest` at boot, `dlopen()`s
  `<file>`, and `dlsym()`s the symbol **`pluginAppInfo`** (verified in the binary: the string
  `pluginAppInfo` is passed to `DynamicLibrary::findSymbol`, and the result's `+0x0c` and `+0x10` fields are
  checked non-NULL — those are the `create` / `destroy` function pointers).
- `pluginAppInfo` layout on the id3 is **20 bytes** (confirmed against `libSecurityMenu.so`'s relocations):

  ```c
  struct pluginAppInfo {
      uint16_t versionMajor;   // 1
      uint16_t versionMinor;   // 0
      const char* name;        // "Android Auto"
      const char* vendor;      // "NBT-AAD"
      IPluginApp* (*create)(IDispatcher&, const char*, const Bundle&, const Bundle&);
      void (*destroy)(void*);
  };                           // NO rhmiInterfaceVersion field
  ```

  The ID5/ID6 RHMI framework adds a `uint32_t rhmiInterfaceVersion` between `vendor` and `create`
  (24 bytes). **Using the 24-byte layout on the id3 would be interpreted as create=1, destroy=&create and
  crash** — so `nbt_aad_app.cpp` uses the 20-byte layout.

- `create` constructs a `bmw::PluginApp` and returns it; the framework calls `destroy` on unload.
  `bmw::PluginApp` on the id3 has a **4-arg constructor**
  `PluginApp(IDispatcher&, BasicString<char>, const Bundle&, const Bundle&)` (no major/minor/rhmiVersion,
  which the ID5/ID6 constructor has). Its vtable slots are identical to ID5/ID6: `onError = 21`,
  `onInit = 22`, `onFinalize = 23` (34 slots), so the rhmiapp's vtable-patching approach is reused verbatim.

### The connector

- `TerminalUIApp` uses `<type>ICE</type><id>140</id>` (ICE/TUI service), `SecurityMenu` uses
  `<type>Etch</type><id>200</id>`, the upstream NbtAad uses `<type>Etch</type><id>210</id>`.
- For a normal `App` plugin the connector is **Etch** with a per-plugin id. The `<id>` is consumed by
  `CHmiIntegrator::integratePluginApplication` (`AppID`); the gate is `CSpecialAppsHelper`
  (coding `A4A` / `A4A_REMOTE_APPS` / `REMOTE_HMI` / `TOUCH_COMMAND`). We reuse `210` (upstream) here.

## Daemon control protocol (mirrored from upstream)

The plugin talks to the ported `nbt-aad` daemon exactly like the upstream app/plugin do (see
`milestone-7/src/main.cpp`):

- **Single-instance control port**: UDP `127.0.0.1:52790` (`NBT_CTL_PORT` overrides).
- **Commands** (plain text datagrams): `ping` → `pong` (liveness), `show` (bring overlay to front),
  `quit` (clean stop), `hide` (step aside), `gear <n>`, `pdc <n>`, `daynight <n>`,
  `drivemode <prop> <n> [prev]`.
- **UI-state file** `/dev/shmem/nbt-aad-state`, written by the daemon as
  `"%-11.11s %10d %d\n"` → `state` (`idle`|`connecting`|`connected`) `pid` `overlay` (1 = on screen,
  2 = BACK held, 3 = MENU/COM/MEDIA/NAV/MAP just went to the phone). Read with `fscanf("%23s %d %d")`;
  a stale record with a dead pid counts as idle (pid liveness via `kill(pid,0)`), and the daemon answers
  `ping` on the control port as a fallback.
- **Start** = spawn `/opt/aa/nbt-aad` (or send `show` if already running).
  **Stop (Force Stop)** = send `quit`, then `SIGTERM` after 3 s, `SIGKILL` after 10 s.
  **Restart** = stop, then start again when the pid is gone.

The plugin does exactly this in `nbt_aad_app.cpp`: `launchDaemon()` (QNX `spawn`), `sendToDaemon()`
(UDP datagram), `readDaemonState()` (state file + `ping`), `beginStop()`/`serviceStop()` (quit + signal
escalation), and a self-reposting `bmw::Runnable` that polls the state once a second to update the entry
text ("Connecting…" / "Open Android Auto").

## Install path on the id3

```
/fs/sda0/opt/hmi/data/ro/Plugins/NbtAad/
├── NbtAad.manifest
├── NbtAad.xml
├── NbtAad_ALL_Texts.zip
├── NbtAad_ALL_Images.zip
└── libNbtAad.so
```

(The `<ro>` partition is read-only at runtime; on this head unit the plugins dir is on `/fs/sda0`, which is
rewritten on update — see `create_sdc.sh` / the `/fs/sda0/repository/` mirror.)

## Blockers / unknowns

1. **Entry placement / `applicationType`.** The exact `applicationType` (and `insertStateID` /
   `entryButtonFocusID` / `insertAfterFocusID`) that places the row in the *main menu* was verified on the
   ID5/ID6 NbtAad (`Custom` + `insertStateID="20"`) but **not** on the id3 — its only `App` example
   (`SecurityMenu`) uses `Settings__SpecialVehicle` (lands in Settings). The id3's `Custom` support and the
   `CSpecialAppsHelper` coding gate must be confirmed on the car.
2. **Connector id.** We reused Etch `210`; the correct/free id for this head unit's coding is unconfirmed.
3. **Text/Image DB zips.** `NbtAad_ALL_Texts.zip` / `NbtAad_ALL_Images.zip` must be generated for the id3
   (text ids 1/2/10/13/14 = "Android Auto" / "Android Auto: Connecting…" / "Start Android Auto" /
   "Connecting…" / "Open Android Auto"; image ids 55001/55002), matching the upstream `make_db.py`.
4. **Runtime library.** `libNbtAad.so` links GNU `libstdc++.so.6` (`-Vgcc_ntox86_gpp`, as instructed).
   That library exists on the id3 (`/fs/sda0/lib/libstdc++.so.6`), but the id3's own plugins/HmiMain3D use
   Dinkumware `libcpp-ne.so.4`. If the GNU runtime fails to resolve, rebuild with `-Vgcc_ntox86` (Dinkumware).
5. **Daemon install path.** The plugin spawns `/opt/aa/nbt-aad` (upstream default). Point `kDaemon` at the
   actual location of the ported daemon on this id3.
