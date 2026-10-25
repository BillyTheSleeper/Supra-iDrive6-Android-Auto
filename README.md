# Overview
I'm a believer of humans writing things, but this is well beyond my skillset, so I (me, Ronnie) am writing this (the words on the page you're reading). I'm not a C-dev, but I do know these things can be done... So... This repository contains code for the J29 Toyota Supra to run Android Auto natively on the NBT Evo head unit. Yeah, talk about a big one. I'll preface it with this: Big screw you to all the companies who have profited for **years** on MMI units. You could have drove innovation, yet you chose to implement a "just good enough" solution to get people to bite. Okay, now that that's out of the way...

## License
NBT-AAD is licensed under the **PolyForm Noncommercial License 1.0.0** (`SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`).
The full text is embedded in `src/main.cpp` / `src/license_text.hpp` and printed by `./nbt-aad --license`.

Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

NBT-AAD is free of charge. Selling it, or bundling it with anything sold, is not permitted.
If you paid for it, you were scammed: official releases are only published at https://github.com/Sq00ky/Supra-iDrive6-Android-Auto
Please report any sellers in here: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues

---

The only authorized bundlers is TurboCode for directly supporting the development of this project by offering free SSH unlocks, and providing guidance for native integration.

## Disclaimer
**I am not responsible for anything you do on your device, if you execute a command, or change files, do so with care - if you break something, only Toyota/BMW can reflash and virginize your head unit. The code is shipped as-is. You may fork the repo, clone it, change whatever you like. I cannot ensure compatibility.**

## Requirements
- SSH Access to your NBT Evo head unit.
- An Ethernet -> ODB2 cable
- A computer w/ an SSH Client
- A flash drive formatted in FAT32 or exFAT

### Building Yourself
The code needs to be compatible with Armv7LE. I built it with [QNX Momentics](https://archive.org/details/qnxsdp-65.7z) because that's probably what the developers iDrive6 did. Be warned, this is from archive.org. Run it in a VM. I cannot guarntee the safety/security of the software. It is as simple as creating a new project in Momentics, adding all the files to the project folder, right clicking on your project folder, selecting "clean", and then "build". 

### Instructions
Installation instructions can be found in the [Installation.md file](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/blob/main/install.md)
