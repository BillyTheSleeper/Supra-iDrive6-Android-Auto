# id3 (Intel NBT) H.264 decode + display — findings and plan

## 1. TL;DR

The id3 head unit **does not have the mm-renderer hardware decode path** that the
daemon currently assumes (`libmmrndclient.so` / `/dev/name/local/mdev` / TI
IVA-HD). That whole chain is a *TI Jacinto* feature of the NBT Evo; the id3 is an
**Intel Atom "Tunnel Creek" (Queensbay) + PowerVR SGX535** platform.

Instead the id3 ships a **Cinemo "NME" multimedia framework** (`/fs/sda0/opt/mm/lib/`)
that contains a **software H.264/AVC decoder**. It is not exposed as a single
"decode this buffer" call — it is a COM-style codec plugin inside the NME filter
graph. The two realistic routes for the daemon are:

1. **Primary — call the NME software codec plugin directly** (`NmeCreateCodecNvdec`
   with codec name `"h264codec"` in `libNmeVideoSW.so`), decode to NV12/I420, colour
   convert, and post to the existing QNX Screen window. This reuses code already on
   the unit.
2. **Fallback — cross-compile a standalone software H.264 decoder** (FFmpeg
   `libavcodec`, or OpenH264) for QNX x86 with `qcc` and link it into the daemon.

There is **no usable hardware H.264 decoder** (see §3).

## 2. Inventory: what the id3 actually has

### 2.1 Display / GPU (confirmed)

```
/fs/sda0/lib/graphics/tunnelcreek/
    libscreen.so.1       QNX Screen
    screen-pvr2d.so      Screen -> PowerVR 2D backend (blit/scanout)
    screen-sw.so         software Screen backend
    libpvr2d.so          PowerVR 2D blit API
    pvrsrv.so / pvrsrvinit.so / libsrv_um.so   PowerVR services (SGX)
    libWFD.so.1 / libWFDtunnelcreek.so          WiFi-Display
/fs/sda0/lib/graphics/libEGL.so.1, libGLESv2.so.1, libKD.so.1
```

