# NES 自动化测试

测试工程只在显式定义 `NES_TEST_MODE=1` 时编译（`test/xmake.lua` 负责加宏，并在生产构建里
`remove_files(src/nes_test.c)`）。SDL2 / SDL3 / RT-Thread 生产构建不扫描 `test/`，也不包含测试接口。

运行器是纯 C（`test/runner`），自带假平台端口：`nes_draw` 为空实现，`nes_frame` 只喂帧探针与帧预算刹车，
所以整机可以无窗口、无音频设备地跑完 195 个 ROM。

## 快速开始

```powershell
# Windows（在 test/ 目录或仓库根目录都可以）
./run_tests.ps1 all                  # 单元测试 + mapper 冒烟（秒级）
./run_tests.ps1 corpus               # ROM 语料回归（需要本机 rom/ 目录）
./run_tests.ps1 corpus -UpdateBaseline   # 首次运行/接受当前结果时刷新基线
./run_tests.ps1 corpus -Mapper 4     # 只跑 rom/mapper4/ 下的镜像
./run_tests.ps1 corpus -MaxRoms 20 -Frames 60   # 快速抽样
./run_tests.ps1 corpus -Isolated     # 逐 ROM 独立进程，崩溃只损失一个镜像
./run_tests.ps1 all -Stream          # 用 NES_ROM_STREAM=1 目标再跑一遍
```

```sh
# Linux / macOS
./run_tests.sh all
./run_tests.sh corpus "" --update-baseline
```

分类：`cpu`、`ppu`、`apu`、`rom`、`mapper`、`stress`、`corpus`、`all`。
**`all` 不含 `corpus`**：语料会持续增长且依赖本机 ROM，必须显式 `--filter corpus` 才跑。

## 单元测试覆盖

| 分类 | 用例 |
|---|---|
| cpu | opcode coverage（151 官方指令 / 13 寻址模式）、opcode cycles（golden 周期 + 跨页/分支惩罚）、flags（ADC/SBC 溢出矩阵、CMP、移位、2A03 无十进制）、stack and control flow（JSR/RTS/RTI/BRK/NMI/IRQ 压栈逐字节）、undocumented opcodes（全部 256 项可执行 + LAX/SAX/DCP/ISC/SLO/ANC/ALR 语义）、interrupt timing（NMI 一指令延迟、IRQ 电平触发、CLI/SEI/PLP 延迟、RTI 立即生效）、oam dma（旋转、513+1 周期、非内存源） |
| ppu | registers and VRAM（含 $2007 缓冲读）、scroll and address、palette mirroring（$3F10/14/18/1C 读写双向镜像）、chr write protection（CHR-ROM 与 nametable 槽位）、mirroring table（4/H/V/单屏/AUTO）、sprite 0 hit and priority（确定性场景：命中、sprite 颜色、priority 保留背景）、rendering（帧哈希与可复现性） |
| apu | length counters and 4015（使能后才装载、关闭即清零、递减）、frame counter irq（4-step 产生 IRQ、$4017 inhibit 同时清 frame_interrupt 与 irq_pending、5-step 不产生）、samples（有源非零且 ≤255、关闭后归零） |
| rom | layout and CRC（trainer/SRAM/CRC/字段）、invalid images（截断、魔数、缺 CHR）、header variants（NES 2.0 12 位长度与 mapper、脏 iNES 头只取低 nibble）、stream consistency（仅 `nes-tests-stream`，否则 SKIP） |
| mapper | dispatch and banks、bank helpers（8K/16K/32K PRG 折回、1K/4K/8K CHR 折回、CHR-RAM 恒等映射）、**synthetic smoke**（无需 ROM：探测 0~255，对每个可用 mapper 的 CHR-ROM/CHR-RAM 两种板型做加载 + 银行自检 + 跑 2 帧）、**write storm**（每个 mapper 对 $8000/$A000/$C000/$E000/$6000 写入全部 256 个值，每轮检查银行表非空）、bank stress（MMC3 + 256 个 1KB CHR bank 溢出回归） |
| stress | CPU million instructions（100 万条指令不越界、不停滞） |

