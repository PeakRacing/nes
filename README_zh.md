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

- [x] APU (定点计算，含 DMC 通道)

- [x] 扩展音频 (VRC6 / Sunsoft 5B / Namco 163 / MMC5 / VRC7) —— `NES_ENABLE_EXPANSION_AUDIO`，
  SDL 前端默认开启，核心/MCU 构建默认关闭（零开销）

**mapper 支持：**

- [x] iNES 1.0 mapper Plane 0 table 0~255

- [ ] NES 2.0 mappers Plane 1 table 256~511

- [ ] NES 2.0 mappers Plane 2 table 512~767

已验证可玩（回归语料中至少有一张镜像能出画面）：

	0, 1, 2, 3, 4, 5, 7, 10, 11, 13, 15, 16, 17, 18, 19, 21,
	22, 23, 24, 25, 26, 31, 32, 33, 34, 37, 38, 44, 45, 47, 51, 57,
	58, 62, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 75, 76, 77, 78,
	79, 80, 82, 83, 85, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
	99, 105, 113, 114, 115, 117, 118, 119, 121, 132, 133, 140, 141, 142, 144, 146,
	147, 149, 150, 159, 163, 168, 171, 176, 178, 180, 184, 185, 187, 189, 193, 198,
	199, 206, 208, 210, 225, 226, 227, 228, 229, 230, 231, 232, 234, 235, 240, 241,
	244, 245, 246, 250, 253, 255,


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

| 手柄 |  上  |  下  |  左  |  右  | 选择 | 开始 |  A   |  B   |
| :--: | :--: | :--: | :--: | :--: | :--: | :--: | :--: | :--: |
|  P1  | `W`  | `S`  | `A`  | `D`  | `V`  | `B`  | `J`  | `K`  |
|  P2  | `↑`  | `↓`  | `←`  | `→`  | `1`  | `2`  | `5`  | `6`  |

**注意：P2使用数字键盘(小键盘)**

### VS. System 街机游戏（`vs xxx.nes`）

**mapper 99 本体始终编译**：它就是一个普通 mapper（板级寄存器写在 `$4016`、CHR/PRG 分页、8KB 工作 RAM、四屏 VRAM），
语料里 `rom/mapper99/` 的 11 张 VS 游戏在默认构建下都能正常加载运行。

**街机专属的部分**（RP2C04 系列 RGB 调色板 + 投币/服务开关）由宏 `NES_VS_SYSTEM` 隔离：
**SDL2 / SDL3 桌面构建默认开启**（`sdl/*/port/nes_conf.h` 里为 `1`），核心与嵌入式默认 `0`（省约 1.3KB 色表）。

之所以桌面端默认开：VS 游戏用颜色索引 `$0F` 画白色，而**家用 2C02 把 `$0F` 映射成黑色** ⇒ 不开街机调色板时，
`vs platoon` 的士兵剪影与 `CREDIT` 计数会整片消失（看起来像卡在半个 logo 上），`vs battle city` 同样偏色。

启用后：街机板没有第二个手柄，投币 / 开始信用 / 服务开关接在手柄口的高位上，
而且游戏是在 8 位移位序列**之外**单独读它们：

|    操作      |       按键       | 说明                                                             |
| :----------: | :--------------: | :--------------------------------------------------------------- |
|  投币 COIN   | `2`（P2 开始键） | `$4016` bit5/bit2；**必须点按**——游戏在信号下降沿才 +1，按住无效 |
| 开始 CREDIT  | `1`（P2 选择键） | `$4016` bit3                                                     |
| 服务 SERVICE |   `6`（P2 B 键） | `$4017` bit2                                                     |

> **已知限制**：开启后画面侧与 Mesen 一致（街机配色），但**还不能真正开局** ——
> 游戏仍停在演示循环里等它的开始条件（排查记录见 AGENTS.md），所以这个宏默认不开启。

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



