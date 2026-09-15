# 贡献指南

感谢你的关注。这是用 C++20 / Qt6 / CMake 编写的 Linux 原生 Cheat Engine 重实现，
专注于单机游戏与逆向工程，不针对多人游戏反作弊。

## 编译

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev \
                 libcapstone-dev zlib1g-dev libdw-dev linux-headers-$(uname -r)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Lua 5.3 已内置在 `third_party/lua`；Keystone 会自动下载并构建。可选功能（`libtcc-dev`、
`libasound2-dev` + `libsoundtouch-dev`、CUDA）会自动探测。

## 运行

```bash
sudo LD_LIBRARY_PATH=build build/cheatengine     # 图形界面
build/cescan --help                              # 命令行扫描器
```

`ptrace` 其他进程需要权限：以 root 运行、把 `/proc/sys/kernel/yama/ptrace_scope` 设为 `0`，
或者让目标进程调用 `PR_SET_PTRACER`。

## 测试，以及 CI 如何把关

- `build/cecore_test` — 主测试套件。**只要有检查输出 `FAILED`，它就会以非零状态退出**，
  CI 会据此判定失败。新增测试时请输出包含 `OK` 或 `FAILED` 的一行（参考现有测试）；
  出现 `FAILED` 即判定构建失败。
- `sudo build/scan_test` — 跨进程扫描/写入（需要 root）。
- `build/gui_debugger_smoke` — 调试器窗口的离屏 Qt 冒烟测试。
- **Sanitizer：** `cmake -B build-asan -DCECORE_SANITIZE=ON && cmake --build
  build-asan --target cecore_test && ./build-asan/cecore_test`。CI 也会跑这套。
  故意触发故障或注入代码的测试由 `#ifndef __SANITIZE_ADDRESS__` 保护。

请尽可能为每个缺陷修复或新功能补一个回归测试。

## 代码结构

```
core/      类型、自动汇编、表达式、作弊表（.CT）
platform/  进程访问（process_vm_readv、ptrace、/proc）、注入器
arch/      汇编器（Keystone）、反汇编器（Capstone）
scanner/   内存扫描器、指针扫描器
symbols/   ELF + DWARF 解析器
debug/     调试会话、断点、跟踪器、代码查找器、LBR
scripting/ Lua 引擎与绑定
analysis/  代码分析、托管运行时探测
gui/       Qt6 窗口
plugins/   变速、audiohack、Vulkan 悬浮层、插件 ABI
```

`docs/DEVELOPMENT.md` 记录当前的路线图、缺口/优先级以及 CLI 功能对齐情况。

## 风格

与周边代码保持一致（注释密度、命名、惯用写法）。注释里不要使用长破折号（em dash）。
保持构建无警告（已开启 `-Wall -Wextra`；目前没有 `-Werror`，但请不要引入新警告）。

## 安全

本工具会读写其他进程的内存，且经常以 root 运行。不要在未经把关的情况下新增能执行代码
或解引用不可信指针的脚本接口（见 `SECURITY.md`；例如 `shellExecute` 默认就是禁用的）。
漏洞请按 `SECURITY.md` 的说明私下上报。
