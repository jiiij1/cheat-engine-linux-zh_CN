# 扫描器基准测试

本项目的内存扫描器找数值有多快？这里与其他能扫描 Linux 实时进程的工具做了正面比较。本文的每个
数字都是同一台机器、同一目标、同一数值、同一次运行下的真实测量结果。

如果你发现某个场景下别的工具更快，欢迎附上环境信息开 issue：这些数字是为了可复现且诚实，而不是
营销话术。

## 太长不看

在 12 线程笔记本上，用精确值扫描一个进程：

| 对比对象 | 典型 | 最好情况 |
|---|---|---|
| **Cheat Engine 7.7**（原生 Linux 版） | 快约 2 倍 | 最多约 13 倍（浮点取整 / 模式扫描） |
| **scanmem 0.17**，即 GameConqueror 的引擎 | 快约 30 到 40 倍 | 最多约 145 倍（已保留内存） |
| **PINCE**（自研 Zig 扫描器 libmemscan） | 快约 40 倍 | 不适用 |
| **gdb** `find` | 快约 8 到 9 倍 | 不适用 |
| **radare2** `/v` | 快约 1000 倍 | 不适用 |

在测试过的所有数值类型和大小上，我们都比所有工具更快。领先幅度最小的是对齐整数首次扫描
（约 1.8 倍，CE 的扫描器确实不错），最大的是浮点/模式扫描、已保留地址空间，以及密集的再次扫描。

## 机器与方法

- **CPU：** Intel Core i5-10500H（6 核 / 12 线程），每核 256 KiB L2。
- **系统：** Linux 6.17，`ptrace_scope=0`（同用户扫描，无需 root）。
- **目标：** 一个进程 `mmap` 一块匿名区域，用伪随机数据填满，并在其中散布 64 份待查找的数值
  （这样匹配数就由该区域主导，且每个工具扫描的都是同样的主体）。源码见本文末尾。
- **计时：** 5 次取最好（radare2 只跑 1 次，它太慢了）。对所有工具而言目标内存都是驻留的（热的）。
- **公平性：** 所有工具用相同对齐方式搜索同一数值，且匹配数一致（cescan/CE/gdb 找到 65 个；
  scanmem 找到 64 个，有一个散落的匹配因工具而异）。当某工具的默认设置不同时，已调整为一致
  （scanmem `scan_data_type int32`，CE `fsmAligned "4"`）。

### 各工具说明（引用数字前请先读）

- **cescan**：本项目的命令行工具（`cescan scan <pid> --type i32 --value <v>`）。按完整进程调用
  计时（其启动只需几毫秒）。
- **Cheat Engine 7.7**：官方原生 Linux 版，通过 `autorun` Lua 脚本驱动（`createMemScan`、
  `firstScan`、`waitTillDone`）。使用 `getTickCount` 在 CE *内部*计时，这**不含 CE 长达数秒的
  GUI 启动时间**，也就是说这个比较是偏向 CE 的（只算纯扫描时间）。
- **scanmem 0.17**：Linux 上事实上的内存扫描器。**GameConqueror 使用 `libscanmem`**
  （已核实：`gameconqueror` 的 `Depends: scanmem` 并链接 `libscanmem.so`），所以它的扫描速度等于
  这一列；GUI 只是额外增加了开销。通过其命令会话驱动。
- **PINCE**（git `ae181c3`）：一个 GDB 前端，其扫描引擎现在是自己用 Zig 写的库 `libmemscan`
  （`brkzlr/libmemscan`），用 Zig 0.16.0 构建，并通过 PINCE 自己的 `memscan.py` 无界面驱动
  （`attach`、`set_data_type INTEGER32`、`set_alignment 4`、`scan MATCHEQUALTO`）。PINCE 过去
  会 fork scanmem，现在不再如此。这里只比较精确值扫描；PINCE 表示其 Zig 后端在*未知值*（快照）
  扫描上快得多，而本基准测试并不涵盖那种场景。
- **gdb 15.1**：`find /w <start>, <end>, <value>`，直接指向目标区域。它是调试器而非扫描器：
  不会枚举内存区域，也不会缩小结果集，是直接被告知要搜索哪块区域的。
