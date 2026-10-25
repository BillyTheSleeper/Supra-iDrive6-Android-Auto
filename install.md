# Installation Guide

## Overview
Computers are hard. I'm going to try to make this as easy as possible for folks to understand, hopefully this will help you understand what's happening behind the scenes a little bit more. The whole process takes roughly 30-45 minutes. The more experienced you are, the faster this will probably go.

### Known Limitations
- AAIDrive cannot be running/installed
- 3d Buildings in Google Maps may cause lag
- TBD

### Hardware Requirements
1. [ODB to ENET Cable](https://www.amazon.com/dp/B0CSX3F5GR)
2. OPTIONAL - [USB A/C to Ethernet Cable](https://www.amazon.com/TP-Link-Ethernet-Thunderbolt-Compatible-UE310C/dp/B0GF9SZBSZ) **ONLY ORDER THIS IF YOUR LAPTOP DOESN'T HAVE AN ETHERNET PORT**
3. Flash Drive formatted in FAT32/exFAT
4. Laptop or Desktop
5. High speed internet

### What are we doing?
Opening up a temporary path to execute commands on the cars head unit. This is being done with software by [TurboCode](https://turbocode.shop/bmw-nbt-evo-ssh-unlocker/). It enables an IT protocol called SSH (Secure Shell) that will let us execute those commands. This protocol is encrypted and secure.
During this process, we are going to install two applications.
1. The Android Auto Daemon
2. An iDrive6 plugin that lets you start Android Auto.

The Daemon sits on your headunit and waits for you to click "Android Auto" and start it. It does not run on start, this is by design.

### What I need From You
Two pieces of software need to be downloaded and installed on your computer
1. [ZeroTier](https://www.zerotier.com/) - This lets me remotely access a private network with just me and your computer.
2. [BMW TurboCode Proxy](https://turbocode.shop/bmw-remote-enet-tool/) - This lets me specifically access your head unit, via an enet cable

I will give you a private ZeroTier network to connect to, this will be sent via Direct Message on whatever platform we're talking on! I'll have to approve your access afterwards.
During this time, you should get Turbocode Proxy running on your device. Click "Discover ZGW" and you should find your cars VIN number pop up, and say something along the lines of "port forward to 0.0.0.0:6969 running". 
### What I Need To Do For You
This is how I will connect to your car and enable SSH.

From there, we will change the head unit from Read Only to Read Write temporarily to install the Android Auto Daemon and the Plugin to enable access in iDrive. This will be done by executing a series of commands over SSH. 
Either I can do this, or you can do this. No pressure. I don't mind doing this for you. I've done both ways. If you're technically inclined and want to poke around, feel free. If not, no worries. I got you!

### What are those commands?
I'm glad you asked!
```bash
ssh root@YOURIP -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1 -p 6969
# or
ssh root@169.254.199.99 -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1
# The password is ts&SK412
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
cd /fs/sda0/opt/
mkdir aa
cd /opt/aa/
cp /mnt/umassXXXXX/nbt-aad ./
chmod +x /opt/aa/nbt-aad
# Before launching the Android Auto Daemon, please note your screen may flash for up to 2 seconds.
/opt/aa/nbt-aad >> /mnt/umassXXXXX/nbt-aad.log
```

This will start dumping logs onto your flash drive in case it crashes. If so, we'll have a copy! If we need to stop the Android Auto Daemon during the process, we'll do so with these commands:

```bash
pidin | grep nbt-aad
nbt-aad 19320
# NBT-AAD will be some random number, in this case 19320.
kill 19320
```

From there, we're going to test and verify that the Android Auto Daemon does **not** crash, and functions as expected. We'll make sure your phone connects to it okay, and nothing breaks. If something does break or doesn't work out of the gate, we may spend a few minutes troubleshooting it.

After we verify it's working okay, we then install the plugins, which is done with these commands:
```bash
mkdir -p /opt/hmi/ID5/data/ro/Plugins/NbtAad
cd /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umassXXXXX/release/* ./
ls -l /opt/hmi/ID5/data/ro/Plugins/NbtAad/
```

We're going to verify all the files successfully copied over okay, once that's done, you'll need to restart the headunit. Press the power button for 30 seconds, and it will reboot. 
Before starting it, make sure your phone is connected via Bluetooth and you can hear music on the speakers. Head over to Media/Radio, and select "Android Auto". 
Your screen may flash briefly. This is 100% normal. On your phone, you should see "Connecting to Android Auto", you should see your WiFi network switch over to your cars... 
Once it's fully connected Android Auto should display on your screen!
