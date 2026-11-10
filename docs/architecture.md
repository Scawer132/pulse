# Pulse 架构与检索地图

这份文档回答两个问题：**改一个功能该从哪几个文件开始**，以及**哪些规则不能违反**。
机器可读的完整模块表在 [`module-map.json`](module-map.json)（由
`tools/ci/gen_module_map.py` 生成），测试入口在 [`testing-map.md`](testing-map.md)。

> 维护方式：模块的增删改先改生成器的 `MODULES`，再跑
> `python tools/ci/gen_module_map.py --write`；提交前跑 `--check --strict`。
> 本文只放稳定的阅读路线与硬规则，不复制模块清单，避免两处漂移。

## 系统形状

```
pulse.exe ── app（状态/导航/命令） ── ui（布局/绘制/命中）
   │            │
   │            ├── fs（快照/枚举/监视）
   │            ├── ops（复制/删除/传输）
   │            ├── index client ── ipc ── Pulse.Index.exe
   │            ├── preview client ── ipc ── Pulse.Preview.exe
   │            └── shell_bridge ── pulse_shell.exe
   └── common（本地化/日志/路径/JSON）
```

独立宿主：`Pulse.Index.exe`、`Pulse.Preview.exe`、`Pulse.Content.exe`、
`Pulse.Document.exe`、`Pulse.AI.exe`、`pulse_shell.exe`、对话框运行时。
宿主崩溃不带走主窗口；主程序通过 `src/ipc/` 的帧协议通信。

## 从任务找文件

| 任务 | 先看这里 | 相关测试 |
| --- | --- | --- |
| 文件列表的绘制、图标、行高 | `src/ui/ui_pane_list.cpp`、`src/ui/ui_renderer.cpp` | `pulse_column_strip_test` |
| 排序、分组、过滤 | `src/app/entry_sort.cpp`、`src/app/entry_group.cpp`、`src/app/folder_view_prefs.cpp` | `pulse_entry_sort_key_test` |
| 点击、框选、焦点 | `src/app/selection_*.cpp`、`src/app/app_input.cpp`、`src/ui/ui_hit_test.cpp` | `pulse_selection_caps_test` |
| 复制/删除/进度/回收站 | `src/ops/ops_manager.cpp`、`src/app/app_ops_ui.cpp`、`src/ui/file_operation_dialog.cpp` | `pulse_ops_test` |
| 预览窗口与解码 | `src/preview_host/`、`src/ui/quick_preview_*.cpp`、`src/app/app_preview_policy.h` | `pulse_preview_test` |
| 设置页与开关 | `src/app/settings_controller.{h,cpp}`、`src/ui/ui_settings_*.cpp`、`src/app/actions/action_settings.cpp` | `pulse_settings_transfer_test` |
| 右键菜单与命令分发 | `src/app/context_menu_controller.cpp`、`src/app/actions/action_registry.cpp`、`src/app/app_commands.cpp` | `pulse_builtin_menu_coverage_test` |
| 标签、颜色、ADS | `src/app/tag_*.cpp`、`src/app/shell_tag_*.cpp`、`src/ui/tag_manager_panel.cpp` | `pulse_tag_rule_roots_test` |
| 搜索（文件名/内容/全局） | `src/index/`、`src/app/search_*.cpp`、`src/app/global_search_window.cpp` | `pulse_index_engine_test` |
| 索引服务与 IPC | `src/index/index_host.cpp`、`src/index/index_engine.cpp`、`src/ipc/` | `pulse_index_test` |
| 标签页、分栏、导航、会话 | `src/app/app_navigation.cpp`、`src/app/session.cpp`、`src/app/tab_*.cpp` | `pulse_session_window_test` |
| AI 助手与 MCP | `src/app/ai_*.cpp`、`src/ai_host/`、`src/app/actions/` | `pulse_ai_tools_test` |
| 主题、颜色、Fluent 控件 | `src/ui/FluentTokens.h`、`src/ui/fluent_components.*`、`src/ui/snack_style.*` | `pulse_dialogs_test` |
| 构建与测试 | `CMakeLists.txt`、`cmake/`、`scripts/build_release_ci.ps1` | `docs/testing-map.md` |

