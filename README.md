**English** | [中文](./README_zh.md) 



# nes simulator 

[![Pull Requests Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](http://makeapullrequest.com)[![first-timers-only Friendly](https://img.shields.io/badge/first--timers--only-friendly-blue.svg)](http://www.firsttimersonly.com/)

![github license](https://img.shields.io/github/license/PeakRacing/nes)[![Latest Release](https://img.shields.io/github/v/release/PeakRacing/nes?label=Release&logo=github)](https://github.com/PeakRacing/nes/releases/latest)![Windows](https://github.com/PeakRacing/nes/actions/workflows/windows.yml/badge.svg?branch=master)![Linux](https://github.com/PeakRacing/nes/actions/workflows/linux.yml/badge.svg?branch=master)![Macos](https://github.com/PeakRacing/nes/actions/workflows/macos.yml/badge.svg?branch=master)



github: [PeakRacing/nes: A NES emulator in C (github.com)](https://github.com/PeakRacing/nes) (recommend)

gitee: [nes: c语言实现的nes模拟器 (gitee.com)](https://gitee.com/PeakRacing/nes) (updates may not be timely due to synchronization issues)

## Introduction
​	A NES emulator written in pure C. It trades accuracy for speed and a smaller memory footprint,
so it can run reliably on resource-constrained hardware such as MCUs.

**Requirements:**

- Language standard: **C11** or above

**Note:**

- **This repository only contains the NES emulator, it does not provide any game !!!**
- Accuracy is traded for speed: rendering is scanline (line-pixel) level instead of pixel level, so a few
  games inevitably show artefacts (individual pixel offsets, or a game misbehaving). Most games run fine.



**Platform support:**

- [x] Windows

- [x] Linux

- [x] MacOS

**Simulator support:**

- [x] CPU (All instructions)

- [x] PPU (scanline-level precision)

- [x] APU (Fixed-point calculation, DMC channel included)

- [x] Expansion audio (VRC6, Sunsoft 5B, Namco 163, MMC5, VRC7) — `NES_ENABLE_EXPANSION_AUDIO`,
  on for the SDL front ends and off (zero cost) for MCU/core builds

**mapper support:**

- [x] iNES 1.0 mapper Plane 0 table 0~255

- [ ] NES 2.0 mappers Plane 1 table 256~511

- [ ] NES 2.0 mappers Plane 2 table 512~767

Verified playable (at least one image in the regression corpus reaches rendering):

	0, 1, 2, 3, 4, 5, 7, 10, 11, 13, 15, 16, 17, 18, 19, 21,
	22, 23, 24, 25, 26, 31, 32, 33, 34, 37, 38, 44, 45, 47, 51, 57,
	58, 62, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 75, 76, 77, 78,
	79, 80, 82, 83, 85, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
	99, 105, 113, 114, 115, 117, 118, 119, 121, 132, 133, 140, 141, 142, 144, 146,
	147, 149, 150, 159, 163, 168, 171, 176, 178, 180, 184, 185, 187, 189, 193, 198,
	199, 206, 208, 210, 225, 226, 227, 228, 229, 230, 231, 232, 234, 235, 240, 241,
	244, 245, 246, 250, 253, 255,

## Software Architecture

​	The example is based on SDL for image and sound output, without special dependencies, and you can port to any hardware by yourself


## Compile Tutorial

### Compile Preparation

#### Windows:	

​	install MSVC([Visual Studio](https://visualstudio.microsoft.com/zh-hans/vs/))

​	install [xmake](https://github.com/xmake-io/xmake)

#### Linux(Ubuntu):

```shell
sudo add-apt-repository ppa:xmake-io/xmake -y
sudo apt-get update -y
sudo apt-get install -y git make gcc p7zip-full libsdl2-dev xmake
```

#### Macox:

```shell
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
brew update
brew install make gcc sdl2 xmake
```

### Compilation Method

​	clone repository，execute `xmake` directly to compile

​	**Note:** This project supports both SDL2 and SDL3, use SDL2 to execute 'xmake' under SDL/SDL2



## Download link

github: https://github.com/PeakRacing/nes/releases

gitee: https://gitee.com/PeakRacing/nes/releases



## Instructions

​	on linux or macos enter  `./nes xxx.nes` load the game to run

​	on windows enter `.\nes.exe xxx.nes` load the game to run

## Key mapping

| joystick |  up  | down | left | right | select | start |  A   |  B   |
| :------: | :--: | :--: | :--: | :---: | :----: | :---: | :--: | :--: |
|    P1    | `W`  | `S`  | `A`  |  `D`  |  `V`   |  `B`  | `J`  | `K`  |
|    P2    | `↑`  | `↓`  | `←`  |  `→`  |  `1`   |  `2`  | `5`  | `6`  |

**Note: P2 uses numberic keypad**

### VS. System arcade games (`vs xxx.nes`)

**Mapper 99 itself is always compiled**: it is an ordinary mapper (the board register is written
through `$4016`, plus CHR/PRG banking, 8KB of work RAM and four-screen VRAM), and all 11 VS games in
`rom/mapper99/` load and run in the default build.

**The arcade-only extras** (the RP2C04 RGB palettes and the coin/service switches) are isolated
behind `NES_VS_SYSTEM`: the **SDL2 / SDL3 desktop builds enable it by default** (`1` in
`sdl/*/port/nes_conf.h`), while the core and embedded defaults stay `0` (saving the ~1.3KB of
palette tables).

Desktop enables it because VS games draw their white graphics with colour index `$0F`, which the
**consumer 2C02 maps to black**: without the arcade palette `vs platoon` loses its soldier
silhouettes and its `CREDIT` counter entirely (which looks like a hung half-drawn logo), and
`vs battle city` is similarly off-colour.

Once enabled: the arcade board has no second gamepad, and the coin / credit / service switches sit in
the upper bits of the controller ports, read outside the 8-bit shift sequence:

|     Action      |        Key         | Notes                                                                       |
| :-------------: | :----------------: | :-------------------------------------------------------------------------- |
|      COIN       |  `2` (P2 start)    | `$4016` bit5/bit2; **tap it** - the game only adds a credit on the falling edge |
|     CREDIT      |  `1` (P2 select)   | `$4016` bit3                                                                |
|     SERVICE     |  `6` (P2 B)        | `$4017` bit2                                                                |

> **Known limitation**: with the macro enabled the video side matches Mesen (arcade colours), but the
> game does **not** start yet - it stays in its attract loop waiting for a start condition that is
> still being tracked down (see AGENTS.md), which is why the macro is off by default.

## Runtime requirements

Three configurations. Memory and code figures are measured (bench front end; code built with `-Os` for Thumb-2 on
`cortex-m3`/`cortex-m4` and for ARM mode on `arm926ej-s`); the clock figures are reference values
derived from the measured frame cost.

| Goal | Macros | Clock | RAM | ROM (program flash) |
| :-- | :-- | :-- | :-- | :-- |
| Minimal (cheapest) | `NES_USE_FS=1`, `NES_ROM_STREAM=1`, `NES_PRG_CACHE_SLOTS=5`, `NES_CHR_CACHE_SLOTS=9`, `NES_ENABLE_SOUND=0`, `NES_RAM_LACK=1`, `NES_USE_SRAM=0`, optionally `NES_FRAME_SKIP=1` | >=100 MHz class 32-bit MCU (higher end of the range for cacheless Cortex-M3/M4; lower for cached ARM9/ARM11; enable frame skip below that) | **>=128 KiB** (130 KB of buffers measured; allow 160 KiB with stack/RTOS/file system) | **>=128 KiB** (113 KiB with Thumb-2, ~168 KiB in ARM mode) |
| Recommended (sound, full speed) | as above with `NES_PRG_CACHE_SLOTS=6`, `NES_CHR_CACHE_SLOTS=12`, `NES_ENABLE_SOUND=1` | cacheless Cortex-M3/M4 need roughly 168-216 MHz; cached ARM9/ARM11 and similar are faster per clock and need less | **>=160 KiB** (128 KiB NROM to 136 KiB MMC3 measured, plus stack/RTOS) | **>=192 KiB** (file system and front end included; ~113 KiB of code with Thumb-2) |
| Full speed (ROM resident, no storage reads) | `NES_USE_FS=1`, `NES_ROM_STREAM=0`, `NES_ENABLE_SOUND=1`, `NES_RAM_LACK=1`, optionally `NES_USE_SRAM=1` | same as recommended (>=100 MHz class with sound off) | **>=96 KiB + the largest cartridge image** (40 KB cart ~= 94.5 KiB; 512 KB cart ~= 1.09 MiB measured) | **>=192 KiB** |

Four notes:

- **Architecture independent**: the core is plain integer C11 - no FPU or DSP instructions and no ARM-specific extensions (verified to compile for `cortex-m3` and `arm926ej-s`). The real constraint is that **one frame must finish within 16.7 ms** (measurable on the target with `bench/`); cached ARM9/ARM11/RISC-V parts are usually faster per clock than cacheless Cortex-M, so the clock figures above are reference values for cacheless Cortex-M only.
- Game images need **external storage** (SD/SPI flash). The streaming tiers read on demand; the resident tier
  stops touching storage after loading, so choose it when storage is slow or absent - while **cartridges of
  256 KB and up require the streaming tier** (resident needs as much RAM as the image itself).
- Streaming caches cost `NES_PRG_CACHE_SLOTS x 8 KB + NES_CHR_CACHE_SLOTS x 1 KB` (60 KB at the default 6/12,
  49 KB at the 5/9 minimum), so **below roughly 60 KB of image the resident tier uses less RAM** (a 40 KB NROM
  needs only 24 KB).
- `NES_COLOR_DEPTH` supports 16 or 32 only: 16-bit with `NES_RAM_LACK=1` (half frame) gives a 61,440 byte draw
  buffer, which is the memory floor; `NES_ENABLE_SOUND=1` adds just 882 bytes of sample buffer and about 0.7% frame time.

## Transplant instructions

​	The source code in the `inc`and `src` directories does not need to be modified, only the three files in the `port` directory `nes_conf.h` `nes_port.c` `nes_port.h`

- `nes_conf.h` is a configuration file, configure according to your needs, such as printing extra definitions, the implementation of `nes_log_printf`
- `nes_port.c` is the main porting file, which needs to be ported according to the needs



​	**Note: If the target platform for migration has weak performance and small space, some macro configurations are specially reserved:**

- `NES_ENABLE_SOUND` can be set to 0 to turn off the APU to increase the running speed
- `NES_RAM_LACK` can be set to 1, using a half-screen refresh to reduce RAM consumption (running at a slower speed)
- You can configure `NES_FRAME_SKIP` to skip frames
- `NES_ROM_STREAM` can be set to 1 to dynamically switch banks from a file. This mode only requires a 40KB active bank buffer (PRG 32KB, CHR 8KB). The file handle remains open, but the bank switching speed will decrease. It is designed for low memory.
- If SPI 8-byte transmission is used for embedded platforms, the color anomaly configuration `NES_COLOR_SWAP` can be used to switch the large and small ends



## Showcase

**mapper 0:**

| ![Super Mario Bros](./docs/SuperMarioBros.png) | ![F1_race](./docs/F1_race.png) | ![Star Luster (J)](./docs/StarLuster(J).png) | ![Ikki (J)](./docs/Ikki(J).png) |
| :--------------------------------------------: | :----------------------------: | :------------------------------------------: | ------------------------------- |
|  ![Circus Charlie](./docs/CircusCharlie.png)   |                                |                                              |                                 |

**mapper 2:**


|  ![Contra1](./docs/Contra1.png)  | ![Castlevania](./docs/Castlevania.png) | ![Journey](./docs/Journey.png) | ![Lifeporce](./docs/Lifeporce.png) |
| :------------------------------: | :------------------------------------: | :----------------------------: | ---------------------------------- |
| ![mega_man](./docs/mega_man.png) |  ![Athena (J)](./docs/Athena(J).png)   |                                |                                    |

**mapper 3:**

| ![contra](./docs/MapleStory.png) | ![Donkey_kong](./docs/Donkey_kong.png) |
| :------------------------------: | :------------------------------------: |



**mapper 94:**

![Senjou no Ookami](./docs/Senjou_no_Ookami(J).png)

**mapper 180:**

![Crazy Climber](./docs/CrazyClimber(J).png)

## Discussion group

​	**Non-technical support, only for the purpose of interest exchange.**

![Communication](./docs/Communication.png)

## Literature reference

https://www.nesdev.org/



