![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP.png)

---

This software is free to use for personal, educational, and non-profit purposes.

- Certain portions of the software are subject to third-party copyright, prohibiting the commercial use of this compilation.
- All C++ code is 100% authored by me and is permitted for use for all purposes.

A ColecoVision and ADAM Emulator & Debugging Suite for Windows & Linux.

ADAM+ is a modern emulator and development toolkit for the ColecoVision and Coleco ADAM systems, built with Qt6 and inspired by the original EmulTwo project.
The software has been completely redesigned from the ground up, focusing on stability, performance, and expandability.

The ADAM+ emulator is built using the latest available techniques and technologies obtainable in 2025. Leveraging deep expertise and the assistance of advanced
Language Models (LLMs), we can achieve the full potential of our programming skills with exceptional speed and accuracy.

It serves as a central platform for integrating a wide range of hardware-related devices, all developed as part of the broader ADAM+ hardware project.

---

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_HARDWARE.png)

Based on our proprietary Adam+ emulator, we are reviving the project we started two years ago—which was put on hold 
due to time constraints—to fully integrate it. Our goal is to make the emulator a 100% integral component of our 
dedicated hardware. 
Four microcontrollers are utilized to bridge the existing ADAM hardware with the new custom hardware, 
with our ADAM+ emulator serving as the core processing unit."

"Our ambition remains to deliver the authentic ADAM 1983 experience."

---

## 💾 Downloads

[![release](https://img.shields.io/badge/Latest%20release-windows64-green.svg)](https://github.com/dvdh1961/ADAMP/releases/download/0.5.12.25/WINDOWS_ADAMP_0.5.12.25.exe)
[![release](https://img.shields.io/badge/Latest%20release-linux64-blue.svg)](https://github.com/dvdh1961/ADAMP/releases/download/0.5.12.25/LINUX_ADAMP_0.5.12.25.zip)
![Current Release](https://img.shields.io/badge/Version-V0.5.12.25-yellow)
![Downloads](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/dvdh1961/ADAMP/main/stats/downloads.json&label=Downloads&query=display_total&color=yellow)
[![YouTube](https://img.shields.io/badge/YouTube-FF0000?logo=youtube&logoColor=white)](https://youtu.be/vobLE2F9Cc0)

![Commits](https://img.shields.io/github/commits-since/dvdh1961/ADAMP/latest)
[![Contributors](https://img.shields.io/github/contributors/dvdh1961/ADAMP)](https://github.com/dvdh1961/ADAMP/graphs/contributors)
![Stars](https://img.shields.io/github/stars/dvdh1961/ADAMP)
![Issues](https://img.shields.io/github/issues/dvdh1961/ADAMP)
[![GitHub Sponsors](https://img.shields.io/github/sponsors/dvdh1961)](https://github.com/sponsors/dvdh1961)
[![Donate](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://www.paypal.com/donate?business=dannyvdh@pandora.be)

## 🚀 Versions

Version 0.5.12.25 x86_64 Windows & Linux

- To approximate the behavior of the original Coleco Adam as closely as possible, 
  we have modified the method for handling ROMs, Tapes, and Disks using the buttons that
  are also present on the original hardware.

  The revised workflow is as follows:
    Media Load & Run: Load the desired media (ROM, Tape, or Disk) and subsequently 
    press the corresponding RESET button to initiate the media:

    - Use the 'CARTRIDGE RESET' button to start ColecoVision ROMs.
    - Use the 'COMPUTER RESET' button to start Adam-specific media (Tapes and Disks).

    Full Media Unload: 
    - To fully release or unload all currently mounted media (ROMs/Tapes/Disks), 
      press the 'POWER' button.

- We have implemented a paddle function. If a joystick has been selected, the user can verify the functionality 
  of their analog stick using the small keypad widget. 
  To play a game requiring a paddle, the user must select the paddle mode in the menubar. 
  When activated, the left and right cursor keys are replaced by the left/right axis input from the analog stick.      

---

Version 0.4.11.25 x86_64 Windows (Linux will be in a couple of days)
- Some software components are subject to licensing agreements held by the rightful owners
  All C++ code is 100% authored by me and is permitted for use for all purposes.
- Added more menues to the emulator with extra options
- Added Adam rom card loading (still hard in development)
- Joystick options
- Folder options
- Removed some bugs

---

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_C1.gif)

Version 0.3.11.25 x86_64 Windows & Linux
- Open source LICENSE removed (because some software issues with licensing rules) software is free to use for personal, educational, and non-profit purposes.
  All C++ code is 100% authored by me and is permitted for use for all purposes. 
- Added USB joystick support (Connect usb joystick to computer before opening emulator)
- Added resizing application to any format
- Added switch bezels on/off
- Added snap windows on/off
- Added saving geometry
- Changed installation path (windows --> no program files anymore)
- Media map now standard configured into working directory (No save issues anymore)

---

Version 0.2.11.25
- LINUX VERSION x86-64 added
- Added 4 tapes / 4 disks support
- Added printer clipboard and removed some bugs with PR#1
- Updated logging

---

Version 0.1.10.25
- 🎮 **ColecoVision** game support (`.rom`, `.bin`, `.col`)
- 🪛 **Built-in Debugging Tools**
  - Tile, Sprite, VRAM, RAM, and Disassembly viewers  
  - Perfect for tracking bugs in homebrew or development builds
- 🔊 **Super Game Module** support (inclusief AY-soundchip)
- 🧠 **Megacart Bankswitching** up to 512 KB
- 🎯 **Full Controller Button Mapping**
- 💾 **Coleco ADAM** game support (`.ddp`, `.dsk`)
- 🖨️ **Print to txt and pdf support (with smartwritter some issues)
- 🎨 **Pixel sharp,smooth and EPX interpolation
- 🖥️ **Full screen option with buildin bezels 
- ✒️ **LOAD & SAVE on your media!
- 💾 **Save / Load Game State**

---

## 💡Info
- You need the free community QT6 Creator to build the project.

## 🚧 ToDo

- 🕹️ Hardware integration with ADAM+ console (connecting Hardware, Cartridges,Keyboard,Joypads,...)
- 🕹️ Drag and Drop media
- 🪛 Debugger patches, cheats add-ons, more breakpoint options
- 🪛 Import custom bios files
- 🖥️ Command line functionality
- 🎨 Custom Palletes
- 🖥️ Adam CPM
- 🖥️ Adam cartridge select

## ADAM+ The Emulated Computer Entertainment System

## Lots of tools to examine and learn
![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/AdamPpic1.png)
![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/AdamPpic2.png)
## Smartbasic list to Clipboard
![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_PRINTER.gif)
## Debugger breakpoint
![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_DEBUG.gif)

## Credits

Thanks to everyone who shared their knowledge and inspiration — without them, this project would never have come to life.
- E.mul T.wo           (https://github.com/alekmaul/emultwo)
- Marat Fayzullin      (https://fms.komkon.org/ColEm/)
- wavemotion-dave      (https://github.com/wavemotion-dave/ColecoDS)
- Fuse                 (https://fuse-emulator.sourceforge.net/)
- EightyOne            (https://sourceforge.net/projects/eightyone-sinclair-emulator/)
- Russell Marks        (https://sz81.sourceforge.net/)
- Juergen Buchmueller  (R.I.P.) (z80 code)

## Support ADAM+

ADAM+ is free but you can donate to support its development

[![PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://www.paypal.com/donate?business=dannyvdh@pandora.be)

## License

ADAM+ software is free to use for personal, educational, and non-profit purposes.