- `libscreen.so` **is present** (at a non-default path), so the daemon's existing
  `screen_*` window code can work on the id3 — but the search path in
  `video_manager.cpp` only tries `/opt/conn/...` and `/opt/mm/...`; the id3 libs are
  under `/fs/sda0/lib/graphics/tunnelcreek/`. This needs to be added (or rely on the
  system's default loader path).
- `/dev/name/local/pvrsrv` exists (PowerVR services) — same compositor the daemon
  already assumes via `libscreen`.

### 2.2 Multimedia framework — Cinemo NME (`/fs/sda0/opt/mm/lib/`)

```
/fs/sda0/opt/mm/lib/
    libmme.so.1             MME control/player API (bookmarks, DVD, mediacopier) — NOT a decode API
    libmmfilter.so.1        NME filter-graph plumbing (Mmf*/MmcPassive* + MmInitializeColourSpaceConversion)
    libNme*.so              NME base classes / image / tag / utils
    libNmeImage.so          NeImgdec_* (still-image decode: JPEG/PNG/MJPG)
    libNmeUtils.so          NeVP6Decoder_* (VP6), FFmpeg-derived DSP (vp6d_put_h264_chroma_mc8_sse2, ...)
/fs/sda0/opt/mm/lib/cinemo/
    libNmeVideo.so          NmeCreateVideo — video filter factory (parses H.264 level from SPS)
    libNmeVideoSW.so        NmeCreateCodecNvdec  — SOFTWARE video decoder (incl. h264codec)
    libNmeVideoHW.so        NmeCreateCodecNvdecHW — "hardware" decoder (DXVA), DIVX/XVID/H263 only
    libNmeVideoMisc.so      NmeCreateCodecFFmpeg — FFmpeg wrapper, ORPHANED (no libavcodec on the unit)
    libNmeVideoRenderer.so  NmeCreateVmr / NmeCreateVmrNull — video mixing renderer
    libNmeVideoDevice.so    NmeCreateDeviceScreen / NmeCreateDeviceGF / NmeCreateDeviceCallbackVideo ...
    libNmeVideoSW/HW/...    nme_scale_* / nme_blit_* / nme_yuva8_to_rgba8 colour routines (SW/RGB paths)
```

Also present but not H.264 decode:
- `/fs/sda0/opt/conn/gstreamer/` — GStreamer 0.10 (app, decodebin, playbin, vp8,
  matroska, ogg, vorbis, flump3, ffmpegcolorspace, qnxaudio). **No H.264 decoder
  element** (no `libgstlibav`, no `openh264`, no `h264parse`).
- `/fs/sda0/opt/conn/lib/libencoderdecodercollection.so` — text codecs (Base64 /
  Quoted-Printable), irrelevant to video.
- `/fs/sda0/opt/sys/bin/videocapturetest` + `/dev/videocapture`, `/dev/nvideocapture`,
  `/fs/sda0/opt/sys/lib/libwal_nvidia.so` — **analog video capture** (NTSC/PAL
  camera/aux) via `devctl(DEVCTL_VP_*)`, not H.264 decode.
- `/fs/sda0/opt/sys/bin/vdev-medialaunch` — USB mass-storage / iPod launcher, not video.

## 3. Is there a hardware H.264 decoder? — No

Evidence:

1. **`/dev/name/local/mdev` does not exist** on the id3 (only `pvrsrv` and `sdio` are
   under `/dev/name/local`). The daemon's `name_open("/dev/name/local/mdev")` and its
   Message 2/3/4/5 protocol (`sendMdevFormat/sendMdevConfig/...`) target the TI IVA-HD
   engine and have no counterpart here.
2. `libNmeVideoHW.so` (`NmeCreateCodecNvdecHW`, the "hardware" NVDEC/DXVA codec)
   registers **DIVX / XVID / H263 only**. It contains `hDXVA` and the full
   `NvdecError_DXVA*` error set, but no `h264codec` / `H264` / `AVC` string or symbol.
3. `libNmeBaseClasses.so` exposes `T_VIDEO_HARDWARE_DECODE` and
   `T_VIDEO_MAX_H264_LEVEL` config globals, but on this build the H.264 path is only
   realised in the **software** codec (`libNmeVideoSW.so`), which resolves its DSP to
   `NeImgdec_*` (image) and `NeVP6Decoder_*` (VP6) — no GPU/driver call.
4. No `libavcodec`/`libav*` exists anywhere under `/fs/sda0/opt` (grep for
   `avcodec_decode_video2` finds nothing), so `NmeCreateCodecFFmpeg` cannot load.

Conclusion: **H.264 decode must be software** — either the on-unit NME `h264codec`, or
a codec the daemon brings itself.

## 4. The exact decode API (software H.264)

### 4.1 Library and symbols

| Library | Symbol | Role |
|---|---|---|
| `libNmeVideoSW.so` | `NmeCreateCodecNvdec(NmeClassAttributes*)` | create the SW codec instance (factory) |
| `libNmeVideoSW.so` | `NmeModuleInfo` | module registration (name + codec list) |
| `libNmeBaseClasses.so` | `NmeCreateChunker(void**, uint, const CinemoMediaType&, const char*)` | create the AVC bitstream chunker (`NmeChunkVideo`) |
| `libNmeBaseClasses.so` | `NmeDetectAVC(const char*, int)` | sniff AVC bitstream |
| `libNmeBaseClasses.so` | `NmeAnalyseAVCSpecificConfig(CinemoMediaType&, const char*, int)` | parse SPS/PPS ("avcC") into a media type |
| `libNmeBaseClasses.so` | `NmeChunkVideo::DecodeAVC*`, `WriteAVCAnnexB`, `WriteAVCIsoMP4`, `WriteAVCHeaders` | Annex-B <-> AVCC handling |
| `libNmeBaseClasses.so` | `NmeClassDLL::Load/CallCreate`, `NmeClassFactory::CreateInstance` | plugin loader (alternative to direct `dlsym`) |
| `libNmeVideoDevice.so` / `libNmeVideoRenderer.so` | `nme_yuva8_to_rgba8`, `nme_rgba8_to_bgra8`, `nme_scale_luma_internal`, `nme_scale_chroma_internal`, `nme_blit_rgba32` | YUV->RGBA / blit |

Codec name string (the value passed in `NmeClassAttributes`): **`"h264codec"`**
(confirmed in `libNmeVideoSW.so` strings, alongside `vc1codec`, `dvcodec`,
`imgcodec`, `theora`).

Output pixel format: **NV12** (the codec registers `NV12`/`I420` output formats;
`NV12u` is the default hardware-friendly format).

### 4.2 How `NmeCreateCodecNvdec` selects the codec (from disassembly)

`NmeCreateCodecNvdec`:
1. reads a string at `attrs + 0x14` (the codec/class name),
2. compares it against the registered name table (`"h264codec"`, `"vc1codec"`, ...),
3. allocates a `0xb8`-byte object, initialises it, and invokes
   `vtable[1]` (offset `0x04`) then `vtable[5]` (offset `0x14`, passing `attrs`).

This is the standard NME COM layout: `vtable[0] = QueryInterface`,
`vtable[1] = AddRef`, `vtable[2] = Release`, and the codec methods (`Init`/`SetType`/
`AddData`/`Decode`/`GetOutput`) follow. The precise method order/signatures of the
`INmeVideoCodec` vtable still need to be recovered (Ghidra against
`libNmeVideoSW.so` / `libNme.so` using the known `QueryInterface`/`AddRef`/`Release`
indices as anchors) before the direct interface can be coded up — see §6/§7.

## 5. Presentation path

Decoded frames should be presented the same way the daemon already does, but with one
extra step (colour conversion) because there is no mm-renderer `screen_writer` doing
scanout for us:

```
phone H.264 (Annex-B NALs)
   -> NME h264codec (software)            # libNmeVideoSW.so + libNmeBaseClasses.so
   -> NV12 / I420 frames
   -> colour convert NV12 -> RGBA8888     # nme_yuva8_to_rgba8 / nme_scale_* or hand-rolled
   -> memcpy into the Screen window buffer # SCREEN_PROPERTY_POINTER
   -> screen_post_window(win, buf, ...)    # PowerVR screen-pvr2d compositor scanout
```

- Keep the existing `libscreen.so` window (id "1280", `SCREEN_USAGE_VIDEO`/native
  buffers, z-order 2000, group `cpvideogrp<pid>`). On the id3, `libscreen.so.1` lives
  in `/fs/sda0/lib/graphics/tunnelcreek/`; ensure it is found (extend the loader path
  or set `LD_LIBRARY_PATH` in the daemon's launcher).
- `SCREEN_PROPERTY_POINTER` on a `SCREEN_USAGE_VIDEO` window may be `NULL` on some
  backends (the existing code already guards for this and uses `screen_fill`). For
  CPU-written decoded frames, use `SCREEN_USAGE_WRITE|SCREEN_USAGE_NATIVE` (or a
  second staging buffer) so `screen_get_buffer_property_pv(SCREEN_PROPERTY_POINTER)`
  returns a CPU-mappable pointer, then `screen_post_window`.
- The `LayerManagerControl` / surface-1280 / `cpvideoctx` logic in
  `video_manager.cpp` is **NBT-Evo/Genivi specific and does not exist on the id3**;
  those `system()` calls will simply no-op/fail. The plan for the id3 should keep the
  pure `libscreen` window path and skip (or `#ifdef` out) the LayerManager plumbing.

## 6. Concrete code changes to `src/video_manager.cpp`

Routing the phone's H.264 through the NME software codec, in order:

1. **Detect the platform and pick a decode backend.** Add a backend enum to
   `video_manager.hpp`: `DECODE_MMR` (unchanged, NBT Evo), `DECODE_NME_SW` (id3),
   `DECODE_NONE`. In `init()`, if `dlopen` of `libmmrndclient.so` fails (the id3
   case), fall back to loading the NME libs instead of just printing the
   "not found" notice.
2. **Load the NME libs** (new helper `loadNmeCodec()`): `dlopen` (RTLD_NOW|RTLD_GLOBAL)
   `/fs/sda0/opt/mm/lib/cinemo/libNmeVideoSW.so`, `/fs/sda0/opt/mm/lib/libNmeBaseClasses.so`,
   `/fs/sda0/opt/mm/lib/libNmeUtils.so`, `/fs/sda0/opt/mm/lib/libNmeImage.so`.
   `dlsym` `NmeCreateCodecNvdec`, `NmeCreateChunker`, `NmeDetectAVC`,
   `NmeAnalyseAVCSpecificConfig`, and the `nme_*` colour routines.
3. **Instantiate the codec** (in `createDecoder()`'s NME branch): build a
   `NmeClassAttributes` with `name = "h264codec"`, call
   `NmeCreateCodecNvdec(&attrs)` -> `INmeVideoCodec*`. Call `NmeCreateChunker` with a
   `CinemoMediaType` describing `H264`/`AVC1` @ 1280x720 (source canvas) to get the
   AVC bitstream chunker.
4. **Feed the bitstream.** The daemon already collects SPS/PPS
   (`cached_codec_config_`) and prepends it in-band. In the worker loop, route
   `item.data` to the NME codec instead of `MsgSendvs_r(mdev_fd_, ...)`:
   - config packets -> `NmeAnalyseAVCSpecificConfig` (or feed SPS/PPS to the chunker),
   - frame packets -> chunker `DecodeAVC`/`WriteAVCAnnexB` then codec `AddData`.
   Keep the existing drop-keyframe-when-queue-full and IDR logic.
5. **Pull decoded frames.** After each input, loop the codec's "get output" method
   for a decoded NV12 frame; if none, continue.
6. **Convert + present.** `nme_yuva8_to_rgba8` (or `nme_scale_*`) the NV12 plane into
   the Screen window's RGBA pointer, then `screen_post_window`. This replaces the
   `onVideoFrameReceived` test-pattern fallback and becomes the real render path when
   `DECODE_NME_SW` is active.
7. **Teardown** (`destroyDecoder()`/`stop()`): release the codec, chunker, and
   `dlclose` the NME libs, symmetrically with the existing mmr teardown.

All of this is gated so the existing NBT-Evo mm-renderer behaviour is byte-for-byte
unchanged when `libmmrndclient.so` loads successfully.

### Code sketch (decode loop for the id3 path)

```c
// ---- NME software H.264 backend (id3) -----------------------------------
typedef struct {
    void* lib_sw;          // libNmeVideoSW.so
    void* lib_base;        // libNmeBaseClasses.so
    void* (*create_codec)(void* attrs);                 // NmeCreateCodecNvdec
    int   (*create_chunker)(void** out, unsigned, const void* media_type, const char* name);
    int   (*analyse_avc)(void* media_type, const char* data, int len);
    void* codec;           // INmeVideoCodec*
    void* chunker;         // NmeChunkVideo*
} NmeSwDecoder;

static NmeSwDecoder* nme_sw_init(uint32_t w, uint32_t h) {
    // dlopen/dlsym the four libs; on any failure return NULL (no decode).
    NmeSwDecoder* d = calloc(1, sizeof(*d));
    d->lib_sw   = dlopen("/fs/sda0/opt/mm/lib/cinemo/libNmeVideoSW.so", RTLD_NOW|RTLD_GLOBAL);
    d->lib_base = dlopen("/fs/sda0/opt/mm/lib/libNmeBaseClasses.so",  RTLD_NOW|RTLD_GLOBAL);
    // ... libNmeUtils.so / libNmeImage.so ...
    d->create_codec = dlsym(d->lib_sw, "NmeCreateCodecNvdec");
    d->create_chunker = dlsym(d->lib_base, "NmeCreateChunker");   // mangled in C++:
        // _Z16NmeCreateChunkerPPvjRK15CinemoMediaTypePKc
    // Build NmeClassAttributes with name="h264codec" (RE layout: name ptr at +0x14).
    char name[] = "h264codec";
    uint8_t attrs[0x40] = {0};  *(const char**)(attrs+0x14) = name;
    d->codec = d->create_codec(attrs);
    // Build CinemoMediaType {fourcc=H264, width=1280, height=720} (RE layout) + chunker.
    return d;
}

// Called from workerLoop() in place of the MsgSendvs_r(mdev_fd_, ...) block:
static void nme_sw_feed_and_present(NmeSwDecoder* d, VideoManager* vm,
                                    const uint8_t* data, size_t len,
                                    bool is_config) {
    if (is_config)
        d->analyse_avc(&d->media_type, (const char*)data, (int)len);   // SPS/PPS
    // chunker.DecodeAVC() / WriteAVCAnnexB() then codec.AddData(data,len)  (RE vtable)
    // ... drain decoded NV12 frames ...
    uint8_t* dst = vm->screenBufferPtr();   // SCREEN_PROPERTY_POINTER, RGBA8888
    nme_yuva8_to_rgba8(yuv, w, h, dst, dst_stride);   // or nme_scale_luma/chroma_internal
    vm->postWindow(dst);                     // screen_post_window(win, buf, 1, rect, 0)
}
```

(The `RE layout`/`RE vtable` markers are the items still to be pinned down via Ghidra
— see §7.)

## 7. Blockers / unknowns

1. **Exact `INmeVideoCodec` vtable and `NmeClassAttributes` / `CinemoMediaType`
   struct layouts are not yet reverse-engineered.** This is the main implementation
   risk for the "call NME directly" route. Anchor points already known:
   `QueryInterface`@0, `AddRef`@1, `Release`@2, codec-`Init(attrs)`@5 (from the
   factory disassembly); `NmeCreateCodecNvdec(attrs)` reads the name string at
   `attrs+0x14`. Next step is a Ghidra pass over `libNmeVideoSW.so` /
   `libNme.so` to fill in the remaining methods (`Decode`, `AddData`,
   `GetOutputBuffer`, `Flush`).
2. **Output stride / format quirks** (NV12 vs I420, alignment, tiled vs linear) must
   be confirmed on-device; the decoder advertises `NV12`/`I420` but the exact
   negotiated format is only known after `SetOutputType`.
3. **`T_VIDEO_HARDWARE_DECODE` / `T_VIDEO_MAX_H264_LEVEL`** are runtime config
   globals; whether the build honours them (and whether some `.cfg`/PPS turns HW on)
   should be checked, but it does not change the "software only" conclusion for H.264.
4. **`libscreen` loader path** on the id3 is `/fs/sda0/lib/graphics/tunnelcreek/`,
   not the paths the daemon currently probes; must be added or set via
   `LD_LIBRARY_PATH`.
5. **LayerManager / Genivi surface-1280 plumbing is NBT-Evo only** and will not
   function on the id3; the id3 path must rely purely on `libscreen` window
   z-order/visibility.

## 8. Alternative if the NME COM route proves too painful

Cross-compile a standalone software H.264 decoder with the QNX x86 toolchain
(`QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6 qcc
-Vgcc_ntox86_cpp`):

- **FFmpeg `libavcodec`** (H.264, disable everything else) — the natural choice, since
  the on-unit `NmeCodecNvdec` is itself FFmpeg-derived (`scale_mv`, `vp6d_put_h264_*`
  are FFmpeg symbols) and the daemon already links a C++ QNX binary.
- **OpenH264 (Cisco)** — smaller, simpler API, BSD-licensed, easy to wrap.

Either is statically linked into `nbt-aad`, decodes Annex-B to I420, and the same
§5 colour-convert + `screen_post_window` path is used. This avoids all NME COM
reverse-engineering at the cost of pulling in an external codec (and its license).

## 9. Bottom line

- **Decode:** software only — `libNmeVideoSW.so` (`NmeCreateCodecNvdec`,
  codec name `"h264codec"`) + AVC chunker in `libNmeBaseClasses.so`. No hardware
  H.264 (`NmeCreateCodecNvdecHW` = DIVX/XVID/H263; no `mdev` device).
- **Display:** reuse the daemon's `libscreen.so` window (PowerVR
  `screen-pvr2d` compositor), adding an NV12→RGBA colour-convert step before
  `screen_post_window`.
- **Recommended route:** integrate the NME `h264codec` behind a new
  `DECODE_NME_SW` backend, gated to the id3; keep mm-renderer untouched for the
  NBT Evo. Fall back to a ported FFmpeg/OpenH264 if the NME COM interface reverse-
  engineering stalls.
