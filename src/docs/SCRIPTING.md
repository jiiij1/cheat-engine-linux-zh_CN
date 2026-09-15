# 用脚本驱动逆向工程工具包

GUI 做的每一件事都只是 cecore 函数的薄封装，因此同样的工作完全可以脚本化：既可以在 GUI 的
**Lua 控制台**里，也可以无界面地通过 `cescan lua <script.lua>` / `cescan lua -e "<code>"` 来做。
本篇介绍在经典 CE 内存/扫描/AA 绑定之上新增的静态分析与 IL2CPP API。文中每一段代码都可在
[`../examples/`](../examples) 中找到可直接运行的版本。

除非给出了文件路径，这些函数都作用于**当前打开的进程**（`openProcess`）；失败时它们返回
`nil, "message"` 而不是抛异常，所以请检查返回值：

```lua
local layout = getIl2CppClassLayout("Player")
if not layout then print("这里没有 IL2CPP"); return end
```

---

## IL2CPP（Unity）剖析

Unity 的 IL2CPP 后端把 C# 编译成原生代码，并把托管元数据剥离到 `GameAssembly` 模块旁边的
`global-metadata.dat` 文件里。cecore 会解析这两者，全程离线完成（不做运行期挂钩，因此对
Proton/Wine 目标同样有效），从而还原类布局、字段偏移、值类型和方法代码地址。支持的元数据
版本为 **27 到 31**。

| 函数 | 返回值 |
|---|---|
| `getIl2CppMetadataPath()` | 定位到的 `global-metadata.dat` 路径，找不到则为 `nil` |
| `getIl2CppClasses([path])` | 离线元数据视图：`{version, decoded, classes = { {image, namespace, name, fullName, fields = {"name",…}} }}`（只有名字，没有偏移） |
| `getIl2CppClassLayout(filter)` | `fullName` 匹配该子串的类的完整布局：字段（偏移/类型）**以及**方法（rva） |
| `getIl2CppObjectLayout(class)` | 单个类的**完整**实例布局：自身字段**加上所有继承字段**（沿基类链上溯），每项都带 `declaringType` |
| `getIl2CppMethods(class)` | 单个类的 `{ {name, rva, address}, ... }` |
| `findIl2CppMethod(class, method)` | 某个方法的实时代码地址，找不到则为 `nil` |
| `getIl2CppStructure(class)` | 把该类表示为一个剖析器 `StructureDefinition` |

`getIl2CppClassLayout` 是最常用的一个：一次调用即可在单趟扫描中解析整个类（或所有匹配某子串的
类）。每个类还带有 `parent`（其基类的完整名，例如 `UnityEngine.Component`）。

```lua
for _, c in ipairs(getIl2CppClassLayout("UnityEngine.Vector3")) do
  print(c.fullName)                                  -- "UnityEngine.Vector3"
  for _, f in ipairs(c.fields) do
    print(string.format("  +0x%X %s", f.offset, f.name))   -- +0x0 x, +0x4 y, +0x8 z
  end
  for _, m in ipairs(c.methods) do
    print(string.format("  method rva=0x%X %s", m.rva, m.name))
  end
end
```

字段项带有 `name`、`offset`（对象内的字节偏移）、`static`、`const` 和 `typeName`，其中
`typeName` 是解析后的托管类型（`System.Single`、`UnityEngine.Vector3`、`System.String`、
`List\`1<System.String>`、`Dictionary\`2<System.String, System.TimeZoneInfo>`、`MyClass[]` 等），
它是从二进制的 `Il2CppType` 表离线读取的，泛型参数会完整展开。方法项带有 `name` 和 `rva`
（GameAssembly 模块内的偏移）；把它加上模块基址就得到实时地址，或者直接用
`findIl2CppMethod`，它会替你完成：

```lua
local addr = findIl2CppMethod("Player", "TakeDamage")   -- 0x7f... 实时地址
```

`getIl2CppClassLayout` 列出的是类的**自身**字段。要对一个实时对象做叠放（overlay），通常需要的
是包含继承字段的**完整**布局，请用 `getIl2CppObjectLayout`：

```lua
local o = getIl2CppObjectLayout("Enemy")     -- Enemy : Character : Entity
for _, f in ipairs(o.fields) do              -- 自身 + 继承，按偏移排序
  local from = f.declaringType ~= o.class and ("  (来自 " .. f.declaringType .. ")") or ""
  print(string.format("  +0x%X %s %s%s", f.offset, f.typeName, f.name, from))
end
```

参见 [`examples/il2cpp_dump.lua`](../examples/il2cpp_dump.lua) 和
[`examples/il2cpp_hook.lua`](../examples/il2cpp_hook.lua)。命令行也能离线完成同样的工作
（无需附加进程），直接读取 `global-metadata.dat`：

```sh
cescan il2cpp <global-metadata.dat> --class Player --fields   # 偏移 + 托管类型
cescan il2cpp <global-metadata.dat> --object Player           # 含继承的完整对象布局
```

### 把类变成可剖析的结构体

`getIl2CppStructure` 把类返回为一个 `StructureDefinition`（与 GUI 结构剖析器使用的同一类型），
于是你可以把它叠放到实时对象地址上，按解析出的值类型遍历实例字段并读取它们。静态字段和常量会
被丢弃（它们不属于实例）。

