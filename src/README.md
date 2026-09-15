# Cheat Engine for Linux（Linux 原生内存修改工具）

一个原生 Linux 的内存扫描器、调试器、反汇编器和修改器工具包，用 C++20/Qt6 从零重新实现了 [Cheat Engine](https://github.com/cheat-engine/cheat-engine)。它直接与内核交互（`process_vm_readv`/`writev`、`ptrace`、`/proc`，以及可选的辅助内核模块），而不是在 Wine 里运行 Windows 版；同时它能读写 Cheat Engine 的 `.CT` 表，因此你已有的表可以直接拿来用。

> ⚠️ **处于早期、尚不成熟。** 这是一个年轻的项目（v0.7.0），正在积极开发中。会有 bug、粗糙之处和缺失的功能，目前还远未达到 Cheat Engine 的功能广度与成熟度。请做好备份，只对你被允许分析的软件使用，并欢迎[提交问题](../../issues)，它们是推动项目前进的动力。

> **适用范围。** 为单机游戏、逆向工程和学习而构建。它*不是*用来对抗多人游戏反作弊（EAC、BattlEye、Vanguard 之类）的，那类系统运行着内核组件，会检测此类工具。请负责任地使用。

约 53k 行 C/C++ 代码，分布在约 200 个文件中；配有约 320 项检查的回归测试套件，CI 中还有 ASan/UBSan 与离屏 GUI 冒烟测试。

---

## 性能

Linux 上最快的内存扫描器。在同一台机器、同一目标的基准测试中，首次扫描一个数值约比 **Cheat Engine 7.7**（官方原生 Linux 版）**快 2 倍**，比 **scanmem、GameConqueror 和 PINCE** 快 **30 到 40 倍**，某些扫描场景差距更大（相对 Cheat Engine 最多约 13 倍，相对 scanmem 最多约 145 倍）。\*

| 首次扫描，1 GB，精确 int32 | 耗时 | 吞吐量 |
|---|---:|---:|
| 本项目 | **0.085 s** | 约 12 GB/s |
| Cheat Engine 7.7（原生 Linux） | 0.156 s | 约 6.6 GB/s |
| gdb `find` | 0.749 s | 约 1.4 GB/s |
| scanmem 0.17 / GameConqueror | 2.924 s | 约 0.34 GB/s |
| PINCE (libmemscan) | 3.480 s | 约 0.29 GB/s |

\* “最多”指的是最好情况（与 Cheat Engine 比较的是浮点取整扫描和字节模式扫描；与 scanmem 比较的是已保留但未触碰的内存）；相对 CE 7.7 的典型首次扫描领先幅度约为 2 倍。测试在同一台机器上进行（Intel i5-10500H，12 线程）；Cheat Engine 的计时不含其 GUI 启动时间（这是对它有利的算法）。GameConqueror 使用 scanmem 的引擎；PINCE 使用它自己的 Zig 后端（libmemscan），此处按精确值扫描计时。完整数据、测试方法与复现步骤见 **[BENCHMARK.md](BENCHMARK.md)**。

---

## 功能

- **扫描** — 支持全部数值类型（int/float/double、可指定 iconv 编码的字符串、带通配符的字节数组、二进制、分组、自定义 Lua）；支持 Cheat Engine 的全部比较方式（精确/大于/小于/介于/未知值 → 已变动/未变动/已增加/已减少/按数值增减/与首次扫描相同）；支持 CE 的浮点取整模式；内存区域过滤；对齐；针对超大目标的多线程、可落盘的扫描；撤销；以及带重新扫描和可分片分布式扫描的指针扫描。
- **编辑** — 按类型读写、方向性冻结（锁定 / 只增 / 只减 / …）、分组与批量编辑、数值热键，以及带指针表达式（`module+offset`、`[[base]+off]`）、分组、颜色和下拉选项的地址列表记录。
- **调试器** — 硬件断点（DR0-DR3）与软件断点、数据（写入/访问）监视点、**条件断点**（在寄存器状态上运行沙箱化 Lua）、启用/禁用与命中计数、异常时中断、单步执行、中断并跟踪、寄存器/栈/线程视图，以及“找出是什么访问/改写了这个地址”。
- **反汇编器** — 基于 Capstone，支持跳转箭头、交叉引用与分支目标解析、`@plt`/`@got` 导入名、DWARF 源码行，以及可持久保存的用户注释和标签。
- **Mono / Unity 剖析器** — 注入一个进程内代理，向 Mono 运行时询问真实的类与字段布局（真实偏移、类型、静态字段），可在 GUI 中浏览（**工具 ▸ Mono 剖析器**）或从 Lua 调用（`monoDissect()`、`findMonoFunction()`）。可识别 IL2CPP 目标。
- **表与脚本** — 读写 CE 的 `.CT`（XML）、受密码保护的 `.CETRAINER` 以及原生 JSON；可运行表中的 Lua；可生成独立的 C 修改器。提供一套与 CE 兼容的广泛 Lua API（内存、扫描、地址列表、反汇编器、热键、定时器、挂钩），真实的作弊表可以直接使用。
- **自动汇编** — 与 Cheat Engine 兼容：`alloc`/`globalalloc`、标签、符号、`aobscanmodule`、数据指令、`{$lua}` 块，以及标准的代码注入模板。
- **平台能力** — ceserver 与 GDB 远程客户端（用于远程/跨设备调试）、X11 点击穿透悬浮层、保持音调的变速（`LD_PRELOAD`），以及可选的、需要 `CAP_SYS_ADMIN` 的辅助内核模块（用于特权内存访问）。
- **诊断** — `CE_LOG=debug`（或按子系统，例如 `CE_LOG=ptrace:trace`）可在无需重新编译的情况下打开运行期日志。
- **界面本地化** — 界面提供英文和简体中文（`translations/cheatengine_zh_CN.ts`）。默认跟随系统语言，也可以在 **编辑 ▸ 设置 ▸ 语言** 中固定。新增语言只需对 `translations/*.ts` 做一轮 `lupdate` / `lrelease`。

## 编译

```bash
sudo apt install build-essential cmake qt6-base-dev libcapstone-dev \
                 zlib1g-dev libdw-dev linux-headers-$(uname -r)

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Keystone（汇编后端）在系统里找不到时会自动下载并编译；Lua 5.3 已随仓库一起提供，因此本仓库可以离线构建，无需额外步骤。每个 [release](../../releases) 都会附上预编译的 `.deb` 和 AppImage 包。

可选的内核辅助模块：

```bash
make -C /lib/modules/$(uname -r)/build M="$PWD/kernel" modules
sudo insmod kernel/cecore_kmod.ko    # /dev/cecore ；sudo rmmod cecore_kmod 卸载
```

## 运行

```bash
sudo LD_LIBRARY_PATH=build build/cheatengine        # 图形界面
sudo LD_LIBRARY_PATH=build build/cescan --help      # 命令行扫描器
CE_SPEED=2.0 LD_PRELOAD=build/libspeedhack.so ./game # 变速（保持音调）
```

基于 `ptrace` 的附加需要较高权限或宽松的 `ptrace_scope`；安装的 `.deb`/AppImage 会设置 `cap_sys_ptrace`，因此无需 root。

## 命令行参考

```text
cescan list                          列出所有进程
cescan scan <pid> [options]          扫描进程内存
cescan read <pid> <addr> [size]      以十六进制转储内存
cescan write <pid> <addr> <val>      写入指定类型的数值
cescan disasm <pid> <addr> [count]   反汇编指令
cescan modules|regions <pid>         列出已加载模块 / 内存区域
cescan signature <pid> <addr> [max]  生成唯一的字节数组特征码
cescan analyze <pid> <what>          静态逆向：strings|statics|caves|functions|xrefs|asm
cescan il2cpp <global-metadata.dat>  浏览 Unity IL2CPP 元数据（离线）
cescan lua <script.lua>|-e <code>    运行 Lua（与 GUI 控制台同一套 API）
```

常用扫描选项：`--type byte|i16|i32|i64|float|double|string|aob|…`、`--value`/`--value2`、`--compare exact|greater|less|between|changed|…`、`--rounding`、`--previous <dir>`、`--writable`。

## 脚本与逆向工程

Lua API（GUI 控制台或 `cescan lua`）同时驱动整个静态分析栈：IL2CPP（Unity）类/字段/方法解析、DWARF 结构体类型还原、PE 导出/导入解析、AOB 特征码生成、交叉引用，以及范围反汇编。参考手册见 **[docs/SCRIPTING.md](docs/SCRIPTING.md)**，可直接运行的脚本见 **[examples/](examples)**。

## 自动汇编示例

```asm
[ENABLE]
aobscanmodule(INJECT, game, 48 89 45 10)   // 唯一特征码
alloc(newmem, $1000, INJECT)
label(return)
newmem:
  mov dword [rax+10], 999
  jmp return
INJECT:
  jmp newmem
return:
registersymbol(INJECT)

[DISABLE]
INJECT:
  db 48 89 45 10
unregistersymbol(INJECT)
dealloc(newmem)
```

## 开发

```bash
./build/cecore_test              # 回归测试套件
tools/ci-check.sh --config       # 推送前在本地复现 CI（见 docs/DEVELOPMENT.md）
```

```text
analysis/  代码分析、托管运行时与 Mono 剖析器        gui/       Qt6 界面、反汇编器、悬浮层
arch/      Capstone 反汇编 / Keystone 汇编           kernel/    可选的特权辅助模块
cli/       cescan 命令行工具                         platform/  进程 API、ptrace、注入器、ceserver
core/      类型、自动汇编、表达式、表、挂钩          plugins/   变速、mono 代理、音频
debug/     断点、调试会话、跟踪、GDB 远程            scanner/   内存与指针扫描器
scripting/ Lua 引擎与绑定                            symbols/   ELF/DWARF 与内核符号
```

## 安全

- **不可信输入。** ELF/DWARF 解析器、表加载器、自动汇编器以及 Lua 断点条件都把输入当作不可信数据：读取有边界检查，`.CETRAINER` 加载有大小上限，条件运行在受限执行步数的沙箱化 Lua 状态中。请只打开你信任的 `.CT`/`.CETRAINER` 文件，表中的 Lua/AA 可以操纵目标进程，运行时会有确认提示。`shellExecute` 与不安全的写文件函数默认禁用。
- **内核辅助模块。** `kernel/cecore_kmod.c` 是可选的，仅通过 `/dev/cecore` 暴露显式受 `CAP_SYS_ADMIN` 限制的 ioctl。它不会隐藏模块、文件或套接字。

## 许可

MIT。灵感来自 Dark Byte 的 [Cheat Engine](https://github.com/cheat-engine/cheat-engine)；再分发衍生资源或兼容性数据前，请先查阅上游的许可条款。
