# `.nessave` 存档格式规范（v1）

本文档描述本模拟器的即时存档文件格式，供第三方工具识别、检查或转换。**这不是跨模拟器通用格式**——
即时存档天然与实现绑定；能跨模拟器互通的是电池存档（裸 SRAM，`<游戏名>.sav`）。

相关实现：`inc/nes_state_io.h`（段 IO）、`inc/nes_state.h`（公共 API）、`src/nes_state.c`（读写与校验）。

## 文件命名

| 文件 | 内容 | 说明 |
|---|---|---|
| `<ROM 同目录>/<游戏名>.nessave` | 本模拟器的即时存档（自描述，含 SRAM） | 扩展名由 `NES_STATE_EXT` 决定 |
| `<ROM 同目录>/<游戏名>.sav` | 游戏自己的电池存档（裸 SRAM，当前 8KB） | 与 Mesen / VirtuaNES / FCEUX 等互通；`NES_STATE_BATTERY_FILE=0` 时不写 |

「游戏名」= ROM 文件名去掉扩展名（`rom/mapper68/Maharaja.nes` → `Maharaja.nessave`）。

## 字节序与总体结构

**全部小端**，按字节写出（不 dump 结构体原始内存布局）。整个文件：

```
Header (32 字节)
Section 0 .. Section N-1   （有序）
END 段（最后一个）
```

## 头部（32 字节）

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | `char[8]` | `magic` | 固定 ASCII `"NESSTATE"` |
| 8 | `u16` | `format_ver` | 容器格式版本，当前 `1`；读方遇到**更高**版本直接拒绝 |
| 10 | `u16` | `header_size` | 头部长度，当前 `32` |
| 12 | `u32` | `rom_crc` | PRG+CHR 的 CRC32，用于 ROM 身份校验 |
| 16 | `u16` | `mapper` | 有效 mapper 号（romdb 纠正之后的值） |
| 18 | `u8` | `flags` | bit0 含 `SRAM`；bit1 含 `CRAM`；bit2 含 `APU`；bit3 状态不完整（partial）；bit4 压缩（预留，v1 恒 0） |
| 19 | `u8` | `reserved` | 0 |
| 20 | `u32` | `payload_size` | 头部之后的全部字节数（含所有段头、段体和 `END` 段） |
| 24 | `u32` | `payload_crc` | `payload_size` 字节的 CRC32（含段头与长度字段，即文件里最终写下的字节） |
| 28 | `u32` | `header_crc` | 前 28 字节的 CRC32 |

CRC32 使用反射多项式 `0xEDB88320`，初值 `0xFFFFFFFF`，结果取反（与 zip/PNG 的 CRC32 相同）。

## 段（section）

每个段：`tag[4] + version(u8) + reserved[3] + len(u32) + body[len]`；`len` 不含这 12 字节。

| tag | 版本 | 内容（`=` 表示小端整型） |
|---|---|---|
| `INFO` | 1 | `producer[16]`（`"PeakRacing-nes"`）、`version[16]`（模拟器版本字符串）、`mapper=u16`、`prg_16k=u16`、`chr_8k=u16`、`rom_crc=u32`、`timestamp=u32`（无 RTC 时为 0）、`name_len=u16`、`name[name_len]`（ROM 文件名） |
| `BUS ` | 1 | `scanline=u16`、`frame_skip=u8`、`four_screen=u8`、`prg_src[4]`、`prg_bank[4]=u16`、`chr_src[8]`、`chr_bank[8]=u16`、`nt_src[4]`、`nt_page[4]=u16` |
| `CPU ` | 1 | `A,X,Y,SP=u8`、`PC=u16`、`P=u8`、`irq_counter,irq_nmi,irq_nmi_delay,irq_pending,opcode=u8`、`pad=u8`、`cycles=u16`、`joypad=u16`、`cpu_ram[2048]` |
| `PPU ` | 1 | `ppu_ctrl,ppu_mask,ppu_status=u8`、`x_w=u8`（bit0-2 fine X，bit3 w）、`oam_addr,buffer=u8`、`v_reg,t_reg=u16`、`ppu_vram[4096]`、`oam_data[256]`、`palette_indexes[32]` |
| `APU ` | 1 | `apu_size=u32` + `apu_size` 字节（`nes_apu_t` 的原始镜像；尺寸不符时整段跳过）。仅在 `NES_ENABLE_SOUND=1` 时存在 |
| `MAP ` | 1 | `reg_size=u16` + `reg_size` 字节（mapper 的寄存器块；尺寸由 `nes_mapper_register_alloc()` 登记）+ 可选的回调追加数据 |
| `SRAM` | 1 | `len=u32` + `len` 字节（电池 RAM；当前 8KB）。读方按 `min(len, 8192)` 拷贝，其余补 0 |
| `CRAM` | 1 | `len=u32` + `len` 字节（CHR-RAM 板的 8KB 图案底存）。仅当 `chr_rom_size == 0` 时存在 |
| `END ` | 1 | 4 字节 `"SAVE"`，标记文件结束（并让截断文件一眼可见） |

`BUS ` 段里的来源类型（`*_src`）：`0=ROM 页号`、`1=核心 RAM 缓冲（CHR-RAM）`、`2=PPU VRAM 页`、`3=mapper 私有 RAM`。
读档时按来源类型重建指针：ROM 页走 `nes_load_prgrom_*` / `nes_load_chrrom_*` 重新装载；VRAM 页直接指向
`ppu_vram`；`3` 由 mapper 的 `mapper_state_reapply()` 回调重建。

## 读取流程（两遍，失败不改状态）

1. **第一遍（只校验）**：魔术、`format_ver`、`header_size`、`header_crc`；按段表遍历检查 `len` 不越界；
   对 `payload_size` 字节算 CRC 并与 `payload_crc` 比对；确认存在 `END ` 段；比对 `rom_crc` 与 `mapper`。
   任一项失败直接返回错误码，**模拟器状态一个字节都不动**。
2. **第二遍（应用）**：逐段恢复；未知 tag 直接跳过（因此**新增段不会破坏旧版读取**）；
   应用完成后刷新调色板缓存、清零跳帧计数（保证下一帧重绘整屏）、并把 SRAM 回写到 `.sav`。

错误码：`-2` 参数错误、`-3` IO 失败、`-4` 不属于当前 ROM、`-5` 格式/版本不支持、`-6` 校验失败、`-7` 文件截断。

## 演进政策（照 RustyNES ADR-0003 的思路）

- **同一 `format_ver` 内**：某个段只能**在 body 尾部追加字段**，并把该段 `version + 1`；旧读方按版本号决定解析到哪。
- **新增段**：直接加新 tag；旧读方跳过；新读方容忍旧档中该段缺失（按默认值处理）。
- **破坏性布局变更**：提升 `format_ver`，旧读方以 `-5` 明确拒绝，不做静默迁移。
- 段版本号与 `format_ver` 分离：改某个芯片/板子的状态只动那个段的版本，不动容器版本。

## 兼容性说明

- 未压缩（快照 15–40KB）。头部预留了 `flags.bit4` 供将来加压缩。
- `.nessave` 内的 SRAM 与 `<游戏名>.sav` 内容一致：存档时写 `.sav`，读档后也用快照里的 SRAM 刷新 `.sav`，
  所以「读档」= 画面与游戏内存档一起回到那一刻。
- 单文件覆盖式（无槽位）。将来要加槽位只需改文件名（例如 `<游戏名>_1.nessave`），**不动格式**。
