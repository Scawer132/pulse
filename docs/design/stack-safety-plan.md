# WndProc 消息重入与栈溢出：改进计划

> 关联：2026-11-08 栈溢出崩溃。主线程栈 1 MB，崩溃栈上 `WndProcImpl` /
> `WindowOverviewMessage` 成对重复 47 层，约 26 KB/层（`HandleMouseMove` ~12 KB +
> `HitTest` ~17 KB）。
> 状态：**阶段 1、2 已实现**；阶段 3 在采集数据（静态盘点见 §5.1）。

## 1. 结论

这套设计的问题不是"消息处理里做重活"，而是**没有给消息嵌套设上限**：任何一条消息
处理里都可能同步再派发一条消息，每层都保留一个完整的 `WndProcImpl` 帧，最后再叠上
一个 64 KB 的栈缓冲，就会把 1 MB 栈耗尽。

帧级护栏其实已经有了：`t_render_depth` + `FrameScope`（`src/app/app_main.cpp`）挡的是
"帧中开帧"，来自 2026-10-25 那次 IME 模态循环重入。它管不到 `ShowWindow` /
`DestroyWindow` / `SetForegroundWindow` / `TrackPopupMenu` / `DoDragDrop` 这类**消息处理
内部**同步派发——那条路径不经过 `Render`，直接回到 `WndProc`。

所以阶段 1 是把帧级的护栏模式复制到消息级，并让崩溃报告能读出嵌套深度。

## 2. 现状证据

| 指标 | 数值 |
|---|---|
| `WndProcImpl` | `src/app/app_main.cpp` 1196–3463 行，2268 行，131 个 `case WM_` |
| 单层消息处理成本 | ~26 KB（dump 实测），1 MB 栈约 39 层 |
| `SendMessage*`（src/app + src/ui） | 285 处，其中约 100 处在测试/探针/自检 |
| 同步窗口操作（`SetForegroundWindow`/`ShowWindow`/`UpdateWindow`/`DoDragDrop`） | 约 185 处生产调用 |
| 非 bench 的 `[32768]`/`[65536]` 栈数组 | 51 处，其中 10 处测试/探针、9 处 `--shot` 段；生产热路径很少 |
| `NormalizePath` | 已改为 1 KB 栈 + 长路径堆回退（`src/fs/fs_enum.cpp`），有长路径回归用例 |

## 3. 阶段 1：WndProc 深度护栏（已实现）

### 3.1 组成

- 新增 `src/app/wndproc_guard.h` / `.cpp`：
  - `thread_local` 深度计数，`WndProcDepthScope` RAII 自增/自减；
  - `kWndProcDepthLimit = 24`；
  - `CurrentWndProcDepth()` / `PeakWndProcDepth()` / `ResetWndProcDepth()`。
- 新增 `src/bench/wndproc_guard_test.cpp`（目标 `pulse_wndproc_guard_test`）。

### 3.2 接入点

1. `WndProcImpl` 第一行（在 `MessageScope` 之前）：超出上限时记 breadcrumb、发
   `wndproc_depth_dropped` 诊断事件、返回 0，不再往下处理。
2. `WndProc` 的 `__except` 过滤表达式：先记 `(2, msg, depth)`，再 `ResetWndProcDepth()`，
   最后 `ReportFatal`。SEH 捕获不会执行析构，所以必须显式复位计数。
3. 嵌套入口（`depth >= 4`）记 `(3, msg, depth)`：breadcrumb 环只有 64 条，正常路径
   走不到，出事时正好留下最深的一段消息链。

护栏放在 `WndProcImpl` 而不是 `WndProc`，因为快捷键重映射（`RouteUserShortcut`）、
`WM_UI_KICK`、`WM_FRAME_PUMP` 三处会直接递归调用 `WndProcImpl`，同样吃栈。

### 3.3 为什么是 24 层

1 MB ÷ 26 KB ≈ 39 层，但那只是鼠标移动那一层的实测值；`WM_PAINT` 层还要带 `Render`
的帧，按最坏 30 KB/层算：24 层 ≈ 720 KB，给外层和系统调用留下余量；32 层 ≈ 960 KB，
护栏有可能来不及触发。正常路径峰值预期 ≤ 8，24 不会误伤。

