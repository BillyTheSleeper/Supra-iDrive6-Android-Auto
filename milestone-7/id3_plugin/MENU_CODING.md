# MENU_CODING.md — Android Auto / CarPlay on the Intel NBT HMI

Research mined from **`DAviS0d/BMW-NBT-Emulator`** (runs the original `HU_NBT`
Intel QNX 6.5 x86 + `HmiMain3D` firmware unmodified in QEMU — the same HMI
generation as our id3 head unit), cross-referenced against our
`id3_plugin` (`NbtAad.xml`, `nbt_aad_app.cpp`, `README.md`) and the upstream
ID5/ID6 plugin (`milestone-7/plugin/`, `milestone-7/rhmiapp/`).

Two different mechanisms exist and they answer our two open unknowns separately:

| unknown | answered by | mechanism |
|---|---|---|
| (a) how the HMI places an "Android Auto" row + selection callback | emulator `native_devices.c` / `patch_external_devices.py` / `trace_nbt_external_devices.py` | **native `CHmiList` "External devices" resource rows + a hooked selection callback** |
| (b) what coding gates A4A / Android Auto / CarPlay | `hmi_native_coding_schema.json` (compiled into `HmiMain3D`) + the shipped NCD | **`A4A` / `REMOTE_HMI` / `A4A_REMOTE_APPS` / `A4A_ANDROID` / Apple bits** |

---

## 1. Menu-entry mechanism (the emulator's native approach)

The emulator does **not** use the `pluginApps` / `CAppManager` path at all. It
adds the two rows directly into the *genuine* "External devices" list resource
and hooks the *genuine* native selection callback. This is the concrete "resource
IDs + callback" answer the task asks for.

### 1.1 The External-devices list

- Resource file: `opt/hmi/data/ro/app/hmimultimedia/hmimultimedia.pba`
  (a protobuf message; the list is found by its component ID **27224 = 0x6a58**).
- The list object is the original `CHmiList` (RTTI `CHmiList`, vtable address
  **0x0945a530**).
- **Genuine row item IDs (pre-existing in the firmware):**

  | item ID | row |
  |---|---|
  | 1 | USB1 |
  | 2 | USB2 |
  | 4 | Bluetooth |
  | 11 | WLAN |
  | 5 | AUX |

- The emulator **appends** two rows (item IDs **12 = Android Auto**, **13 = CarPlay**)
  by cloning the USB1 (ID 1) row template and *omitting protobuf field 3* — i.e.
  **no feature/coding gate is imposed** on the added rows
  (`tools/patch_external_devices.py`: `feature_gate_removed: True`). This is
  important: the firmware's own rows are coding-gated, so the emulator strips the
  gate to force the entries visible.

So, precisely: **Android Auto = item ID 12, CarPlay = item ID 13** in the
External-devices list (component 27224). These are *not* present in stock
firmware; they are appended experimentally. The README's "genuine resource rows"
means they live in the genuine resource/list (not a host overlay), not that they
ship in the stock file.

### 1.2 The native list API used to label/enable a row

From `trace_nbt_external_devices.py` ("native_list_analysis") and
`native_devices.c` (`set_native_label`), the `CHmiList` vtable slots are:

| vtable offset | meaning | used as |
|---|---|---|
| `+0x78` | `setItem(list, labelObject, rowIndex, column)` | set the row's label |
| `+0x7c` | `setItemEnabled(list, rowIndex, enable)` | enable/disable a row |
| `+0x80` | `enabled(list, rowIndex)` | query |
| `+0x8c` | `findIndex(list, itemID) -> rowIndex` | map itemID to row |
| `+0xc8` | returns the signal the init registers the selection callback on | — |

Label object ctor/dtor (UTF-16 native string, 28-byte `native_string_source`
layout): ctor **0x080cd116**, dtor **0x080cca5a**.

### 1.3 Init + selection callback (what the emulator hooks)

- Controller **init at `0x084572f8`**: calls `0x080ae8b0(componentID=0x6a58)`
  (the `CHmiList` lookup) and stores the returned list at `controller+0x7c`.
- **Selection callback at `0x08453b96`**: reads the item ID from the **4th stack
  argument**, subtracts 1, **rejects values > 10**, then dispatches through a
  **jump table at `0x094fd704`** (11 entries → item IDs 1..11). Original IDs
  `12`/`13` would be rejected by stock code — that is exactly why the emulator
  must hook the callback.