- **radare2 5.5.0**：对 `dbg.maps` 执行 `/v4`。这是逆向工程框架而非扫描器，仅作参考。

## 首次扫描：精确 int32，对齐（即“查找某个数值”的操作）

墙钟时间，5 次取最好。越低越好。

| 区域 | **cescan** | CE 7.7 | gdb `find` | scanmem / GC | PINCE | radare2 |
|---:|---:|---:|---:|---:|---:|---:|
| 256 MB | **0.022 s** | 0.057 s | 0.274 s | 0.738 s | 0.849 s | 34.3 s |
| 512 MB | **0.046 s** | 0.081 s | 0.413 s | 1.457 s | 1.745 s | 不适用 |
| 1 GB | **0.085 s** | 0.156 s | 0.749 s | 2.924 s | 3.480 s | 不适用 |
| 2 GB | **0.149 s** | 0.281 s | 1.291 s | 5.973 s | 7.251 s | 不适用 |
| 1 GB，已保留区域¹ | **0.020 s** | 0.131 s | 约 0.75 s² | 2.900 s | 3.434 s | 不适用 |

1 GB 时的吞吐量：**cescan 约 12 GB/s**，CE 7.7 约 6.6 GB/s，gdb 约 1.4 GB/s，scanmem 约
0.34 GB/s，PINCE 约 0.29 GB/s，radare2 约 0.03 GB/s。PINCE 的 Zig 扫描器是单线程的，在精确值
扫描上与 scanmem 接近。

cescan 在 1 GB 时的加速比：相对 CE 7.7 为 **1.8 倍**，相对 gdb 为 8.8 倍，相对 scanmem 为 34 倍，
相对 PINCE 为 41 倍，相对 radare2 约 1000 倍。

¹ 进程已保留但基本从未触碰的区域（游戏引擎预分配大块 arena 时很常见）。cescan 通过
`/proc/pid/pagemap` 只读取驻留页；其他工具会读取整块。
² gdb 和 radare2 不会跳过未驻留页，所以对它们来说“已保留”与“已触碰”耗时基本相同。

## 再次扫描：缩小结果集

只有带状态的扫描器能做这件事（gdb/radare2 不能）。对上一次的匹配重新扫描，5 次取最好。

| 结果集 | **cescan** | CE 7.7 | scanmem / GC |
|---|---:|---:|---:|
| 密集，419 万个匹配（类似数组） | **0.071 s** | 0.199 s（2.8 倍） | 约 2.49 s（35 倍） |
| 分散，105 万个匹配 | **0.098 s** | 0.157 s（1.6 倍） | 不适用 |

当匹配是密集排列时（结构体数组的字段），我们的领先最大，因为此时再次扫描的读取会合并成大块传输；
在广泛分散的匹配上，相对 CE 会收窄到约 1.6 倍。

## 数值类型：首次扫描，1 GB

除特别说明外，各工具找到的匹配数一致。

| 类型 | **cescan** | CE 7.7 | scanmem / GC |
|---|---:|---:|---:|
| 浮点，位精确 | **0.073 s** | 不适用（无位精确模式） | 2.906 s（40 倍） |
| 浮点，取整（CE 默认） | **0.050 s** | 0.648 s（**13 倍**） | 不适用（仅精确） |
| 字符串 | **0.076 s** | 0.634 s（8.3 倍） | 2.890 s（38 倍） |
| 字节数组 / 模式 | **0.071 s** | 0.610 s（8.6 倍） | 3.004 s（42 倍） |

字节模式（字符串 / AOB）扫描相对 CE 7.7 的领先最大（约 8 到 9 倍）：它们受计算限制（每个未对齐
偏移都要比较），而 cescan 每步先用 SIMD 排除 16 个偏移，然后再做完整比较。

## 为什么更快

优势来自内存流水线，而不是聪明的算术：

- **按缓存块读取：** 以 L2 大小的块读取内存，使扫描在缓存中运行而非访问内存（读取本身约快 3 倍）。
- **单区域用满所有核心：** 一块大的映射会被拆分到多个线程。
- **跳过未触碰的页：** 对已保留空间通过 pagemap 只读驻留页。
- **再次扫描的读取合并：** 连续或近似连续的匹配会合并成一次大的 `process_vm_readv`，而不是几百万次
  小读取。