更细的「功能 → 文件 → 测试 → 归属」查 `module-map.json` 的 `modules`：

```jsonc
{
  "id": "app-files",
  "keywords": ["复制", "删除", "文件夹大小", "选择", "打包", "拖放"],
  "globs": ["src/app/app_ops_ui*", "src/app/folder_size*", "..."],
  "entry": "src/app/app_ops_ui.h",
  "files": 100,
  "owner": "mixed",
  "local_files": ["src/app/folder_sizes_ui.cpp"],
  "tests": ["pulse_ops_test", "..."]
}
```

`owner` 的三种取值：`upstream`（全部文件在 `main` 里）、`local`（全部是本地新增）、
`mixed`（含本地新增或被本地改过的上游文件）；`local_files` 直接列出这些文件，\n不需要再跑 `git diff`。

## 硬规则

1. **UI 线程不做阻塞 IO。** 文件系统、预览、Shell COM、索引查询都在后台线程或独立宿主；
   主线程只处理窗口消息与内存状态。
2. **请求带代际，过期结果丢弃。** 导航、搜索、缩略图、预览都遵循同一模式；
   取消不是可选项。
3. **界面文字只来自 `tools/l10n/catalog.tsv`。** 改文案改 catalog，然后跑
   `python tools/l10n/gen_strings.py`；产品代码用 `l10n::Get(...)`，禁止 `l10n::Pick`。
4. **测试是可执行文件，exit 0 为通过。** 仓库没有注册 CTest；按 target 单跑，
   定向验证优先于全量回归。
5. **本地构建与发行版隔离。** `PULSE_LOCAL_PROFILE=ON` 时数据在
   `%LOCALAPPDATA%\Pulse-local`，不要碰发行版的 `PulseIndex` 服务。
6. **扩展与 AI 走受控接口。** 扩展通过清单与适配器接入；AI 动作默认拒绝，
   由 `actions` 的授权层放行。

## fork 边界

`main` 是上游镜像（只快进），`local-main` = `main` + 本地提交。本地改动越集中在
**新文件**，每次 merge 的冲突面越小。

```powershell
git diff --name-only upstream/main...local-main   # 本地碰过的上游文件
git log --oneline upstream/main..local-main       # 本地提交
```

- 新功能优先新增文件（如 `details_info.cpp`、`wndproc_guard.cpp`），上游文件只加最小调用点。
- 不搬动上游文件、不改 include 路径：C++ 的路径是硬编码的，搬迁等于与每个上游提交冲突。
- 结构性改动（拆巨型文件、改分发）从 `main` 切 `feat/*` 发上游 PR，不在本地长期持有。
- 个人工作流与工具链放 `AGENTS.local.md` / `local/`（已被 `.git/info/exclude` 排除）。

## 新增代码放哪里

| 你在做什么 | 放这里 | 命名 |
| --- | --- | --- |
| 新的窗口命令/文件操作 | `src/app/<领域>_*.cpp` | 领域前缀 + 动词 |
| 新的设置项 | 优先改 `settings_controller` 的既有开关表 | 不要新开写入口 |
| 新的可复用控件 | `src/ui/`，先看 `fluent_components` 是否已有 spec | 不要复制绘制代码 |
| 新的 IPC 消息 | `src/ipc/` 定义结构，编码/解码成对出现 | 不要手写两遍字段序 |
| 新的测试 | `src/bench/` 或 `src/app/*_test.cpp` | target 名以 `_test` 结尾 |
| 新模块 | 先加进 `tools/ci/gen_module_map.py` 的 `MODULES` | 跑 `--strict` 确认无未归类 |


