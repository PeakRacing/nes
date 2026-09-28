[**English**](./README.md)  | **中文**



# nes 模拟器

[![Pull Requests Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](http://makeapullrequest.com)[![first-timers-only Friendly](https://img.shields.io/badge/first--timers--only-friendly-blue.svg)](http://www.firsttimersonly.com/)

![github license](https://img.shields.io/github/license/PeakRacing/nes)[![Latest Release](https://img.shields.io/github/v/release/PeakRacing/nes?label=Release&logo=github)](https://github.com/PeakRacing/nes/releases/latest)![Windows](https://github.com/PeakRacing/nes/actions/workflows/windows.yml/badge.svg?branch=master)![Linux](https://github.com/PeakRacing/nes/actions/workflows/linux.yml/badge.svg?branch=master)![Macos](https://github.com/PeakRacing/nes/actions/workflows/macos.yml/badge.svg?branch=master)



github: [PeakRacing/nes: A NES emulator in C (github.com)](https://github.com/PeakRacing/nes) (推荐)

gitee: [nes: c语言实现的nes模拟器 (gitee.com)](https://gitee.com/PeakRacing/nes) (由于同步问题可能导致更新不及时)

## 介绍
​	纯C语言实现，意在牺牲精确度换来更快速，占用内存更小，能在单片机这种小资源硬件上稳定运行的模拟器

**要求:** 

- C语言标准: **C11** 及以上

**注意:**

- **本仓库仅为nes模拟器，不提供游戏本体！！！**
- 由于牺牲了精确度，使用行像素级模拟而非像素级别模拟，故注定有少数游戏效果异常(个别像素偏移，个别游戏运行异常)，但大部分游戏是可以运行的



**平台支持:**

- [x] Windows

- [x] Linux

- [x] MacOS

**模拟器支持情况：**

- [x] CPU (所有指令)

- [x] PPU (行像素级精度模拟)

- [x] APU (定点计算)

**mapper 支持：**

- [x] iNES 1.0 mapper Plane 0 table 0~255

- [ ] NES 2.0 mappers Plane 1 table 256~511

- [ ] NES 2.0 mappers Plane 2 table 512~767

已测试:

	0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25,
​    26, 31, 33, 34, 37, 38, 45, 47, 64, 65, 66, 67, 68, 69, 
    70, 71, 72(音频), 74, 75, 76, 77, 78, 79, 80, 83, 85, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
    105, 140, 141, 144, 146, 159, 163, 168, 177, 180, 184, 185, 189, 193, 206, 210, 228, 232, 246, 253



## v0.2.0 更新要点

- **即时存档**：`<游戏名>.nessave`，带魔术头与 CRC 校验；读取分两遍，坏档或别的游戏的档不会把机器改成半成品。
  格式见 [docs/nessave-format.md](docs/nessave-format.md)。SDL 前端 F5 存 / F8 读。
- **电池存档互通**：游戏自身的 `<游戏名>.sav` 自动读写，并与存档文件保持一致；电池就是 CHR-RAM 的板子也已支持。
- **PAL 支持**：视频与音频时序随区域切换（312 行 / 50Hz / 3.2 dots per CPU cycle，PAL 的 APU 时钟），
  NTSC 数值未变。
- **新增 mapper**：82（Taito X1-017）、96（Bandai Oeka Kids）、168（Racermate），并修正 TQROM(119)、
  RAMBO-1(64) 的寄存器布局。255 个 mapper 已全部接入分发表。
- **流式 ROM 构建**：PRG/CHR LRU 缓存现在会上报 I/O 失败，而不是静默沿用旧 bank；MMC5 扩展背景取数不再越界。
- **测试与 CI**：内置 37 个单测/压力用例 + 195 张 ROM 回归基线；Linux/Windows/macOS 流水线既构建前端也跑测试。
- **性能基准**：`bench/` 提供"桌面"与"类 MCU（RGB565、半帧缓冲、流式 ROM）"两种配置的帧耗时与模块占比，
  数据见 [docs/performance-2026-09-27.md](docs/performance-2026-09-27.md)。

## 软件架构

​	示例基于SDL进行图像声音输出，没有特殊依赖，您可自行移植至任意硬件


## 编译教程

### 编译准备

#### Windows:	

​	安装MSVC([Visual Studio](https://visualstudio.microsoft.com/zh-hans/vs/))

​	安装 [xmake](https://github.com/xmake-io/xmake)

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

### 编译方法

​	克隆本仓库，直接执行 `xmake` 编译即可 

​	**注意：**本项目同时支持sdl2和sdl3, 使用sdl2就在sdl/sdl2下执行`xmake`

​	

## 下载地址

github: https://github.com/PeakRacing/nes/releases

gitee: https://gitee.com/PeakRacing/nes/releases



## 使用说明

​	Linux或Macos下输入 `./nes xxx.nes` 加载要运行的游戏

​	Windows下输入 `.\nes.exe xxx.nes` 加载要运行的游戏

## 按键映射

| 手柄 |  上  |  下  |  左  |  左  | 选择 | 开始 |  A   |  B   |
| :--: | :--: | :--: | :--: | :--: | :--: | :--: | :--: | :--: |
|  P1  | `W`  | `S`  | `A`  | `D`  | `V`  | `B`  | `J`  | `K`  |
|  P2  | `↑`  | `↓`  | `←`  | `→`  | `1`  | `2`  | `5`  | `6`  |

**注意：P2使用数字键盘(小键盘)**

## 移植说明

​	`inc` 和 `src` 目录下的源码无需修改，只需要修改`port`目录下的三个文件 `nes_conf.h` `nes_port.c` `nes_port.h`

- `nes_conf.h`为配置文件，根据自己需求配置即可,如需打印额外定义 nes_log_printf 的实现
- `nes_port.c`为主要移植文件，需要根据需求进行移植



​	**注意:如果移植的目标平台性能羸弱、空间较小等特别预留了一些宏配置：**

- 可以将`NES_ENABLE_SOUND`设置为0关闭apu以增加运行速度
- 可以将`NES_RAM_LACK`设置为1使用半屏刷新以减少ram消耗(运行速度会降低)
- 可以自行配置`NES_FRAME_SKIP`进行跳帧
- 可以将`NES_ROM_STREAM`设置为1动态从文件切换 bank，此模式只需要40KB 活跃 bank 缓冲区（PRG 32KB + CHR 8KB），文件句柄保持打开，但切换bank速度会下降，为低内存设计
- 如果为嵌入式平台使用spi 8字节传输时颜色异常配置`NES_COLOR_SWAP`可进行大小端切换

## 运行展示

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

## 交流群

​	**非技术支持，仅作为兴趣交流**

![Communication](./docs/Communication.png)

## 文献参考

https://www.nesdev.org/



