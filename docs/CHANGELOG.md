# ([中文](# 更新日志))

# Changelog 

## master (unreleased)

### FIX: MMC1 (Mapper 1) — Castlevania II and general correctness

Six bugs fixed in `src/nes_mapper/nes_mapper1.c` and one in `src/nes_mapper.c`:

**nes_mapper1.c**

1. **WRAM never allocated** — MMC1 carts map $6000–$7FFF as work RAM even without battery save. `mapper_init` now allocates 8 KB SRAM/WRAM when not already present, and `mapper_deinit` frees it. Without this, all SRAM reads/writes hit unmapped memory.

2. **Control register wrong power-on value** — Was initialized to `0x00` (one-screen lower, 8K CHR, 32K PRG). Correct power-on value per NESDev is `0x0C` (P=3: fix last PRG bank at $C000; C=0: 8K CHR mode; M=0).

3. **32KB PRG mode wrong block index** — `nes_load_prgrom_32k` was called with the raw 4-bit bank register value instead of a 32KB block index. Fixed to `bankid >> 1` so adjacent 16KB bank pairs are selected together.

4. **PRG banks not re-applied on mode change** — Writing to the control register can change the PRG banking mode (P field). The existing bank register value was never re-applied to the new mode. Added `prg_bank` field to mapper state and a `nes_mapper_apply_prgbank()` helper called whenever P changes.

5. **Mirroring table wrong order** — MMC1 M field: 0=one-screen-lower, 1=one-screen-upper, 2=vertical, 3=horizontal. Was mapped incorrectly; corrected to `{ONE_SCREEN0, ONE_SCREEN1, VERTICAL, HORIZONTAL}`.

**nes_mapper.c**

6. **CHR bank index out-of-bounds** — `nes_load_chrrom_4k/8k/1k` had no bounds checking. When a game writes a CHR bank index larger than the ROM's actual bank count (e.g., Castlevania II writes bank 18 into a 16-bank ROM), the pointer computed was past the end of the CHR-ROM buffer, producing garbage tiles. Fixed by masking `src % total_banks` before computing the pointer — matching real MMC1 hardware wrap behavior.

## v0.1.0

- Improve CPU emulation (including all illegal instructions) 
- Change APU emulation to fixed-point calculations 
- Fix CPU interrupt handling 
- Add dynamic file-based bank switching feature; this mode only requires a 40KB active bank buffer (PRG 32KB + CHR 8KB). The file handle remains open, but the bank switching speed will decrease, designed for low memory usage

## v0.0.4

- Supports  rt-thread

## v0.0.3

- Optimization Rate
- Experimentally added mapper1
- Supports SDL3

## v0.0.2

### ADD:

- APU
- Mapper support 3,7,94,117,180
- Merge threads

### FIX:

- Background drawing mirroring error

### DEL:

-  Delete llvm



## v0.0.1

The first beta version, which already supports CUP, PPU, mapper0 2, is already playable Super Mario, Contra, etc





# ([英文](# Changelog))

# 更新日志 

## master (开发中)

### 修复：MMC1 (Mapper 1) — 恶魔城II 及通用正确性修复

在 `src/nes_mapper/nes_mapper1.c` 中修复 5 个 bug，在 `src/nes_mapper.c` 中修复 1 个 bug：

**nes_mapper1.c**

1. **WRAM 从未分配** — MMC1 卡带即使没有电池存档，$6000–$7FFF 也映射为工作 RAM。`mapper_init` 现在在 sram 指针为 NULL 时自动分配 8 KB，`mapper_deinit` 负责释放。缺少此内存时所有 SRAM 读写都访问未映射区域。

2. **控制寄存器上电初始值错误** — 原来初始化为 `0x00`（单屏幕、8K CHR、32K PRG）。NESDev 规范中正确上电值为 `0x0C`（P=3：最后一个 PRG bank 固定在 $C000；C=0：8K CHR 模式；M=0）。

3. **32KB PRG 模式 block 索引错误** — `nes_load_prgrom_32k` 使用原始 4 位 bank 寄存器值调用，而非 32KB 块索引。修正为 `bankid >> 1`，以便正确选取相邻的两个 16KB bank 组成 32KB。

4. **P 模式切换后 PRG bank 未重新应用** — 写控制寄存器可能改变 PRG bank 模式（P 字段），原来的 bank 寄存器值不会在新模式下重新应用。新增 `prg_bank` 字段保存最近的 bank 寄存器写入值，并提取 `nes_mapper_apply_prgbank()` 辅助函数，在 P 变化时调用。

5. **镜像表顺序错误** — MMC1 M 字段：0=单屏下、1=单屏上、2=垂直、3=水平。原来顺序错误；已修正为 `{ONE_SCREEN0, ONE_SCREEN1, VERTICAL, HORIZONTAL}`。

**nes_mapper.c**

6. **CHR bank 索引越界** — `nes_load_chrrom_4k/8k/1k` 没有边界检查。当游戏写入的 CHR bank 索引超过 ROM 实际 bank 数量时（如恶魔城II 向只有 16 个 bank 的 ROM 写入 bank 18），计算出的指针会越过 CHR-ROM 缓冲区末尾，导致花屏。修复方法：在计算指针前对 bank 索引取模 `src % total_banks`，与真实 MMC1 硬件的折回行为一致。

## v0.1.0

- 完善cpu模拟(包括所有非法指令)
- apu模拟改为定点计算
- 修复cpu中断处理
- 新增动态从文件切换 bank功能，此模式只需要40KB 活跃 bank 缓冲区（PRG 32KB + CHR 8KB），文件句柄保持打开，但切换bank速度会下降，为低内存设计

## v0.0.4

- 添加rt-thread适配

## v0.0.3

- 优化速率
- 实验性添加mapper1
- 支持sdl3

## v0.0.2

### 新增：

- APU
- mapper 支持 3,7,94,117,180
- 合并线程

### 修复：

- 背景绘制镜像错误

### 删除:

- 去掉llvm使用



## v0.0.1

第一个浏览版，已支持CUP,PPU,mapper0 2，已可玩超级玛丽，魂斗罗等
