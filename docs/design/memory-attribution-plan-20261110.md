# 内存归因计划（2026-11-10）

目标：在**不改变默认行为**的前提下，把私有工作集里属于本进程堆的部分拆到调用点，找出可以真实削减的分配。本文只覆盖归因与由此得出的单点修复，不含空闲释放工作集、缩略图预算调整等“数字型”改动。

## 一、现状证据

测量口径：任务管理器进程页「内存」列 = 私有工作集（`\Process(pulse)\Working Set - Private`）。

用户的真实配置（`%LOCALAPPDATA%\Pulse-local\app.json`）：预览关闭（`preview_enabled:false`）、自动文件夹大小关闭（`folder_size_auto:false`）、搜索关闭、默认视图 Details。

| 场景 | 私有工作集 | 堆 | 快照缓存 |
|---|---|---|---|
| 41 项 fixture（起点） | 37.5 MB | 16.6 MB | 1 项 / 0.0 MB |
| +bench_data（18 项） | 43.9 | 20.6 | 2 / 0.0 |
| +System32（4,867 项） | 46.1 | 22.4 | 3 / 0.6 |
| +WinSxS（23,917 项） | 55.7 | 30.8 | 4 / 7.3 |
| +Fonts（577 项） | 59.0 | 33.4 | 5 / 7.4 |
| +Program Files / Users / src | 60.8 | 35.0 | 8 / 7.4 |
| 用户当前实例（同一台机） | **61.4** | — | — |

复现结果 60.8 MB 与用户实例 61.4 MB 吻合，说明该路径可以代表用户体感。

已知结论：

- 堆基线 16.6 MB 对应约 6 万次存活分配（32–63 B 约 1.9 万、64–127 B 约 1.8 万、128–255 B 约 1.5 万）。
- 访问 8 个文件夹堆增长 18.4 MB：**快照占 7.4 MB**，其余 **约 11 MB 无归因**；增长与项数弱相关（Fonts 577 项 +2.5 MB、Users 7 项也 +0.3 MB），更像 per-folder 缓存或容器。
- 非堆私有约 26 MB（D3D/D2D 表面、栈、驱动私有页），未拆分。
- 预览与自动文件夹大小已由用户关闭，实测关闭前后增长曲线一致，**不是这两项**。

## 二、要回答的三个问题

1. 启动基线的 16.6 MB 堆由哪些调用点构成？≥1 MB 的调用点是哪些？
2. 每访问一个文件夹固定增加约 1.0–1.3 MB（与项数弱相关）的是什么？是缓存、容器扩容残留，还是每目录的簿记？
3. 非堆私有的 26 MB 中，D2D/D3D 表面占多少？是否存在可复用或可缩小尺寸的目标？

## 三、方法

### 3.1 分配拦截

在探针编译单元里替换全局 `operator new/delete`（含 `nothrow` 与对齐版本），只统计 `new` 路径；CRT 内部 `malloc`（`std::vector` 走 `operator new`，会被覆盖）与 COM 私有堆不在覆盖范围，作为已知盲区记录。

- 每个分配记录：大小、返回地址序列（`CaptureStackBackTrace(0, 12, frames, &hash)`）、次数。
- 聚合键：`(首个非探针返回地址的模块基址, 帧序列 FNV-1a)`；同一调用点只保留一条记录，累计次数/字节/最大块。
- 只对 ≥256 B 的分配抓栈（控制开销），小分配只计入大小直方图；直方图总量与 `HeapWalk` 活块总量必须能对上，否则说明盲区过大，需扩大覆盖（如加 `_malloc` 钩子）。

### 3.2 符号化

输出 `模块名+0xRVA` 与本模块绝对地址，离线用 `build\pulse.pdb` 解析：

```powershell
& 'D:\Windows Kits\10\Debuggers\x64\cdb.exe' -z <dump> -c "ln <模块基址>+<rva>; q"
```

或在探针里用 `dbghelp!SymFromAddr`（PDB 就在 exe 旁边）直接输出函数名。优先用后者，避免二次流程；只把无法解析的行退回 cdb。

### 3.3 采样场景

| 场景 | 操作 | 目的 |
|---|---|---|
| S1 | 空目录启动，静置 3 s | 基线 |
| S2 | 41 项 fixture | 每项开销 |
| S3 | bench_data → System32 → WinSxS → Fonts → Program Files → Users → src，每步静置 1 s | 复现用户路径，逐步 diff |
| S4 | 重复 S3 一次 | 判断线性增长还是缓存上限 |
| S5 | 静置 60 s，每 5 s 采样 | 区分预热与泄漏 |