`WndProcDepthScope` 析构回到 0 时，若峰值 ≥ 8 且距上次上报 ≥ 5 s，发一条
`wndproc_depth_peak`（带峰值和触发它的顶层消息），用于在真实使用中校准这个常量。

### 3.4 breadcrumb 编码

| category | 含义 | action / result |
|---|---|---|
| 1 | WndProc 捕获异常 | msg / 异常码（保持不变） |
| 2 | 崩溃时的嵌套深度 | msg / depth |
| 3 | 嵌套消息进入（depth ≥ 4） | msg / depth |
| 4 | 护栏触发丢弃 | msg / depth |

`exception_code` 本来就单独写在崩溃 JSON 里，所以 `result` 让给 depth 不丢信息，也
不用改 schema。

### 3.5 验证

**单元测试**：`pulse_wndproc_guard_test` 12 passed, 0 failed（24/25 边界、拒绝后计数
不残留、复位、峰值）。`cmake --build build --target pulse` 通过，无新增告警。

**端到端**（2026-11-08，Release，local profile）：

| 构建 | 运行 | 结果 |
|---|---|---|
| 上限临时改成 1 | `pulse.exe --shot <temp>.png` | 28 条 `wndproc_depth_dropped`，全部 `depth=2`；进程正常走到 `process_stop` |
| 上限 24 | 同上 | 0 条 `wndproc_depth_dropped`、0 条 `wndproc_depth_peak` |
| 改动前基线（`4174309`） | 同上 | 无相关事件，行为一致 |

被真实触发的嵌套消息是 `WM_SETTEXT`、`WM_GETMINMAXINFO`、`WM_WINDOWPOSCHANGING`、
`WM_NCCALCSIZE`、`WM_WINDOWPOSCHANGED`、`WM_SETFOCUS`——都在启动阶段、深度 2。也就是说
正常路径只嵌套到 2 层，24 这个上限有充分余量。

**未覆盖**：

- 崩溃 JSON 里 category 2/3 的深度链（需要在 `WndProcImpl` 里制造一次真实崩溃）。
- `--shot` 模式的退出码不稳定：4 次运行里 3 次以 `0xC0000005` 结束，**包括改动前的
  基线构建**，且都发生在 `process_stop` 之后，与护栏无关。根因见 §8。

### 3.6 已知降级

- 触发上限意味着已经在病态重入里，丢一条消息远好于崩溃；但 `WM_LBUTTONUP` 被丢
  可能留下 `SetCapture`，`WM_NCCALCSIZE` 返回 0 会让那次布局等下一次尺寸变化纠正。
- 上限 1 的实验说明丢消息本身不会立刻让应用崩溃（启动、渲染、退出都完成了），但
  丢掉的正是 `WM_SETTEXT`/`WM_NCCALCSIZE` 这类消息；它们正常只在深度 2 出现，24 层
  的护栏不会碰到，只有在已经失控的链路上才会。
- 护栏不修复触发链，只把"崩溃"降级为"一次可诊断的丢消息"。

## 4. 阶段 2：大栈帧 CI 检查（已实现）

- `tools/ci/check_stack_arrays.py`：只扫 `src/app`、`src/ui`、`src/fs`、`src/ops`，
  定长数组 ≥ 8 KB 判失败；只检查 diff 新增行（老代码自然 grandfather，不需要基线
  文件）；行尾 `// stack-ok: <理由>` 豁免；`--all` 供一次性审计。
- 接入 `.github/workflows/l10n.yml` 新增的 `stack` job：`fetch-depth: 0`，PR 与
  `origin/<base>` 比较，push 与 `github.event.before` 比较。**没有**同步到
  `release.yml`：tag 上没有可靠的 diff 基准，发布构建不该跑 diff 检查。
- 本地验收：临时提交一个 `wchar_t buffer[65536]` 必须失败（实测报了 131072 bytes）；
  给同一行加 `// stack-ok:` 后通过；`--base origin/local-main` 在真实分支上通过。
