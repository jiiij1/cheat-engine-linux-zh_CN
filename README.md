# cheat-engine-linux-zh_CN（修复版 0.8.0）

Linux 原生 Cheat Engine 0.8.0 简体中文版的**修复构建**：修复了在 **Proton / Steam Linux Runtime（pressure-vessel）等沙盒游戏**下，变速工具（Speedhack）点击 *Apply* 时报错、无法注入的问题。

错误现象：

```
注入失败: dlopen returned 18446744073709551104 but libspeedhack.so is not mapped in the target
```

---

## 一、根因（两个独立缺陷）

目标进程跑在 Steam Linux Runtime 容器里（`srt-bwrap` + `pressure-vessel`）：**mount namespace、user namespace 都与宿主机不同，`/usr` 被运行时自己的目录树覆盖，还带 seccomp 过滤器**。

### 缺陷 1：符号解析读错了文件（直接原因）

`ce::resolveProcPath()` 原本用 `access(path, F_OK)` 判断路径，认为“宿主机上存在 = 目标看到的也是这个文件”。但沙盒里：

| | 路径 | 实际文件 |
|---|---|---|
| 宿主机 | `/usr/lib/x86_64-linux-gnu/libc.so.6` | md5 `0d071b…`，`dlopen` 偏移 `0x982d0` |
| 沙盒内的目标 | 同一个路径 | md5 `09f09e…`，`dlopen` 偏移 `0x8f2a0` |

于是 CE 用**宿主 libc 的偏移**去目标里调用 `dlopen`（`base + 0x982d0`），实际落在 `pthread_rwlock_rdlock+0x10` 上，返回垃圾值 `-512`（即 `18446744073709551104`），`libspeedhack.so` 从未被映射。

**修复**：改为比较 `st_dev`/`st_ino`，只要目标看到的不是同一个文件，就走 `/proc/<pid>/root<path>` 读取目标真正在运行的副本。

### 缺陷 2：注入路径在目标命名空间里不存在

传给目标 `dlopen` 的路径由**目标的动态链接器**在自己的 mount namespace 里解析。容器里 `/usr/bin/libspeedhack.so` 是 ENOENT（宿主 `/usr` 被覆盖）。

**修复**：`ce::os::injectLibrary()` 现在按候选路径依次真实尝试，取第一个真正把库映射进目标的路径：

1. 原路径（普通进程直接命中，无额外开销）
2. `/run/host<原路径>`（Steam Runtime 与 Flatpak 都在容器内暴露宿主根）
3. `/proc/<注入方 pid>/root<原路径>`（chroot 类沙盒）

每组候选都用 `/proc/<pid>/maps` 校验是否真的映射成功，失败则换下一个，并在错误信息里带上具体路径。

---

## 二、下载与安装

### 方式 A：安装修复后的 .deb（推荐）

```bash
sudo dpkg -i dist/cheat-engine-linux_0.8.0_zh-CN_amd64.deb
sudo apt-get -f install        # 如有缺失依赖
```

包内 `postinst` 会自动 `setcap cap_sys_ptrace+ep`，无需 sudo 运行 CE。

### 方式 B：只替换核心库（你已经装过 0.8.0 时）

```bash
sudo cp dist/libcecore.so.0.8.0 /usr/lib/x86_64-linux-gnu/libcecore.so.0.8.0
sudo ldconfig
```

> 本次修复只涉及 `libcecore.so.0.8.0`（`injectLibrary` / `resolveProcPath` 都在其中）。
> GUI 可执行文件 `cheatengine`、`cescan`、`libspeedhack.so`、`libcecore_mono_agent.so` **没有任何改动**，因此无需重建。

### 校验

```
d9579b7b634b26d8ff292ae8cc34343d  cheat-engine-linux_0.8.0_zh-CN_amd64.deb
035cff254c65c7ff45f9e5c8eb6142f6  libcecore.so.0.8.0
ed0df84f34b84c5cce97569f3b9b1d65  cheat-engine-linux_0.8.0_sandbox-injection-fix.patch
```

---

## 三、使用（Proton / Steam 游戏）

1. 启动游戏（Steam 里正常启动即可，不必关掉 Steam Linux Runtime）。
2. 打开 Cheat Engine，选择游戏进程（Wine/Proton 游戏显示为 `xxx.exe`）。
3. 菜单 `工具(Tools) → 变速(Speedhack...)`，拖动速度后点 `Apply (inject speedhack + set speed)`。
4. 状态栏显示 `Active, Speed: N.Nx` 即注入成功；速度通过 `/dev/shm/ce_speedhack` 实时生效，可随时拖动。

若干限制（与本次修复无关，属已知设计边界）：

- Wine/Proton 目标的“查找是什么改写了该地址”只监听主线程，且不启用软件页保护（会与 Proton 的 write-watch 冲突）。
- `/dev/shm/ce_speedhack` 需要是**你自己的用户**所有（权限 600）。若曾用 root 运行过 CE，可能留下 root 属主的该文件，普通用户打不开，变速会静默无效——删除它即可：`sudo rm -f /dev/shm/ce_speedhack`（下次注入时插件会自己按你的身份重建）。

---

## 四、源码与补丁

- `src/` —— 已修复的完整源码（0.8.0 zh_CN）。
- `dist/cheat-engine-linux_0.8.0_sandbox-injection-fix.patch` —— 相对官方 0.8.0 源码的补丁，在源码根目录执行：

  ```bash
  patch -p1 < cheat-engine-linux_0.8.0_sandbox-injection-fix.patch
  ```

改动文件：

| 文件 | 说明 |
|---|---|
| `src/platform/linux/ns_attach.cpp` | `resolveProcPath()`：按 `st_dev`/`st_ino` 判断目标实际看到的文件 |
| `src/platform/linux/injector.cpp` | `injectLibrary()`：候选注入路径 + 映射校验；32/64 位两条路径都覆盖 |
| `src/CHANGELOG.md`、`src/docs/CHALLENGING_TARGETS.md` | 缺陷记录与设计说明更正 |

### 构建（只重建核心库）

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DCECORE_BUILD_TESTS=OFF
cmake --build build --target cecore -j$(nproc)
# 产物：build/libcecore.so.0.8.0
```

需要 Capstone、Keystone、libdw（DWARF）、zlib 的开发头文件；Lua 5.3 已随源码 `third_party/lua` 内置。
GUI（`cheatengine`）额外需要 Qt6 Widgets；不装 Qt6 也能单独构建出 `libcecore.so` 与 `cescan`。

### 验证结果（本机构建）

- `SymbolResolver::lookup("dlopen")` 由错误的 `base+0x982d0` 变为目标真实的 `base+0x8f2a0`。
- 原版库对 Proton 进程复现出**完全一致**的报错：`dlopen returned 18446744073709551104 but libspeedhack.so is not mapped in the target`；修复后同一进程 `INJECT OK`，`/proc/<pid>/maps` 中出现 `/run/host/usr/bin/libspeedhack.so`。
- 内置测试 `cecore_test` 全部通过（含 speedhack GOT 注入：161 ms → 16 ms @10x）。

---

## 许可

源码沿用上游 `LICENSE`（见 `src/LICENSE`）。本项目为第三方构建与修复，与上游作者无隶属关系。