## ROM 语料回归（corpus）

语料就是本机的 `rom/` 目录：递归扫描所有 `*.nes`，父目录名形如 `mapper4` 时记录为“期望 mapper”。
**没有手工清单**，所以新增 ROM、补目录都不需要改代码。

每个镜像：加载 → 记录 ROM 头 mapper 与实际生效 mapper（含 `src/nes_rom.c` 的 romdb 纠正）
→ 跑 `--frames`（默认 180）帧，帧间统计渲染开关、画面是否整屏单色、逐帧哈希链。

判定（verdict）：

| verdict | 含义 |
|---|---|
| `ok` | 渲染已开启且出现过非单色帧 |
| `suspect` | 渲染开启但全程单色（典型：CHR 映射错导致 tile 全同） |
| `blank` | 全程未开启渲染（黑屏类 bug） |
| `load_fail` | 镜像加载失败（mapper 不支持、头部损坏等） |

与基线 `test/baseline/corpus.csv` 对比（按 **sha256** 匹配，不看路径）：

| 情况 | 状态 | 是否失败 |
|---|---|---|
| 基线中没有这个 sha256 | `NEW` | 否（新 ROM 只记录） |
| 基线 ok/suspect → 现在 blank/load_fail | `REGRESSED` | **是** |
| 基线 ok → 现在 suspect | `SUSPECT` | 否（仅报告） |
| 基线 blank/load_fail → 现在 ok | `IMPROVED` | 否（可用 `-UpdateBaseline` 接受） |
| 基线有、本机没有该 ROM | 计入 “not present locally” | 否（条目保留在基线里） |
| `--frames` 与基线不同 | `STALE_FRAMES` | 否（帧数一致才比哈希） |
| `--strict` 且哈希链不同 | `HASH_DIFF` | 是（默认关闭） |

产出（`test/out/`）：

- `corpus.csv`：逐镜像全字段结果（status、verdict、sha256、label、三种 mapper、帧数、哈希链、首次渲染帧）。
- `mapper_coverage.csv`：每个 mapper 有几张镜像、各自 verdict、以及核心支持但**没有 ROM** 的 mapper。
- `mapper_mismatch.csv`：目录期望 mapper ≠ 实际生效 mapper 的镜像 —— 这是给 `romdb` 纠错表挑候选的清单。
- `mapper_report.md`：可直接粘进 `README_zh.md` 的“已测试”mapper 列表。

### 语料增长时怎么办

什么都不用做：新镜像记为 `NEW`，不会让测试变红；确认无误后跑一次 `-UpdateBaseline` 把它们收进基线。
基线按 sha256 索引，改名/移动/换机器都不会让它失效，而且**只含哈希与文件名，不含 ROM 数据，可以提交**。

### 崩溃隔离

若某个镜像让模拟器崩溃（例如某 mapper 写坏了 bank），默认的整批运行会在该镜像处中断：
运行器会在每个镜像前 flush 一行 `RUN <label>`，所以日志最后一行就是元凶。
用 `run_tests.ps1 corpus -Isolated` 可改为逐镜像独立进程，崩溃只影响一个镜像，结果汇总后再统一生成报告。

## 注意事项

- 非 ASCII 文件名在 Windows 上是按本地代码页读出来的，控制台里可能显示为乱码；报告与基线不受影响（索引是 sha256）。
- 测试 ROM 不进入版本库（`.gitignore` 排除 `/rom` 与 `*.nes`）。
- 语料运行时间随镜像数与帧数增长（195 个镜像 × 180 帧 ≈ 45 秒，Debug 构建），可用 `-Frames` / `-Mapper` / `-MaxRoms` 收敛。
- 改动核心后先跑 `./run_tests.ps1 all`（秒级）再跑 `./run_tests.ps1 corpus` 确认没有把别的游戏改坏。
