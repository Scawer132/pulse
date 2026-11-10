# src/app 文件索引

`src/app` 有约 670 个文件平铺在同一层（只有 `actions/`、`extensions/` 两个子目录）。
这不是模块划分，而是历史：主程序用 `GLOB_RECURSE` 收源，增加文件不需要改 CMake。
本索引按文件名前缀给出可检索的分组；完整的「功能 → 文件 → 测试」映射见
[`docs/module-map.json`](../docs/module-map.json)。

> 统计于 2026-11-10。新增文件请按下方规则命名；不必为了整理而搬动上游文件。

## 前缀分组（前 24）

| 前缀 | 文件数 | 领域 | 入口示例 |
| --- | ---: | --- | --- |
| `ai_` | 76 | AI 助手、模型、MCP、语音 | `ai_assistant.h`、`ai_agent_actions.cpp`、`ai_tools.cpp` |
| `app_` | 57 | 主窗口运行时、输入、状态 | `app_main.cpp`、`app_input.cpp`、`app_state.h` |
| `window_` | 29 | 多窗口、合并、排列、切换 | `window_factory.cpp`、`window_merge.cpp`、`window_registry.h` |
| `shell_` | 28 | Shell 集成、窗口接管、标签 COM | `shell_integration_registry.cpp`、`shell_window_sync.cpp` |
| `folder_` | 21 | 文件夹大小、视图偏好、别名、统计 | `folder_sizes.cpp`、`folder_view_prefs.cpp` |
| `tag_` | 17 | 标签、规则、视图、ADS | `tag_manager.cpp`、`tag_rules.cpp`、`tag_ads_sync.cpp` |
| `explorer_` | 15 | Explorer 接管与跳转诊断 | `explorer_window_takeover.cpp` |
| `content_` | 14 | 内容搜索、结果页 | `content_search_snapshot.h`、`content_results_ui.cpp` |
| `pane_` / `picker_` / `global_` / `update_` | 各 13 | 分栏、选择器、全局搜索、更新 | `pane_layout.cpp`、`picker_host_mode.h`、`global_search_window.cpp`、`update_installer.cpp` |
| `win_e_` / `search_` | 12 / 11 | Win+E、搜索 | `win_e_hotkey.cpp`、`search_query.h` |
| `tab_` / `batch_` / `settings_` | 各 10 | 标签页、批量重命名、设置 | `tab_controller.cpp`、`batch_rename_plan.cpp`、`settings_controller.h` |
| `entry_` / `network_` | 各 9 | 排序分组、网络位置 | `entry_sort.cpp`、`network_locations.cpp` |
| `startup_` / `details_` | 各 8 | 启动、详情 | `startup_location.h`、`details_info.h` |
| `address_` / `dialog_` / `system_` | 各 7 | 地址栏、对话框跳转、系统信息 | `address_search.cpp`、`dialog_jump.cpp`、`system_info.h` |
| `external_` / `sidebar_` / `tray_` / `change_` / `operation_` / `context_` / `cloud_` / `pack_` | 各 6 | 外部打开、侧栏、托盘、变更跟踪、操作意图、右键菜单、云盘、增强包 | `tray_controller.cpp`、`context_menu_controller.h` |
| `shortcut_` / `workspace_` / `column_` / `toolbar_` / `file_` / `media_` | 各 5 | 快捷键、工作区、列、工具栏、文件点击、媒体属性 | `shortcut_settings.cpp`、`toolbar_tools.h`、`file_click_controller.h` |
| 其余 | — | `drag`、`drop`、`dup`、`git`、`jump`、`lan`、`open`、`recycle`、`zip` … | 用 `rg --files src/app \| rg '^src/app/<prefix>'` |

## 两个已有的子目录

| 目录 | 内容 | 入口 |
| --- | --- | --- |
| `actions/` | 动作清单、注册表、授权、参数、文件步骤 | `actions/action_inventory.h` |
| `extensions/` | 声明式扩展与原生 SDK 适配器 | `extensions/native/module.h` |

## 命名与放置规则

1. **新文件放新子目录**，不要继续加到顶层。主程序用
   `file(GLOB_RECURSE ... CONFIGURE_DEPENDS src/app/*.cpp)`，子目录会被自动收进来，
   不需要改 CMake；测试 target 仍要显式列源文件。
2. **文件名带领域前缀**：`<领域>_<对象>.cpp`。`app_` 前缀不携带信息
   （57 个文件、约 4 万行都叫 `app_*`），新代码不要再用它。
3. **一个文件一个职责**。超过约 1,500 行先考虑拆；不要按行数机械切函数。
4. **不动上游文件的位置**。搬文件会改 include 路径，和每个上游提交冲突；
   用 `docs/architecture.md` 的模块地图解决检索问题，而不是物理搬家。
5. **测试驱动放 `src/bench/`**；只在 app 内部使用的 GUI 测试才留在 `src/app/*_test.cpp`。

## 快速定位

```powershell
rg --files src/app | rg '^src/app/folder_size'
rg -n "ToggleUi|SettingsEffect" src/app src/ui
python -c "import json;d=json.load(open('docs/module-map.json',encoding='utf-8'));print([m['globs'] for m in d['modules'] if m['id']=='app-files'])"
```


