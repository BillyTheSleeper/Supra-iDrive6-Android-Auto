# Overview
I'm a believer of humans writing things, but this is well beyond my skillset, so I (me, Ronnie) am writing this. This repository contains code for the J29 Toyota Supra to run Android Auto natively on the NBT Evo head unit. Yeah, talk about a big one. I'll preface it with this: Big screw you to all the companies who have profited for **years** on MMI units. You could have drove innovation, yet you chose to implement a "just good enough" solution to get people to bite. Okay, now that that's out of the way...

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
The code comes pre-compiled in the https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases page. I cannot guarantee cross compatibility. I built this entirely using AI, so you may need to have it help troubleshoot for you.

First, you'll need to enable SSH access on your head unit. I'm working on a free way to do that. I have information here if you [want to pay and use FeatureInstaller](https://github.com/Sq00ky/Supra-Headunit-RE/tree/main). I do not profit off of this in any way. Once enabled:

- SSH in as root, the password is ts&SK412
- Drop down into hu-omap
- Look for your flash drive name in /mnt/ ,this should be something like umassXYZ or some variation
- Run the Android Auto Daemon to make sure it works before you make any permanent system changes
  
```
ssh root@169.254.199.99
on -f /net/hu-omap/ /bin/sh
cd /tmp/
ls /mnt/
cp /mnt/yourflashdrivenamehere/nbt-aad ./
./nbt-aad
```

After, the daemon should start. Verify full functionality (audio, video, controls).

I'm going to pause instructions here for now, since I still need to work on this & get code written, but rest assured, I'll finish the rest up soon :) It may be a few weeks because I've burnt about $500 tokens at this point. I'll include a paypal donation link if you got something out of this, but **please only donate if you got something out of this. Don't feel like you have to - it did cost me quite a bit of money to make this work, so I'd appreciate it if you did, but don't feel obligated.**
