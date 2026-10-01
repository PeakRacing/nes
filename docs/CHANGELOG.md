# ([中文](# 更新日志))

# Changelog 

## v1.0.0

Release highlights:

- **APU completed** — the DMC channel is now fully implemented (16 NTSC rates, `$4010`-`$4013`
  semantics, sample fetch through the CPU bus, end-of-sample IRQ), the APU IRQ line is kept apart
  from the CPU's shared flag (reading `$4015` no longer swallows mapper interrupts) and the
  `$4015`/`$4017` semantics were corrected (live interrupt flags, frame-counter reset).
- **Expansion audio 5/5** — VRC6 (24/26), Sunsoft 5B (69), Namco 163 (19/210/163), MMC5 (5) and
  now **VRC7** (85) are emulated behind `NES_ENABLE_EXPANSION_AUDIO` (on for desktop builds, off
  for core/MCU builds where the whole module compiles away).  VRC7 is the project's own compact
  fixed-point FM model — no third-party emulator code — with the chip's built-in instrument ROM,
  six two-operator channels, no heap use, and a single branch per sample for boards that are
  silent.
- **Mapper audit finished** — 28 boards were rewritten or corrected against the Mesen2 reference
  and the local corpus (12, 51, 62, 79, 85, 86, 114, 117, 132, 133, 142, 147, 149, 150, 171, 176,
  178, 187, 198, 199, 207, 227, 235, 240, 242, 244, 245, 250) plus 14 romdb entries for images
  whose headers name the wrong board.  Mappers with an untrustworthy verdict dropped from 36 to
  10, and more than 40 games went from blank or load failure to playable.
- **State and API cleanup** — save states keep the whole APU (expansion audio included) by value,
  the expansion-audio module was folded into `nes_apu`, `nes_state_io.h` was merged into
  `nes_state.h`, every header now uses `#pragma once`, and the APU state version was bumped so
  older state files are skipped safely instead of being misread.
- **Verification** — 577-image corpus regression: 571 images render, 0 regressions and 0 frame
  hash changes; 99 unit tests green, including expansion-audio tests that drive real boards and a
  VRC7 test that asserts the produced waveform moves off its DC baseline.

Known limitations (by design): PPU timing is scanline/pixel level rather than dot level, the
Zapper light gun and the FDS disk system are not emulated, NES 2.0 plane 1/2 mappers (256/512/558)
have no reference implementation to follow, and VRC7 tone generation is a compact approximation
of the real OPLL.

## v0.3.0

Release highlights:

- **Mapper audit and completion** — every mapper outside the previously verified list was audited
  against the Mesen2 reference (`Core/NES/Mappers/…`) and the local corpus. Sixteen boards were
  rewritten or corrected — 51 (BMC 11-in-1), 114 (scrambled MMC3), 117, 142 (Kaiser 202),
  149, 150 (Sachen 74LS374N), 171 (Kaiser 7058), 176 (**Waixing FK23C**: 12 MMC3 registers,
  `$5010-$501F` extensions, 32 KB of board WRAM, delayed scanline IRQ), 178, 207 (Taito X1-005
  alternate mirroring), 227, 240, 244, 245, 250 (MMC3 with the register index on address bit 10) —
  and eleven `romdb` overrides route dumps whose header lies to the board Mesen's database names
  (e.g. 四人街霸 → 189, 泰坦尼克号 → 241, 妖怪俱乐部 → 140, 星河战士 → 176). 40+ games now render
  that previously stayed blank, failed to load, or ran on the wrong board. Every fix carries a
  unit test that fails on the old implementation plus the authority it was checked against.
- **APU** — the **DMC channel** is implemented (rate table, `$4010-$4013`, 7-bit DAC, sample fetch
  through the CPU bus, loop/IRQ); the APU now drives its **own IRQ line** so a `$4015` read can no
  longer acknowledge a mapper interrupt; `$4015` bits 6/7 report the live interrupt flags and a
  `$4017` write resets the frame counter sequence.
- **Expansion audio** — new `NES_ENABLE_EXPANSION_AUDIO` switch (on for the SDL front ends, off and
  therefore free for MCU/core builds) plus a per-segment mixer hook and four chips: **Namco 163**
  (mapper 19/210/163), **VRC6** (24/26), **Sunsoft 5B** (69) and **MMC5** (5). Boards declare their
  chip through `nes_mapper.mapper_audio`; the chip state lives in the APU so it travels with save
  states. VRC7 (OPLL) is provided by the project's own compact FM model in src/nes_apu.c.
- **NES 2.0 plane 1/2 audit** — the four boards named in the plan were checked against Mesen2 before
  any code was written. 268 has an authority (`Mmc3Variants/MMC3_Coolboy`) and is a candidate for later;
  **256/512/558 have none** (Mesen2 itself only has `case 256: break;` and no case at all for 512/558),
  so they are deliberately **not** implemented rather than guessed. The images in the corpus that carry
  those headers already render through their header mappers, and the plane 1/2 checkboxes in the READMEs
  stay unticked, which matches this finding.
