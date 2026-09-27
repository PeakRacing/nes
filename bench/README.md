# NES MCU 基准前端

`bench/` 是一个独立的无界面基准工程。它只给自身的 target 定义
`NES_TEST_MODE=1` 和 `NES_TEST_PROFILE=1`，生产 target 不会编译或链接这些测试钩子。

```powershell
cd F:\code\codeup\nes\bench
.\run.ps1 -Rom 'F:\code\codeup\nes\rom\mapper0\90坦克.nes' -Frames 300 -Regions 1 -Hash
```

也可以直接运行：

```powershell
.\out\bin\nes-bench-ref.exe <rom.nes> --frames 300 --regions 1 --hash 1
.\out\bin\nes-bench-mcu.exe <rom.nes> --frames 300 --regions 0
```

参数默认为 `--frames 300 --regions 1 --hash 0`。`--regions 0` 只计整帧，适合取得没有模块计时探针的帧耗时；
`--regions 1` 会输出 CPU、背景、精灵、APU 与 draw 的累计耗时和占整帧总时间的比例。`--hash 1` 计算当前帧缓冲的
FNV 校验值；每次报告都会提供轻量的 draw/audio 校验和。

## 三个配置

| target | 主要定义 |
| --- | --- |
| `nes-bench-ref` | `NES_COLOR_DEPTH=32`、`NES_RAM_LACK=0`、`NES_ROM_STREAM=0`、`NES_USE_SRAM=1` |
| `nes-bench-mcu` | `NES_COLOR_DEPTH=16`、`NES_RAM_LACK=1`、`NES_ROM_STREAM=1`、`NES_FRAME_SKIP=1`、`NES_USE_SRAM=0` |
| `nes-bench-mcu-cache` | 与 mcu 相同，另外设置 `NES_PRG_CACHE_SLOTS=8`、`NES_CHR_CACHE_SLOTS=32` |

三个 target 都启用声音、文件系统和全部 mapper 平面。MCU 配置以 RGB565、半帧绘图缓存和 PRG/CHR LRU 流式缓存，近似嵌入式端的
内存布局。每份 CSV 风格报告会列出编译配置、`nes_t`/绘图/ROM 缓存占用、帧时间、模块比例、流式命中与缺失，以及校验值。

## 解读与对比

桌面 CPU 的微秒数不能直接当作单片机性能结论。应使用同一台主机、同一 ROM、同一帧数和相同的 `--regions` 参数，比较优化前后的
模块占比与相对变化。`--regions 0` 用于比较无模块探针时的总帧时间；模块拆分则使用 `--regions 1`。

移植到真实 MCU 时，请将 `bench/main.c` 中的 `nes_test_time_us()` 改为单调的硬件计时实现，例如 DWT cycle counter 换算为微秒。
同时保留同一 ROM 和帧数，并先比较 `verify,frame_hash`，确认优化没有改变画面行为，再比较 `frame`、`region` 和 `stream` 行。

`ram,total_bytes` 是结构体及 ROM 缓冲估算，绘图缓冲已包含在 `nes_t` 中。
`heap,peak_bytes` 跟踪所有 `nes_malloc` 申请的有效字节，包括 mapper 与 SRAM；不包含分配器元数据、栈、SDL、文件系统内部缓存。
默认 draw/audio checksum 只采样每次输出的首尾字节。`--hash 1` 会累计每次输出的全部像素和音频字节，适合一致性验证；其额外成本不应混入性能比较。

`./run.ps1 -Rom <路径> -Only McuCache` 可测试扩容方案。实际 MCU 端在自己的 `nes_conf.h` 设置上述两个缓存宏即可；默认仍为 6/12 槽。8/32 比默认增加 36 KiB 数据缓冲和少量缓存元数据，是否采用应结合板端可用 RAM 与 SD/Flash 延迟决定。

SDL 测试构建可设置 `NES_TEST_PACINGLOG=<路径>`，逐帧记录提交间隔（微秒）。
该数据是应用提交时间，不是显示器实际扫描输出时间。