---

## 从 DWARF 还原原生结构体

这是面向带调试信息（`-g`）编译的 C/C++ 目标的原生对应能力：从 DWARF 还原结构体布局并叠放到
实时内存上。

| 函数 | 返回值 |
|---|---|
| `listDwarfStructs([elfPath])` | `{ "GameState", "Entity", ... }` |
| `getDwarfStructure(name[, elfPath])` | `{name, size, fields = { {name, offset, size, type, typeName} } }` |

`typeName` 是 cecore 的简短值类型名（`int32`、`float`、`pointer` 等），已经穿过
typedef/const/指针/数组 链完成解析，因此可以为每个字段选对 `read*` 函数。参见
[`examples/native_struct.lua`](../examples/native_struct.lua)。

---

## PE 模块（Wine / Proton）

Proton 下的 Windows 游戏会映射真正的 PE 模块（`GameAssembly.dll`、`UnityPlayer.dll` 等）。
cecore 会解析它们的导出表和导入表，于是你可以按名字把某个函数解析为实时地址（理想的挂钩目标），
或者找出某个模块通过哪个 IAT 槽去调用导入的 API。

| 函数 | 返回值 |
|---|---|
| `getModuleExports(nameOrPath)` | `{ {name, ordinal, rva, address, forward?}, ... }` |
| `getModuleImports(nameOrPath)` | `{ {dll, name, ordinal, iatRva, iatAddress}, ... }` |

`address` 是实时 VA（模块基址 + rva）；`iatAddress` 是该导入项 IAT 槽的实时地址。导出的符号也会
并入常规符号解析器，因此 `getAddress("GameAssembly.dll+il2cpp_domain_get")` 这类写法同样可用。

```lua
for _, e in ipairs(getModuleExports("GameAssembly.dll")) do
  if e.name == "il2cpp_runtime_invoke" then print(string.format("0x%X", e.address)) end
end
```

---

## 逆向工程

这些静态辅助函数会读取并反汇编目标，而不会运行它的代码。

| 函数 | 返回值 |
|---|---|
| `createSignature(address[, maxBytes])` | `pattern, unique` — 一个字节数组特征码，自动加通配符并增长直到唯一 |
| `findReferences(address)` | `{ {address, target, type, text}, ... }` — 静态交叉引用（call、jump、lea/mov rip-rel） |
| `enumerateFunctions([module])` | `{ {address, references}, ... }` — 候选函数入口点 |
| `buildCallGraph([module])` | `{ {caller, callee, callSite}, ... }` — 静态调用边 |
| `findReferencedStrings([module])` | `{ {address, target, text}, ... }` — 代码指向的字符串字面量（按界面文字反查代码） |
| `findStatics([module])` | `{ {address, references}, ... }` — 代码访问的全局地址，访问最频繁的排在前 |
| `findCodeCaves([module[, minSize]])` | `{ {address, size}, ... }` — 可用来安放注入代码的填充区 |
| `findAssemblyPattern(asm[, module])` | `{ {address, text}, ... }` — 汇编后指令字节出现的每一处（指令级 AOB） |
| `disassembleRange(address, count)` | `{ {address, size, text, ripTarget?}, ... }` |

```lua
-- 为某个扫描结果生成可重定位的特征码，以便在 ASLR / 重启后依然有效。
local sig, unique = createSignature(0x7f1234560000)
print(sig, unique and "唯一" or "不唯一")

-- 谁调用了这个函数？
for _, r in ipairs(findReferences(funcAddr)) do
  print(string.format("%s @0x%X  %s", r.type, r.address, r.text))
end
```

参见 [`examples/reverse_engineer.lua`](../examples/reverse_engineer.lua)。同一套工具在命令行上
也有，无需编写 Lua：

```sh
cescan analyze <pid> strings              # 被引用的字符串字面量
cescan analyze <pid> statics              # 频繁访问的全局地址
cescan analyze <pid> functions            # 函数入口点
cescan analyze <pid> xrefs 0x<addr>       # 谁引用了某个地址
cescan analyze <pid> asm "call rax"       # 某条汇编指令出现的每一处
cescan analyze <pid> caves 64 --module GameAssembly.dll
```

---

## 运行目标进程里的代码

`executeCode(address[, timeoutMs])` 会在目标进程的一个新线程上运行一个 `cdecl` 约定、以 `ret`
结尾的函数，并等待（默认 5000 ms）其结束，返回 `ok, err`。这相当于 CE 的
`executeCodeEx`/带等待的 `createThread`：配合自动汇编分配并填充一段桩代码，然后调用它。

```lua
-- （在 AA 的 [ENABLE] 块已把 `mycode` 分配为以 ret 结尾的桩代码之后）
local ok, err = executeCode(getAddress("mycode"), 3000)
```

---

## 边界在哪里

这里提供的是**静态**和**读取/执行**类原语：元数据解析、表解析、反汇编、特征码，以及基于线程的
代码执行。实时挂钩、单步与断点属于调试器 API（`debug_*`、`createSimpleHook`），它们与经典 CE
绑定一并记录在别处。这里没有任何功能用于规避反作弊，将来也不会有（超出项目范围，见项目 README）。
