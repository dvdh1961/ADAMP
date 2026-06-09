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

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_HARDWARE5.png)

“Our brand-new hardware console — all parts and step-by-step documentation will be available on GitHub soon, so you can build it yourself!”
“Built around the Raspberry Pi 5 running with our custom ADAM+ emulator on ARM.”

Based on our proprietary Adam+ emulator, we are reviving the project we started two years ago—which was put on hold <br>
due to time constraints—to fully integrate it. Our goal is to make the emulator a 100% integral component of our <br>
dedicated hardware. <br>
Four microcontrollers are utilized to bridge the existing ADAM hardware with the new custom hardware, <br>
with our ADAM+ emulator serving as the core processing unit." <br>

"Our ambition remains to deliver the authentic ADAM 1983 experience." <br>

---

## 🎥 ADAM+ WHAT'S THIS ?!?

[![release](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_INTRO.png)](https://youtu.be/JlMo5caZGwo)

---

## 💾 Downloads

[![release](https://img.shields.io/badge/Latest%20release-windows8+_64_EXE-green.svg)](https://github.com/dvdh1961/ADAMP/releases/download/1.1.06.26/WINDOWS_ADAMP_1.1.06.26.exe)
[![release](https://img.shields.io/badge/Latest%20release-windows8+_64_ZIP-green.svg)](https://github.com/dvdh1961/ADAMP/releases/download/1.1.06.26/WINDOWS_ADAMP_1.1.06.26.zip)
[![release](https://img.shields.io/badge/Latest%20release-linux64_ZIP-blue.svg)](https://github.com/dvdh1961/ADAMP/releases/download/1.1.06.26/LINUX_ADAMP_1.1.06.26.zip)
![Current Release](https://img.shields.io/badge/Version-V1.1.06.26-yellow)
![Downloads](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/dvdh1961/ADAMP/main/stats/downloads.json&label=Downloads&query=display_total&color=orange)

[![release](https://img.shields.io/badge/Quick%20review-ADAM+%20Emulator-magenta.svg)](https://youtu.be/vobLE2F9Cc0)
[![release](https://img.shields.io/badge/Quick%20review-ADAM+%20Hardware-magenta.svg)](https://youtu.be/u7YjjqL3C9E)
![Stars](https://img.shields.io/github/stars/dvdh1961/ADAMP)
![Issues](https://img.shields.io/github/issues/dvdh1961/ADAMP)

## 💔 From the heart,

Sometimes I find myself wondering whether it still makes sense to continue working on this project. 
I put a lot of passion into it, and I genuinely believed it would bring value to the community.
That said, I can’t ignore the concern I feel about the limited positive feedback I’ve been receiving. 
It seems that many people struggle with the fact that I use LLMs. For me, they are simply a tool—to help 
express myself more clearly in English and to move forward more efficiently.
What’s often overlooked is that building an emulator requires far more than just relying on an LLM. 
That alone won’t get you anywhere. 
I’ve invested months of work into this, and in reality, the project itself has been years in the making.

Apologies for the more personal and less upbeat message, but I felt it was something I needed to share.

— Danny


## 🎮 HARDWARE

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_HARDWARE.gif)

The hardware PCBs are finished, and hardware testing can begin.
Programming of our MCUs and integration of our emulator are now underway.
From the moment we have a fully working hardware system, everything will be published on our GitHub.


## 🚀 Versions

Version 1.1.06.26 x86_64 Windows & Linux

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/CVBASIC_PLUGINS1.png)

This release has been expanded with a complete plug-in for creating games, applications, and music ROMs in CVBasic. 

The plug-in includes a CVBasic IDE, Sprite Editor, and Sound Editor all build by me. 

--> CVBASIC core files build by Óscar Toledo (https://github.com/nanochess/CVBasic) <--

All projects can be baked into a ROM with just one click!

For Linux users: do not forget to give the two files cvbasic_linux and gasm80_linux the required permissions using chmod +x, 

otherwise the compiler will not be able to run.

I have also added automatic default paths for new installations when no settings.ini file exists. This makes it much easier to get started.

Please note: if you overwrite an older installation and the settings.ini file already exists, these automatic default paths will not be applied.

Have fun with it!

If you encounter anything that causes problems, or if you think something could be improved, please let me know.



---

Version 1.0.05.26 x86_64 Windows & Linux


This release adds some long-requested functionality, improves usability, and lays important groundwork for future development.

Highlights of version 1.0.05.26:

-CP/M tape and disk support

-Choice between TMS and F18A video hardware simulation

-80-column CP/M and T-DOS support when using F18A

-80-column copy & paste support

-Various GUI improvements

-Z80 CPU Space is now writable in the debugger

-Completely redesigned Settings window with tabbed navigation

-Option to start directly into your personal favorite application instead of Writer

-Fixes for Coleco ROM playback issues

-Major internal source code reorganization

-Initial groundwork for future plug-in support

This release is a mix of visible improvements and under-the-hood work that will help us move faster in future versions. 
The addition of CP/M media support and enhanced F18A functionality are particularly exciting milestones, 
while the source reorganization and plug-in preparation open the door for future expansion.

As always, feedback, bug reports, and feature suggestions are welcome. 
If you give the new version a try, let us know what works, what doesn't, and what you'd like to see next.

Happy retro computing!

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/HW_adamp.png)

---

Version 0.9.04.26 x86_64 Windows & Linux

**Dear Retro Fans,**

A special shout-out to the coders among you…

We are proud to unveil a powerful new addition to our emulator:

**The EOS Media Manager**

Dive deep into your media like never before:
• Create, modify, copy, and edit your files
• Build brand-new media from scratch
• Open your files in a Hex/ASCII editor and tweak every byte to perfection

All these tools are now at your fingertips — just like in the golden days of computing, when every bit mattered.

But we’re not stopping here…

In the next release, we will bring even more power to your setup with a fully integrated **CP/M and T-DOS Media Manager**.

Stay tuned, keep coding… and keep the retro spirit alive.

**— The ADAM+ Team**

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/MEDIAMANAGER.png)

---

Version 0.8.02.26 x86_64 Windows & Linux

Dear Retro Fans,

A brand-new update has arrived — packed with fixes, refinements & shiny new add-ons!

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/BG_ADAMP_0.8.02.png)
  
  - Even greater compatibility with a wide range of ROMs

  - More advanced reset controls

  - Debugger memory dump add-on

  - Preparations for our upcoming Adam+ hardware expansion

  - SmartWriter improvements

  - Game screen optimizations

  - Drag and Drop into roms,disk en tape maps

Fire it up, load your favorite cartridge, and enjoy the ride back to the golden age of computing.

Have fun!

---

Version 0.7.01.26 x86_64 Windows & Linux

We’re excited once again to announce a fresh update of AdamPlus Emulator.
This release focuses on more control, deeper debugging, and—yes—more columns than ever. Brace yourself.

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/T-DOS80.png)

✨ What’s new?

Choose your BIOS
- You decide. No more BIOS lock down. Freedom at last.

Extended logging
- More insight, less guesswork. Logs that actually tell you something (and sometimes even tell the truth).

Expanded debugger
- Extra tools for those who like to dissect bits with their morning coffee.

Tape & Disk games now playable
- Thanks to the new Adam Game Mode in the menubar. Click → play → instant nostalgia.

Disk & Tape sounds in CP/M and T-DOS
- Authentic drive noise included. Neighbours not included.

🔥 T-DOS 80 Columns!!!!! Yes. Eighty. Columns.

---

Version 0.6.12.25 x86_64 Windows & Linux

We’re excited to announce a new update of the AdamPlus Emulator, bringing several new features, 
improvements, and upgrades to both usability and development tools. Here’s what’s new:

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP_BEZEL1.png)

- New Software Features:
    - Video filters added => enhance visuals with optional filtering
    - Settings paths made absolute => more reliable config behavior across systems

    - Debugger Improvements
        - The debugger received a major upgrade:
        - Breakpoints
        - Symbol support
        - Step-over function
        - Copy/Paste in Debugger, Registers, and Memory Dump
        - Memory editor: double-click any byte in the memory dump to modify it directly

    - Look & Feel Enhancements:
        - You can now add your own bezel for both ADAM and ColecoVision modes
        - Perfect for creating your own themed layout or recreating the look of classic hardware.

    - Other Updates:
        - Various bug fixes and general improvements

As always, feel free to share feedback, report issues, and follow development.
Thanks for supporting AdamPlus -- enjoy!

---

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
- 🪛 Debugger patches, cheats add-ons, more breakpoint options
- 🖥️ Command line functionality
- 🎨 Custom Palletes
- 🖥️ Adam CPM

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
- Óscar Toledo         (https://github.com/nanochess/CVBasic) 

## Support ADAM+

ADAM+ is free but you can donate to support its development

[![PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://www.paypal.com/donate?business=dannyvdh@pandora.be)

## License

ADAM+ software is free to use for personal, educational, and non-profit purposes.