- The emulator's `devices_init_hook` (on init) sets native labels for IDs
  `4` (Bluetooth "(PC)"), `12` (Android Auto), `13` (CarPlay) and enables them;
  `devices_select_hook` intercepts `item == 4 || 12 || 13` and publishes a
  **mailbox selection** instead of calling the original handler
  (`work/bridge-v2/guest/native_devices.c`).
- `native_menu.c` additionally hooks the main-menu event handler at **`0x081ad936`**
  so that selecting the Navigation row (row 3) publishes selection **14**; the
  sport list (component **32013**, row **17**) is enabled the same way.

### 1.4 Routing to the host transport (mailbox)

- Guest mailbox (`native_addon.c`): 36 bytes, magic `0x5854424e`, version 1;
  fields `{magic, version, command_sequence, power, torque, event_sequence,
  selected, applied_sequence, status}`. `nbt_native_mailbox_selection(id)` writes
  `selected` and bumps `event_sequence`.
- Selection IDs → host transport (viewer `_activate_extension` accepts
  `'android_auto' | 'carplay' | 'navigation' | 'bluetooth'`):

  | selection ID | host surface |
  |---|---|
  | 12 | Android Auto |
  | 13 | CarPlay |
  | 4 | Bluetooth |
  | 14 | Navigation |

- The host side (`native_mailbox.py` `exchange()`) reads `selected`/`applied` from
  the owned QEMU guest RAM and emits a `native_device_selected` event carrying the
  matching `source` kind.

---

## 2. Coding (what gates A4A / Android Auto / CarPlay)

The coding descriptors are **compiled into `HmiMain3D`** (they are not an external
CAFD XML): a 3100-record table at **VA 0x09879ea8** (`hmi_native_coding_schema.json`,
record_size 36; fields `record_va, block_id, parameter, first_byte, last_byte,
offset, mask, compare_value, feature_id, feature_name`), corroborated by
`CVACoding::isCodingFeature` at `0x089e0a9e`. The HMI receives the raw NCD bytes via
`boot/lib/libnbt_offline_syset.so` (8 coding blocks **0x3000..0x3007**).

The repo ships `coding/CAFD_00000DED_003_012_025.ncd` (S-records; CAF_ID
**00000DED**, coding version **003.012.025**). I decoded it and read every
relevant parameter against the schema. **The shipped NCD has all AA/CarPlay gates
set to "activ"** — i.e. it is the "everything enabled" reference.

### 2.1 The gate parameters (block / byte / mask / compare)

`mask` is the bit mask within `byte` (block offset); `compare_value` is the
"active" value. Byte addresses are block-relative (block 0x3000 lives at
S-record address `0x300000`, so block `0x3000` byte `107` = S-record `0x30006B`).

