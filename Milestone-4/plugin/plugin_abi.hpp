// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_AAD_PLUGIN_ABI_HPP
#define NBT_AAD_PLUGIN_ABI_HPP

// ABI of the HmiMain3D plugin framework (PluginFramework in /opt/hmi/ID5/lib/libhmiplugin.so), reversed from
// the shipped A4A bundle (libhmia4a.so, A4ABundle.cpp) on 2026-10-23:
//
//  * HmiMain3D scans <hmi data ro>/hmiplugins/*.xml at boot. Each <Bundle path="stem"> names lib<stem>.so
//    in the same directory; every exportedSymbolName N (bundle and each <Plugin>) must be exported as the
//    data symbol "N" + "Struct" (dlsym, see DynamicLibraryPluginProxy::getPluginStructName).
//  * NStruct = { create, destroy, compiler }. PluginFramework::CompilerCheck refuses the library unless
//    compiler equals the HMI's own string "GCC, Version 4.4.2".
//  * create(void** iface, void** plugin, void** object): iface = the implemented-interface subobject,
//    plugin = the subobject with the plugin vtable, object = the complete object (handed to destroy).
//    A4ABundle returns the same pointer three times; CA4AAudioService returns this+0x14 / this+0x10 / this.
//  * Plugin vtable (bundle and plugins alike): [0] ~D1, [1] ~D0 (deleting), [2] int start(IDependencyResolver&),
//    [3] int stop(IDependencyResolver&). start/stop return 0 on success.
//  * IDependencyResolver vtable: [2] int resolve(const capu::String& name, void** out, int), [4] release(void**).
//    A4ABundle::start resolves "IHmiPluginManager", "CHmiMainDispatcher", "CSvcPlatformBase" (not used yet).
//  * Confirmed in libhmiplugin.so (PluginFramework, 2026-10-23):
//    - XmlScanner::VisitEnter: <Bundle> needs 'path' (lib prefix/suffix added); the library is looked up next to
//      the XML, in ./QNX_ARMV7L/ or ./QNX/ first. Each <Plugin> gets an automatic dependency on its bundle's
//      exportedSymbolName, so the bundle is started before its plugins.
//    - DynamicLibraryPluginProxy::loadInternal: dlsym(name + "Struct"), CompilerCheck::areCompatible against the
//      HMI's own "GCC, Version 4.4.2", then create(&iface, &plugin, &object); ALL THREE must be non-NULL or the
//      proxy logs an error and refuses the plugin (returns 4). LibraryCheck::isValid always returns 1 (no signature).
//    - startInternal / stopInternal call plugin->vtable[2] / [3]; non-zero = logged error, proxy state 4.
//    - unloadInternal calls destroy(object) then dlclose.
//  * BMW's plugins use GNU libstdc++ (libstdc++.so.6), so this library must be built with the _gpp variant,
//    never the Dinkumware _cpp variant the daemon uses.

#include <stddef.h>