- **Mapper audit (27 boards rewritten or corrected, 14 romdb entries)** — every fix below was
  checked against Mesen2 (`Core/NES/Mappers/...`) or FCEUX first, and each one has a unit test that
  **fails if the fix is reverted**:
  - rewritten: 51, 142, 171, 176 (Waixing FK23C), 178, 198 (MMC3_198), 199 (MMC3_199, authority
    alignment only — its one ROM still does not render), 207, 208 (MMC3_208), 227, 240, 244, 245,
    250, 132 and 147 (the TXC scrambler, one JV001 and one non-JV001 variant);
  - decode corrected: 62, 86 (Jaleco JF-13 — all three fields were in the wrong place), 114, 115,
    117, 133 (Sachen SA-72007), 149, 150, 12 (the outer CHR bit had been dead code), 85 (VRC7 — the
    second register of each pair is selected by A3, not A4);
  - `src/nes_rom.c` romdb entries now override a wrong header mapper for 14 images (189, 241, 115,
    140, 176 ×2, 114, 178 ×2, 208, 198, 4);
  - boards 4/18/21/23/25/73/153/162/164/165/167 also allocate their on-board work RAM
    unconditionally (the iNES header often has no battery bit).
- **Audit result** — the collection-level audit (`test/audit_mappers.py`, 575 images) went from
  **36 mappers with an untrustworthy verdict down to 11**, and 40+ games changed from
  blank/load_fail/unregistered to playable. Remaining items are documented rather than guessed:
  79/12/86 only ever show a static screen (need a screenshot check), 242 and 天神之剑 do not render
  under any candidate mapper in this core *or* in Mesen (dump/hack issue), 116/153 need big board
  implementations, 111 (GTROM) needs core support for 16 banked nametables, and **512/256/558 are
  deliberately not implemented because Mesen2 has no implementation to follow** (its factory only has
  `case 256: break;` and no case at all for 512/558).
- **Tests** — 88 unit/stress cases and a 567-image corpus baseline; `-Strict` stays at
  REGRESSED 0 / HASH_DIFF 0. The expansion-audio module compiles away entirely when the macro is 0
  (verified: 86/87 cases in the two configurations).

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

## v1.0.0

发布要点：

- **APU 补全** —— DMC 通道完整实现（16 档 NTSC 速率、`$4010`-`$4013` 完整语义、样本经 CPU 总线取字节、结束时置 IRQ）；APU 中断线与 CPU 的共享标志分离（读 `$4015` 不再吞掉 mapper 中断）；`$4015`/`$4017` 语义修正（实时中断标志、帧计数器复位）。
- **扩展音频 5/5** —— VRC6（24/26）、Sunsoft 5B（69）、Namco 163（19/210/163）、MMC5（5），以及 **VRC7**（85）；整体由 `NES_ENABLE_EXPANSION_AUDIO` 控制（桌面默认开、核心/MCU 默认关，关闭时整块编译掉）。VRC7 是**本项目自研的紧凑定点 FM 模型**（不含第三方代码），带芯片内置乐器 ROM、6 个双算子通道、**不使用堆**；没有发声音的板子每采样只多一次判断。
- **mapper 审核收尾** —— 28 块板按 Mesen2 权威与本地语料重写/修正（12、51、62、79、85、86、114、117、132、133、142、147、149、150、171、176、178、187、198、199、207、227、235、240、242、244、245、250），并新增 14 条 romdb 条目修正头部谎报的镜像；verdict 不可信的 mapper 由 **36 降到 10**，40 多张游戏从 blank 或加载失败变为可玩。
- **状态与接口整理** —— 存档按值保存整个 APU（含扩展音频）；扩展音频并入 `nes_apu`；`nes_state_io.h` 并入 `nes_state.h`；全部头文件统一 `#pragma once`；APU 存档版本号提升，旧档按版本安全跳过而不会被误读。
- **验证** —— 577 张语料回归：571 张出画面、0 回归、0 帧哈希变化；99 个单元测试全绿，其中扩展音频用例驱动真实板子，VRC7 用例断言输出波形确实偏离直流基线。

已知限制（均为既定取舍）：PPU 为行/像素级精度而非逐点；不支持 Zapper 光枪与 FDS 磁碟机；NES 2.0 plane 1/2（256/512/558）无权威实现可循；VRC7 音色为紧凑近似。

## v0.3.0

- **mapper 审核与补全**：按 Mesen2 参考实现与本地语料逐块核对 README「已验证可玩」之外的板子，16 块板重写或修正（51、114、117、142、149、150、171、176、178、207、227、240、244、245、250 等），并新增 romdb 条目修正头部谎报的镜像。
- 详细内容与逐项权威出处见上方英文段（英文段为本文件的权威版本）。

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