- `--all` 审计出当前 40 处（含 `std::array` 和 8–16 KB 的），不批量重构：绝大多数在
  index/preview/测试流程，不在 UI 线程的深栈路径上。

## 5. 阶段 3：同步派发治理（进行中：先采集）

### 5.1 静态盘点（2026-11-08）

口径：`src/app` + `src/ui` 生产代码（排除 bench、test、probe、selftest），共 558 处同步
调用点。按"能不能把消息送回本窗口"分四档。

**档 1：模态循环**——循环里会派发任何消息，最可能形成链：

| 位置 | 调用 | 入口 |
|---|---|---|
| `app_commands.cpp:890` | `menu->TrackPopup` | 右键菜单 |
| `tray_controller.cpp:112` | `TrackPopupMenu` | 托盘图标菜单 |
| `ui/drag_drop.cpp:696` | `DoDragDrop` | 拖动越过阈值（WM_MOUSEMOVE） |
| `app_main.cpp:1305` | `MessageBoxW` | 仅 D3D 初始化失败，冷路径，排除 |

**档 2：消息处理里同步改窗口状态**——直接重入 `WndProcImpl`，每处 +1 层：

| 位置 | 调用 | 入口 |
|---|---|---|
| `window_overview_ui.cpp:152` | `SetForegroundWindow` | 窗口总览 Drop（WM_LBUTTONUP） |
| `window_overview_ui.cpp:188/192` | `ShowWindow` | 同上 |
| `window_overview_ui.cpp:190` | `DestroyWindow(gone)` | 同上，会带出另一个窗口的 WM_DESTROY |
| `window_overview_ui.cpp:135` | `RevealWindow` → ShowWindow + SetForegroundWindow | 总览 GoTo |
| `app_input.cpp:1888` | `SendMessage(WM_NCLBUTTONDOWN, HTCAPTION)` | 标题栏拖动，进入系统移动循环 |
| `tab_detach.cpp:663/667` | `SendMessage(WM_SYSCOMMAND / WM_NCLBUTTONDOWN)` | 标签拖出 |
| `window_identity_ui.cpp:154` | `SendMessage(WM_NCLBUTTONDOWN)` | 窗口身份标题拖动 |
| `app_input.cpp:4256/4284` | `SendMessage(WM_SYSCOMMAND)` | 双击标题栏最大化 |
| `window_helpers.cpp:402` | `SendMessage(WM_NCACTIVATE)` | 强制激活重绘 |
| `app_commands.cpp:888/1013` | `UpdateWindow` | 托盘 / 右键菜单弹出前 |
| `app_commands.cpp:5861` | `RedrawWindow(RDW_UPDATENOW)` | 主题切换 |
| `app_hosted_edit.cpp:938/960` | `SendMessage(s.hwnd, …)` | 子编辑框把按键转发回主窗口，**真正嵌套 WndProcImpl** |
| `address_search.cpp:249` | `SendMessage(s.hwnd, WM_KEYDOWN)` | 地址栏 |
| `fluent_menu.cpp:2072` | `SendMessage(hwnd_, WM_MOUSEWHEEL)` | 菜单窗口 |

**档 3：子控件 SendMessage**——约 90 处 `EM_*` / `WM_SETFONT` / `WM_SETREDRAW`，走子控件
自己的 wndproc，不增加本窗口的栈深。

**档 4：跨线程 / 跨进程 SendMessage**——其余约 150 处，会阻塞但不重入本窗口。

结论：能形成"本窗口 → 本窗口"环的只有档 1 + 档 2，共约 17 个点；阶段 3 盯这 17 个，
不是 285 个。

### 5.2 采集

- 护栏的峰值上报阈值从 8 降到 **3**：正常路径实测峰值就是 2，8 永远不会触发；3 抓到的
  正是"菜单 / 拖动 / 强制重绘多出来的那一层"。
- 周期：日常使用 3–7 天，看 `%LOCALAPPDATA%\Pulse\Diagnostics\Runtime\app-*.jsonl`：
  - `wndproc_depth_peak`（外层 msg + 峰值）——按 msg 归类就是候选链的入口；
  - `wndproc_depth_dropped`——出现即说明 24 层被逼近过；
  - 崩溃 JSON 里 category 3 的 msg 序列——真出事时的完整链。
