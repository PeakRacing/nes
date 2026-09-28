# ([中文](# 更新日志))

# Changelog 

## v0.2.0

Release highlights:

- **Save states** — `<game>.nessave`: complete machine snapshots (32-byte magic header, per-section
  versioning, payload CRC). Loading is two-pass: a corrupt file or a state from another ROM is rejected
  before the machine is touched. Spec: `docs/nessave-format.md`. F5 saves / F8 loads in the SDL front ends.
- **Battery save interop** — the game's own `<game>.sav` is loaded after `mapper_init` and flushed on
  unload / state save, so emulator snapshots and in-game saves never disagree. Boards whose battery is
  the CHR-RAM (e.g. Racermate, 64 KB) are carried through a mapper-provided battery buffer.
- **PAL support** — region driven timing for video *and* audio: 312 lines/frame at 50 Hz,
  106 + 9/16 CPU cycles per line (3.2 dots per CPU cycle), PAL APU clock and sample count. Implemented as
  data in `nes_timing_t` so the NTSC path is numerically unchanged and no branch was added to the
  scanline loop.
- **Mappers** — added 82 (Taito X1-017), 96 (Bandai Oeka Kids), 168 (Racermate); corrected TQROM (119)
  CHR-RAM selection and RAMBO-1 (64) PRG/CHR layout and IRQ semantics against the Mesen reference.
  `mapper_state_reapply` now covers 20 of the mapper-specific boards so save states restore their mapping.
- **Streaming ROM builds** — the PRG/CHR LRU cache detects seek/read failures, invalidates the affected
  slot and reports the first error through `nes_rom_stream_error()` instead of silently mapping a stale
  bank; `nes_chrrom_tile()` keeps MMC5 extended background reads inside the cache (fixes both
  `L'Empereur` dumps).
- **PPU/CPU fixes** — OAM DMA goes through the CPU bus when a mapper overrides PRG reads; skipped frames
  refresh the background opacity map so sprite-0 hit still sees the current frame; `$3F10-$3F1F` palette
  mirroring and the stream-mode CHR-ROM bound are handled explicitly.
- **Tests** — 37 unit/stress cases (CPU, PPU, APU, ROM, mapper, save state, stress), a 195-image corpus
  baseline and a `-Strict` mode; the suite also runs in the Linux, Windows and macOS CI pipelines.
- **Benchmark front end** — `bench/` reports frame time, per-module cost and streaming cache hit/miss
  for a desktop and an MCU-like (RGB565, half-frame buffer, streaming ROM, frame skip) configuration;
  numbers in `docs/performance-2026-09-27.md`.

### FIX: MMC1 (Mapper 1) — Castlevania II and general correctness

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

## v0.2.0

本次发布要点：

- **即时存档**：`<游戏名>.nessave`（32 字节魔术头、分段版本号、payload CRC）；两遍读取，坏档或异 ROM 的档
  在改动机器之前就被拒绝。规范见 `docs/nessave-format.md`；SDL 前端 F5 存 / F8 读。
- **电池存档互通**：`<游戏名>.sav` 在 `mapper_init` 之后自动载入，卸载/存档时回写，两边永远一致；
  电池就是 CHR-RAM 的板子（如 Racermate 的 64KB）通过 mapper 提供的电池缓冲带走。
- **PAL 支持**：视频与音频时序都由区域驱动（312 行/帧、50Hz、每行 106+9/16 CPU 周期、PAL 的 APU 时钟与采样数）；
  实现为 `nes_timing_t` 里的数据，NTSC 数值不变、扫描线循环里没有新增分支。
- **mapper**：新增 82（Taito X1-017）、96（Bandai Oeka Kids）、168（Racermate）；按 Mesen 参考修正 TQROM(119)
  的 CHR-RAM 选择、RAMBO-1(64) 的 PRG/CHR 布局与 IRQ 语义；`mapper_state_reapply` 已覆盖 20 个特殊板子。
- **流式 ROM 构建**：PRG/CHR LRU 缓存会检测 seek/read 失败、作废该槽并通过 `nes_rom_stream_error()` 上报首个错误，
  不再静默映射旧 bank；`nes_chrrom_tile()` 让 MMC5 扩展背景取数不越界（修复两份《L'Empereur》）。
- **PPU/CPU 修正**：mapper 覆盖 PRG 读取时 OAM DMA 走 CPU 总线；跳帧仍刷新背景不透明度图以保证 sprite-0 hit；
  `$3F10-$3F1F` 调色板镜像与流式 CHR-ROM 上界显式处理。
- **测试**：37 个单测/压力用例 + 195 张 ROM 语料基线与 `-Strict` 模式，并在 Linux/Windows/macOS 流水线中运行。
- **性能基准**：`bench/` 报告桌面与"类 MCU（RGB565、半帧、流式、跳帧）"两种配置的帧耗时、模块占比与缓存命中缺失。

### 修复：MMC1（Mapper 1）——《恶魔城 II》与通用正确性

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