| parameter | block | byte | mask | active | shipped NCD raw → value |
|---|---|---|---:|---|---|
| **A4A** (main Android-Auto gate) | 0x3000 | 107 | 0x80 | 1 | 0x97 → **1 (activ)** |
| **REMOTE_HMI** | 0x3000 | 107 | 0x70 | 1 (`va_REMOTE_HMI`; 0=nicht_aktiv, 2=restricted) | 0x97 → **1** |
| **A4A_REMOTE_APPS** | 0x3000 | 108 | 0x01 | 1 | 0xd5 → **1 (activ)** |
| A4A_SWT (screen-mirror bit) | 0x3000 | 110 | 0x80 | 1 | 0x69 → **0 (off)** |
| A4A_KML | 0x3000 | 19 | 0x80 | 1 | 0x82 → **1** |
| A4A_XL | 0x3000 | 19 | 0x40 | 1 | 0x82 → 0 |
| A4A_PIA | 0x3000 | 20 | 0x01 | 1 | 0x81 → **1** |
| A4A_LEGAL_DISCLAIMER | 0x3000 | 112 | 0x10 | 1 | 0xcb → 0 |
| A4A_VOICE_APP_SUPPORT | 0x3000 | 129 | 0x80 | 1 | 0xdb → **1** |
| GOOGLE_LOCAL_VOICE_SEARCH | 0x3000 | 134 | 0x40 | 1 | 0x00 → 0 |
| A4A_STT_SPELLER_INPUT | 0x3000 | 135 | 0x01 | 1 | 0x1f → **1** |
| A4A_STATUS_BAR_NOTIFICATION | 0x3000 | 135 | 0x02 | 1 | 0x1f → **1** |
| A4A_CALENDER_WIDGET | 0x3000 | 135 | 0x04 | 1 | 0x1f → **1** |
| A4A_PROD_YEAR | 0x3000 | 95 | 0xf0 | 4 = 2014 | 0x47 → **2014** |
| A4A_PROD_MONTH | 0x3000 | 95 | 0x0f | 7 = jul | 0x47 → **jul** |
| **APPLE_ENHANCEMENTS** (CarPlay) | 0x3000 | 22 | 0x80 | 1 | 0x97 → **1 (activ)** |
| SPECIAL_VEHICLE | 0x3000 | 24 | 0xc0 | 1/2/3 | 0x17 → 0 |
| **A4A_ANDROID** (Android Auto specific) | 0x3002 | 81 | 0x02 | 1 | 0xfe → **1 (activ)** |
| **API_APPLE_AUTH_CHIP_ON_OFF** (CarPlay) | 0x3003 | 22 | 0x40 | 1 (ON) | 0xef → **1 (ON)** |
| **PIA_A4A** | 0x3004 | 2 | 0x20 | 1 | 0x63 → **1 (activ)** |
| **PIA_A4A_WHITELIST_CHECK** | 0x3004 | 2 | 0x40 | 1 | 0x63 → **1** |

### 2.2 What actually gates the two entries

- **Android Auto** = `A4A` (0x3000/107/0x80) **and** `A4A_ANDROID` (0x3002/81/0x02),
  with `A4A_REMOTE_APPS` (0x3000/108/0x01) and `REMOTE_HMI` (0x3000/107/0x70) for the
  remote-apps/remote-HMI plumbing.
- **CarPlay** = `APPLE_ENHANCEMENTS` (0x3000/22/0x80) **and**
  `API_APPLE_AUTH_CHIP_ON_OFF` (0x3003/22/0x40).
- `PIA_A4A` + `PIA_A4A_WHITELIST_CHECK` (0x3004/2) gate the phone-integration-app
  side of A4A.

### 2.3 `TOUCH_COMMAND` is not an NBT coding parameter

The schema has **no** `TOUCH_COMMAND` parameter (only unrelated `ZBE_TOUCH*`).
`TOUCH_COMMAND` is an ID5/ID6 RHMI-internal special-app id from the upstream
Supra-iDrive6 project, not a coding byte on this Intel-NBT/iDrive-4 firmware. Our
README's "CSpecialAppsHelper (coding A4A / A4A_REMOTE_APPS / REMOTE_HMI /
TOUCH_COMMAND)" should be corrected to the four *real* NBT gates above.

---

## 3. Cross-reference with our `id3_plugin`

| aspect | emulator (native patch) | our `id3_plugin` (pluginApps) |
|---|---|---|
| placement | append rows 12/13 to the `CHmiList` "External devices" resource (component 27224) | `applicationType="Custom"` + `insertStateID="20"` + `entryButtonFocusID="32752"` + `insertAfterFocusID="2270"` (copied verbatim from upstream `NbtAad.xml`) |
| entry action | hooked selection callback (0x08453b96) intercepts item 12/13/4 → mailbox | `<entryButton action="2">` + `raAction id="2"` → `onEntryAction` → `startDaemonOrShow()` |
| labels/icons | native UTF-16 label objects via list vtable `+0x78`/`+0x7c`/`+0x8c` | `textIdModel`/`imageIdModel` + `NbtAad_ALL_Texts.zip`/`Images.zip` |
| gate | strips protobuf field 3 (no coding gate on appended rows) | `CSpecialAppsHelper` gate on coding (the unknown) |

Key conclusions from the cross-reference:

1. **The emulator's row IDs (12/13) and callback are for a *different* entry
   point** than our plugin: they put "Android Auto"/"CarPlay" inside
   **Multimedia → External devices**, whereas our plugin targets a **top-level
   main-menu entry** (insertStateID 20) plus its own menu state. They are
   complementary, not interchangeable.