- 判据：按命中频率改档 1 / 档 2 的点，没被数据命中的不改。

### 5.3 预期

如果一周内峰值始终 ≤ 3，说明真实使用里不存在深链，阶段 3 可以从"改代码"降级为
"保留护栏 + 定期看数据"。

## 6. 不做的

- 不拆 `WndProcImpl`（可维护性问题，不是正确性问题）；
- 不改立即模式每帧重建 VM；
- 不把 `SendMessage` 全改 `PostMessage`（语义会变）；
- 不逐个重构冷路径的栈数组。

## 7. 决策记录

- 上限取 24（而非 32）：按最坏 30 KB/层，32 层已接近 1 MB 栈。
- CI 阈值取 8 KB（而非 4 KB）：项目里 1–4 KB 的临时缓冲很常见，4 KB 噪音大；肇事
  值是 32/64 KB。
- CI 只查 diff 新增行：避免维护基线清单，也让存量代码不必一次性整改。

## 8. 附录：`--shot` 退出码 `0xC0000005` 的根因（2026-11-08）

**现象**：`pulse.exe --shot` 约一半的运行以 `0xC0000005` 退出，改动前的基线构建同样复现。

**取证**：临时装 vectored exception handler 记录故障地址，再用 `build/pulse.map` 反查符号：

```
code=0xC0000005 base=0x7FF6ACC80000 addr=pulse+0xB961B2
  thread_start
  SeedShellVerbCache 的 lambda + 0x242
  EnumerateStaticVerbs + 0xE63
  l10n::Get + 0x102        <- AV（该函数范围 0xB960B0..0xB964D0）
```

**链条**：

1. `SeedShellVerbCache` 在启动时起一个 **detached** 线程逐类型枚举 shell 动词
   （`std::thread(...).detach()`，`src/app/app_commands.cpp`）；
2. `EnumerateStaticVerbs` 为菜单标题调用 `l10n::Get`；
3. `l10n` 的字符串表是命名空间作用域的全局对象——`g_cache`（`std::array<std::unordered_map<...>, 3>`）、
   `g_empty`、`g_mutex`（`src/common/localization.cpp`），在 `exit()` 的静态析构阶段被销毁；
4. 退出时只要那个线程还在某次 `EnumerateStaticVerbs` 里（`IsWindow` / generation 检查只在
   两个扩展之间做），它就会访问已析构的 map → AV。

同一个仓库已经为这个坑绕过一次：`src/app/shell_verbs.cpp` 的图标缓存注释写着
"Never destroyed: SeedShellVerbCache's detached thread can still be here while exit()
runs static destructors"，用 `*new` 做成永不销毁。`l10n` 的表没有这样处理。

**为什么没有崩溃报告**：崩溃发生在静态析构阶段，`crash_reporter.cpp` 的
`g_diagnostics_root` / `g_config` 也是全局 `std::wstring`，此时可能已被析构，`WriteReport`
自己再炸一次，所以 `Diagnostics/Crashes` 只留下 lease 的痕迹（目录时间戳变了），没有 .json/.dmp。

**影响**：

- `--shot` / 视觉 CI 约一半概率退出码是 AV；只看 PNG 的脚本不受影响；
- 生产：只在"退出时 seed 线程仍在枚举"的窗口内触发——启动后很快关窗、或 shell 缓存刚失效
  重新 seed 后立刻退出。此时 `HandOffAtExit` 已跑完、会话已保存，用户看不到现象，但进程以
  AV 结束，且这类退出期崩溃不会留下报告。

**修复建议（未实施）**：

1. 把 `l10n` 的 `g_cache` / `g_empty` / `g_mutex` 改成永不销毁（与 `shell_verbs.cpp` 同一写法），
   最小改动，只影响这几个对象的生命周期；
2. 可选：在 `HandOffAtExit` 里 join 这个 seed 线程（停止标志 + 有上限等待），从根上消除
   "线程比进程活得久"这一类问题；
3. 可选附加：把 `crash_reporter.cpp` 的 `g_diagnostics_root` / `g_config` 也做成永不销毁，
   让退出阶段的崩溃能留下报告。