每个场景在**同一进程**内执行，逐场景取差；不要跨进程比较。

### 3.4 重复性与噪声

本次已观察启动期基线波动 ±5 MB（2.5 s 采样点仍处于收尾阶段）。因此：

- 采样点至少放到启动后 3 s；
- 每个场景跑两遍，取较小值；
- 报告里同时给绝对值和两次差值。

## 四、判读标准

- **≥3 MB 且可解释为浪费**（重复缓存、可共享、可懒加载、明显超配）→ 单点修复，单独提交。
- 1–3 MB：记录，不修，除非同时改善代码结构。
- <1 MB 或无法解释 → 记录后停止。
- 若增长集中在快照之外的 per-folder 表：先量上限（是否随访问目录数线性），再决定收紧或懒加载；不在本轮改默认值。
- 若 D3D 表面占比高：评估复用/减少 render target，单独提交并附带帧时间对比。

**没有找到 ≥3 MB 的明确浪费时，本轮结论为“不改默认行为”，把数据写回本文第五节，结束。**

## 五、结论（执行后填写）

_待填。_

## 六、验证要求

行为不变的改动至少跑：

```powershell
.\build\pulse_frame_pump_test.exe
.\build\pulse_hover_motion_test.exe
```

并给出同一场景修改前后的私有工作集与堆数据（探针同一份、同参数）。涉及导航或快照的改动再加 `pulse_app_controllers_test`（注意其 3 项既有失败：explorer keys、default file manager、toolbar apps，未改动树上同样失败）。

## 七、已踩的坑

1. `GetProcessHeaps(n, buf)` 返回**进程堆总数**，不是写入数；未按 `min(返回值, n)` 截断会遍历到未初始化句柄，导致访问违例（本次崩溃即此）。
2. `HeapLock`/`HeapWalk` 与其它线程竞争：只在隔离实例、停止交互时使用；绝不进产品路径。
3. 探针自身的大块分配（`ComputeWs` 的 vector）不能计入；统计区间外分配。
4. `HeapWalk` 的活块不含 LFH 空闲块；不要把碎片算成泄漏，判定泄漏看 S5 的曲线是否持续上升。
5. `--test-instance` 下 `PULSE_TEST_DATA_DIR` 里的 `app.json` 会覆盖默认设置；要复现用户配置就整份拷贝，手写局部 JSON 会引入其它字段的默认值差异（本次已因此得到一次不可用的对比）。

## 八、探针重建清单

本次会话的探针（`src/app/memory_probe.cpp`、`memory_probe.h`）已在收尾时删除，重建步骤：

1. 新建 `src/app/memory_probe.h`：`int RunMemoryProbe(AppState&, const std::wstring& report);`
2. 新建 `src/app/memory_probe.cpp`：
   - 等待 2.5 s 消息泵后采样；
   - `K32GetProcessMemoryInfo` 取工作集/私有；
   - `VirtualQueryEx` + `QueryWorkingSetEx`（`ShareCount==0` 记私有）拆分 image/mapped/anonymous；
   - `GetProcessHeaps` + `HeapWalk` 统计活块（注意坑 1、2）；
   - 本次新增：`operator new` 拦截与调用点聚合（见 3.1）。
3. 在 `src/app/app_main.cpp` 的 `PULSE_TEST_HOVER_MOTION` 分支之后接一个 `PULSE_TEST_MEMORY_REPORT` 分支（仅 `state.isolatedTest`）。
4. 构建与运行：

```powershell
cmd /c "call scripts\vcvars.bat >nul && cmake --build build --target pulse"
$env:PULSE_TEST_DATA_DIR = '<root>\data'
$env:PULSE_TEST_P1_SCOPE = 'selection'
$env:PULSE_TEST_MEMORY_REPORT = '<root>\report.txt'
.\build\pulse.exe --test-instance <folder>
```

5. 运行前把用户的 `app.json` 整份拷进 `<root>\data\`，否则结果不代表用户配置。

## 九、非目标

- 空闲释放工作集（`SetProcessWorkingSetSize`）：只改读数，不改提交量，且需要帧泵/输入空闲判断，本轮不做。
- 缩略图/预览缓存预算：用户已关闭预览，实测无收益，本轮不动。
- 快照上限默认值：不在本轮改，等归因结果再单独评估。
- D3D 设备生命周期：风险高，需单独立项。