2. **The coding gate is the real missing piece for us.** The emulator had to
   *strip* the feature gate to make its rows show, which confirms the firmware
   *does* gate these entries on coding. For our plugin to appear/be selectable,
   the id3's CAFD must have the A4A bits **activ** — exactly the bits the
   emulator's NCD already has set.
3. Our XML vocabulary (Custom/insertStateID/entryButtonFocusID) is the upstream
   ID5/ID6 vocabulary and is **not** proven on the id3's older framework; the only
   proven id3 `App` plugin (`SecurityMenu`) uses a Settings placement.

---

## 4. Concrete changes for our `id3_plugin`

### 4.1 Coding (the gate — do on the id3, not in the plugin files)

Ensure the id3's `CAFD_00000DED` (same CAFD as the emulator) has these set:

- `A4A` = activ → block **0x3000** byte **107** bit **0x80** = 1
- `REMOTE_HMI` = 1 → block **0x3000** byte **107** bits **4-6** (`0x70`) = 1
- `A4A_REMOTE_APPS` = activ → block **0x3000** byte **108** bit **0x01** = 1
- `A4A_ANDROID` = activ → block **0x3002** byte **81** bit **0x02** = 1
- (for CarPlay later) `APPLE_ENHANCEMENTS` = 0x3000/22/0x80 = 1 and
  `API_APPLE_AUTH_CHIP_ON_OFF` = 0x3003/22/0x40 = 1

The shipped `CAFD_00000DED_003_012_025.ncd` already matches, so it can be used as
the byte-for-byte reference. On a real id3 this is an E-Sys FDL change in the
`3000`/`3002` function addresses.

### 4.2 `NbtAad.xml`

- Keep `applicationType="Custom"`, `applicationWeight="200"`, `insertStateID="20"`,
  `entryButtonFocusID="32752"`, `insertAfterFocusID="2270"` (matches upstream
  main-menu placement). **No change required by this research**, but treat the
  `Custom` main-menu insertion as *still unverified* on the id3.
- Lower-risk fallback if the main-menu entry does not appear: switch to
  `applicationType="Multimedia"` (see upstream `rhmiapp/NbtAad_Multimedia.xml`) so
  "Android Auto" lands in **Multimedia** — the same menu the firmware's own
  External-devices list lives in, and the same place the emulator shows its rows.
- The entry action `2` and `raAction`s already route to `nbt_aad_app.cpp`, which
  is correct.

### 4.3 `nbt_aad_app.cpp`

- No ABI/layout change needed from this research. The one *additive* thing worth
  doing: in `onInit()` (or a poll tick), log the effective A4A gate so a
  "entry never shows" symptom can be diagnosed as coding-vs-placement. There is
  no public `CSpecialAppsHelper` symbol to call; the practical check is whether
  `onInit` runs and the entry renders after setting the §4.1 bits.

### 4.4 `README.md` (id3_plugin)

- Correct blocker #1's phrasing: the gate is `A4A` + `A4A_REMOTE_APPS` +
  `REMOTE_HMI` (and `A4A_ANDROID` for AA), with the exact block/byte/mask above;
  `TOUCH_COMMAND` is an ID5/ID6 app-id, not an NBT coding byte.

---

## 5. Blockers / unknowns

1. **id3 `Custom` main-menu insertion is unproven.** The emulator bypasses the
   plugin framework entirely (native patch), so it does not confirm our
   `insertStateID="20"` / `entryButtonFocusID="32752"` placement on the id3. Only
   a bench test on the id3 confirms it; `applicationType="Multimedia"` is the
   safer first target.
2. **Connector id 210** is still unconfirmed as free/valid on the id3 (SecurityMenu
   uses 200). The gate is coding, not the connector id, so this is secondary.
3. **`CSpecialAppsHelper` is not referenced anywhere in the emulator repo**, so its
   exact id3 behavior is inferred, not proven; the coding bits in §4.1 are the
   best available proxy for the gate.
4. The emulator's "Android Auto / CarPlay" rows are **experimental** (item IDs
   12/13 appended, no coding gate) — the stock `hmimultimedia.pba` contains only
   USB1/USB2/Bluetooth/WLAN/AUX. There is no genuine, pre-existing AA/CarPlay row
   in the firmware to reuse; the "genuine" part is only the list/callback API.
5. Text/Image DB zips and the GNU-vs-Dinkumware runtime concern are unchanged from
   `id3_plugin/README.md` (not addressed by this research).