- **只在划算时才用 SIMD：** 精确/取整的数值扫描与字节模式扫描；多数数值扫描受内存限制，此时 SIMD
  是免费的。

完整变更记录见 [CHANGELOG.md](CHANGELOG.md) 中 “Performance” 一节。

## 注意事项

- 只有一台机器、一个合成负载。真实游戏的内存区域布局和匹配密度各不相同，你测出的比例会不一样。
  可靠的是**相对顺序**（cescan > CE 7.7 > gdb > scanmem/GameConqueror > PINCE > radare2）。
- “最多”指的是最好情况（浮点取整、已保留内存）。相对 CE 7.7 的典型首次扫描领先约为 2 倍。
- CE 7.7 的计时不含其 GUI 启动时间，这是偏向 CE 的。
- 工具版本很重要（CE 7.7、scanmem 0.17、gdb 15.1、radare2 5.5.0）。scanmem 0.17 是当前发行版。

## 复现方法

上面使用的目标程序：

```c
// target.c  (gcc -O2 target.c -o target ;  ./target <MB> <reserved 0|1> <dense_stride>)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
int main(int argc, char** argv) {
    size_t mb = argc > 1 ? atol(argv[1]) : 512;
    int reserved = argc > 2 ? atoi(argv[2]) : 0;
    size_t stride = argc > 3 ? atol(argv[3]) : 0;  // >0: 每隔 `stride` 字节放一个数值
    size_t N = mb * 1024 * 1024;
    uint32_t* buf = mmap(0, N, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | (reserved ? MAP_NORESERVE : 0), -1, 0);
    const uint32_t SENT = 0x5A5A1234u;
    if (!reserved) {
        uint64_t x = 88172645463325252ull;
        for (size_t i = 0; i < N / 4; i++) { x ^= x<<13; x ^= x>>7; x ^= x<<17;
            buf[i] = (uint32_t)x & 0x0FFFFFFFu; }               // 永不等于 SENT
    }
    if (stride) for (size_t o = 0; o + 4 <= N; o += stride) buf[o/4] = SENT;
    else        for (int k = 0; k < 64; k++) buf[((N/65)*(k+1))/4] = SENT;
    printf("PID=%d SENT=%u\n", getpid(), SENT); fflush(stdout);
    pause(); return 0;
}
```

然后针对它打印出的 `PID` 和 `SENT`（`0x5A5A1234` = 1515852340）执行：

```bash
# 本项目
cescan scan  $PID --type i32 --value $SENT

# scanmem（GameConqueror 的引擎）
printf 'option scan_data_type int32\noption region_scan_level 3\n%s\nexit\n' $SENT | scanmem -p $PID

# gdb（从 /proc/$PID/maps 里找到区域并指向它）
gdb -p $PID -batch -ex "find /w $START, $END, $SENT" -ex detach -ex quit

# radare2
r2 -q -n -e search.in=dbg.maps -c "/v4 0x5A5A1234; q" -d $PID
```

PINCE 通过它自己的扫描库驱动：克隆 `korcankaraokcu/PINCE`（加 `--recursive`），用
`zig build -Doptimize=ReleaseFast` 构建 `libmemscan` 子模块（Zig 0.16.0），然后使用 PINCE 的
`memscan.py`：

```python
import memscan as M
lib = M.Libmemscan("libmemscan/zig-out/lib/libmemscan.so")
lib.attach(PID); lib.set_scan_level(M.ScanLevel.ALL_RW)
lib.set_data_type(M.DataType.INTEGER32); lib.set_alignment(4)
lib.scan(M.MatchType.MATCHEQUALTO, SENT)   # 计时；lib.get_match_count() -> 匹配数
```

Cheat Engine 7.7 由一个 `autorun` Lua 脚本驱动，执行 `openProcess`、`createMemScan`、
`firstScan(soExactValue, vtDword, rtRounded, tostring(SENT), "", 0, 0xffffffffffffffff, "",
fsmAligned, "4", ...)`、`waitTillDone`，并用 `getTickCount` 计时。