namespace nbt {

typedef void (*PluginCreateFn)(void** iface, void** plugin, void** object);
typedef void (*PluginDestroyFn)(void* object);

struct PluginStruct {
    PluginCreateFn  create;
    PluginDestroyFn destroy;
    const char*     compiler;
};

// Layout-compatible with the framework's plugin interface (first four vtable slots).
class IHmiPluginLike {
public:
    virtual ~IHmiPluginLike() {}
    virtual int start(void* dependencyResolver) = 0;
    virtual int stop(void* dependencyResolver) = 0;
};

#define NBT_AAD_PLUGIN_COMPILER "GCC, Version 4.4.2"

// ---------------------------------------------------------------------------------------------
// Stage 3 reverse-engineering notes: HmiMain3D (ET_EXEC, fixed load address, no ASLR; addresses
// below are HmiMain3D virtual addresses = file offset + 0x100000). Firmware-build specific.
//
// MAIN MENU = registered "applications" (players). IHmiHome is a plugin interface (resolve
// "IHmiHome" through the start() dependency resolver, like A4ABundle resolves IHmiPluginManager).
//   IHmiHome vtable: [0] D1, [1] D0, [2] registerPlayer(CHmiApplicationAbstract*) -> bool,
//   [3] unregisterPlayer(app), [6] getActivePlayer(int). Impl: CHmiHomeApplication 0x2f2dac/0x2f2dfc
//   -> CHmiActivityMainMenu (this+0x50) vtbl slot 101 registerPlayer @0x305820 (Mini variant 0x312e94).
//   registerPlayer refuses duplicates (same type) and apps whose slot 22 returns non-zero.
//
// CHmiApplicationAbstract (vtable _ZTV23CHmiApplicationAbstract @0x37afce8 is EXPORTED, address
// point 0x37afcf0, 29 slots); base ctor @0x182a004(this, int type, const SHmiResourceHandle* text,
// int stateId): +0x04 u8, +0x08 type, +0x0c text handle, +0x10 u8, +0x14 0, +0x18 stateId,
// +0x1c..0x28 list, +0x2c 0, +0x30 10.  CHmiCarPlayApplication ctor @0x452cc0 = base(0x10, h, 0x8ef),
// +0x34 = 0, object >= 0x38 B (vtable 0x2b05370).
//   slot  2 0x08 collectDependencies(list)   (pure)        slot 15 0x3c out: handle -> entry+0x4c
//   slot  3 0x0c isAvailable? (CarPlay: 1)    (pure)        slot 16 0x40 out: icon handle (CarPlay
//   slot  4 0x10 init()                        (pure)                0x50000859) -> entry vtbl+0x48
//   slot  6 0x18 forwards to slot 8           (pure)        slot 17 0x44 out: text handle -> entry vtbl+0x3c
//   slot  8 0x20 stateId (CarPlay 0x8ef)       (pure)        slot 19 0x4c out: string handle
//   slot  9 0x24 out: text handle (base: +0x0c) -> entry ctor arg3
//   slot 10 0x28 focusId (CarPlay 0x2535)  -> entry ctor arg1
//   slot 11 0x2c type (base: +0x08)        -> picks entry class: 0x10 CarPlay, 0x11 CarLife, else generic
//   slot 21 0x54 stateId (base: +0x18)     -> entry ctor arg2 (entry+0x68)
//   slot 22 0x58 must return 0 or IHmiHome::registerPlayer ignores the app
//   slot 23 0x5c value -> 0x332464(entry, v)
// App types in this build: 1 Ent, 2 Communication, 3 Navi, 4 ConnectedDrive, 5 Notifications, 6 Car,
// 7 Home, 8 Parking, 9 SideView, 10 NightVision, 15 Common, 16 CarPlay, 17 CarLife, 19 Speech.
// Unknown types only trigger a non-fatal HmiAssertComment in 0x3031a8 (returns -1).
//
// Generic entry CHmiMainMenuEntry: new(0x9c), ctor @0x332c00(this, focusId, stateId, textHandle);
// vtable 0x2ac4b08 (60 slots + secondary at +8). CarPlay's entry only overrides slots 0,1,2.
// slot 2 = execute: default @0x3326a4 = stateManager(FUN_00227768()->+8)->vtbl[7](stateId, 1, 0),
// i.e. it opens HMI state <stateId>. CarPlay's execute 0x460bcc requests the CarPlay UI instead.
// Setters on the entry: slot 15 0x2518fd0 text resource (+0x80), slot 18 0x25188b8 (+0x30/+0x84,
// notify 5), slot 35 0x2518824 (+0x74 hi16).
// Menu providers: CHmiMenuProviderRegistry (plugin instance "CHmiMenuProviderRegistry", getter
// 0x300084); lookup by stateId 0x294528; find entry by focusId over all providers 0x2945fc
// (provider vtbl+0x18(out, provider, focusId)). CHmiMenuProviderBase::addEntry 0x251d160 (vtbl[2]);
// main menu provider created for "ST_Start_Main_Selection" in 0x30b224.
//
// ENTERTAINMENT list = CEntPlayerBase objects (ctor 0x535948(this, IHmiContext*); 47-slot vtable
// 0x2b293a8, slots 15..29 pure). CEntPlayerCarPlay: new(0xb0), ctor 0x45ff08, created by
// CHmiCarPlayApplication::init 0x453008 and initialised via its vtbl[4]; its slot 38 0x4601fc
// creates CHmiCarPlayEntMenuEntry. Needed for the Media/Entertainment entry + audio source (later).
//
// Resource handles: top byte = SHmiResourceHandle::EType (0x10 PNG, 0x11 JPG, 0x12 TGA, 0x13 BMP,
// 0x16 ARGB, 0x17 Bitmap, 0x20 String, 0x22 StringBundle, 0x24 Ascii, 0x28 SpeechBundle, 0x30 ASSET,
// 0x50 WidgetAsset, 0x60 Widget). Dynamic images: libhmidb CHmiResourceManager::
// getDynamicFileResourceProvider() / CHmiDynamicFileResourceProvider::registerDynamicResource(
// EType, const THmiString<char>& path, bool). CHmiResourceManager is registered as plugin
// "IHmiResourceManager" (registerStaticPlugin<CHmiResourceManager, IHmiResourceManager>).
// ---------------------------------------------------------------------------------------------

} // namespace nbt

#endif // NBT_AAD_PLUGIN_ABI_HPP
