#!/usr/bin/env python3
"""Generate the module map and testing map for the Pulse repository.

The JSON is the machine-readable interface: every module owns a set of source
globs, every test target is listed once, and both say whether the files are
upstream-owned.  The Markdown files are the human/agent facing summaries.

Usage:
    python tools/ci/gen_module_map.py --write   # regenerate the two outputs
    python tools/ci/gen_module_map.py --check   # fail when they are stale
    python tools/ci/gen_module_map.py --strict  # also fail on unmapped src files

Only files tracked by git are considered.  Ownership is computed against
`main`, which is the upstream mirror in this fork.
"""

from __future__ import annotations

import argparse

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = ROOT / "docs" / "module-map.json"
TESTING_PATH = ROOT / "docs" / "testing-map.md"
CMAKE_FILES = [ROOT / "CMakeLists.txt", *sorted((ROOT / "cmake").glob("*.cmake"))]
CI_SCRIPT = ROOT / "scripts" / "build_release_ci.ps1"

# Ordered: the first matching glob owns the file.  Keep the specific groups
# before the broad ones.  `entry` is the file a reader should open first.
MODULES = [
    dict(
        id="index",
        title="索引引擎与索引宿主",
        keywords=["搜索索引", "MFT", "USN", "增量", "index"],
        globs=["src/index/*"],
        entry="src/index/index_engine.h",
        invariants="索引数据只由 Pulse.Index.exe 写；pulse.exe 通过 ipc 查询。",
    ),
    dict(
        id="ops",
        title="文件操作",
        keywords=["复制", "移动", "删除", "回收站", "传输", "copy", "delete"],
        globs=["src/ops/*"],
        entry="src/ops/ops_manager.h",
        invariants="阻塞 IO 不进 UI 线程；每个任务有取消与代际。",
    ),
    dict(
        id="fs",
        title="文件系统快照",
        keywords=["枚举", "快照", "目录监视", "snapshot", "enum"],
        globs=["src/fs/*"],
        entry="src/fs/fs_enum.h",
        invariants="快照按 generation 失效；UNC 与长路径走统一规范化。",
    ),
    dict(
        id="ipc",
        title="进程间协议",
        keywords=["管道", "协议", "帧", "pipe", "protocol"],
        globs=["src/ipc/*"],
        entry="src/ipc/protocol.h",
        invariants="帧头与 PayloadWriter/Reader 只在这里定义。",
    ),
    dict(
        id="preview-host",
        title="预览宿主与解码",
        keywords=["预览", "解码", "图片", "视频", "PDF", "preview"],
        globs=["src/preview_host/*"],
        entry="src/preview_host/preview_router.h",
        invariants="解码在独立宿主进程；UI 只拿位图或文本。",
    ),
    dict(
        id="shell-bridge",
        title="Shell 桥接",
        keywords=["Shell", "对话框", "系统菜单", "COM"],
        globs=["src/shell_bridge/*"],
        entry="src/shell_bridge/file_dialog_wrapper.cpp",
        invariants="Shell COM 调用不落在 UI 线程的等待上。",
    ),
    dict(
        id="shell-host",
        title="Shell 宿主进程",
        keywords=["Shell", "宿主", "shell host"],
        globs=["src/shell_host/*"],
        entry="src/shell_host/main.cpp",
        invariants="独立进程，崩溃不带走主窗口。",
    ),
    dict(
        id="ai-host",
        title="AI / MCP 宿主",
        keywords=["AI", "MCP", "助手", "模型"],
        globs=["src/ai_host/*"],
        entry="src/ai_host/main.cpp",
        invariants="工具协议与 broker 客户端是唯一入口。",
    ),
    dict(
        id="common",
        title="公共基础",
        keywords=["本地化", "日志", "崩溃", "路径", "JSON", "l10n"],
        globs=["src/common/*"],
        entry="src/common/localization.h",
        invariants="界面文字只经 l10n::Get；新增文案改 catalog.tsv 后重新生成。",
    ),
    dict(
        id="setup",
        title="安装器",
        keywords=["安装", "卸载", "升级", "Installer"],
        globs=["src/setup/*"],
        entry="src/setup/setup_ui.cpp",
        invariants="安装器需要管理员；卸载要移除 PulseIndex 服务。",
    ),
    dict(
        id="dialog-launcher",
        title="对话框启动器",
        keywords=["对话框", "第三方", "dialog"],
        globs=["src/dialog_launcher/*"],
        entry="src/dialog_launcher/main.cpp",
        invariants="对话框运行时与主程序版本隔离。",
    ),
    dict(
        id="picker-inprocess",
        title="进程内文件选择器",
        keywords=["选择器", "打开对话框", "picker"],
        globs=["src/picker_inprocess/*"],
        entry="src/picker_inprocess/picker_ui_api.h",
        invariants="进程内实现与进程外实现共享同一套事件适配。",
    ),
    dict(
        id="start-pin",
        title="开始菜单固定",
        keywords=["开始菜单", "固定", "pin"],
        globs=["src/start_pin/*"],
        entry="src/start_pin/protocol.h",
        invariants="",
    ),
    dict(
        id="hotkey-broker",
        title="全局热键代理",
        keywords=["热键", "hotkey", "Win+E"],
        globs=["src/hotkey_broker/*"],
        entry="src/hotkey_broker/main.cpp",
        invariants="热键注册失败不能阻塞主程序。",
    ),
    dict(
        id="integration-host",
        title="系统集成宿主",
        keywords=["默认文件管理器", "集成"],
        globs=["src/integration_host/*"],
        entry="src/integration_host/main.cpp",
        invariants="",
    ),
    dict(
        id="app-launcher",
        title="启动器",
        keywords=["启动", "launcher"],
        globs=["src/app_launcher/*"],
        entry="src/app_launcher/main.cpp",
        invariants="",
    ),
    dict(
        id="elevated-host",
        title="提权宿主",
        keywords=["管理员", "提权", "elevated"],
        globs=["src/elevated_host/*"],
        entry="src/elevated_host/main.cpp",
        invariants="提权操作与普通进程通过 IPC 通信。",
    ),
    dict(
        id="extension-host",
        title="扩展宿主",
        keywords=["扩展", "插件", "extension"],
        globs=["src/extension_host/*"],
        entry="src/extension_host/isolation.h",
        invariants="扩展不能直接触碰主程序状态。",
    ),
    dict(
        id="bench",
        title="测试与基准",
        keywords=["测试", "基准", "selftest", "test"],
        globs=["src/bench/**"],
        entry="src/bench/preview_test_main.cpp",
        invariants="测试是 [PASS]/[FAIL] 可执行文件，exit 0 为通过；未注册 CTest。",
    ),
    dict(
        id="build-tools",
        title="构建辅助",
        keywords=["构建", "版本", "build"],
        globs=["src/build_tools/*"],
        entry="src/build_tools/pack_catalog_tool.cpp",
        invariants="",
    ),
    # ---- src/app: specific clusters first ----
    dict(
        id="app-ai",
        title="AI 助手与工具",
        keywords=["助手", "AI", "MCP", "对话", "语音"],
        globs=["src/app/ai_*"],
        entry="src/app/ai_assistant.h",
        invariants="AI 执行动作走 actions 与授权；默认拒绝。",
    ),
    dict(
        id="app-picker",
        title="文件选择器应用侧",
        keywords=["选择器", "打开", "保存", "picker"],
        globs=["src/app/picker_*"],
        entry="src/app/picker_host_mode.h",
        invariants="",
    ),
    dict(
        id="app-tags",
        title="标签与标签视图",
        keywords=["标签", "颜色", "tag", "ADS"],
        globs=["src/app/tag_*", "src/app/shell_tag_*"],
        entry="src/app/tag_manager.cpp",
        invariants="标签写入走 ADS 批量队列，不在 UI 线程直写。",
    ),
    dict(
        id="app-search",
        title="搜索与筛选",
        keywords=["搜索", "筛选", "命令面板", "search"],
        globs=[
            "src/app/search_*",
            "src/app/global_search_*",
            "src/app/content_*",
            "src/app/filter_*",
            "src/app/saved_search*",
            "src/app/live_network_search*",
        ],
        entry="src/app/search_query.h",
        invariants="搜索请求带代际；过期结果直接丢弃。",
    ),
    dict(
        id="app-network",
        title="网络位置与设备",
        keywords=["网络", "NAS", "SMB", "云盘", "设备", "network"],
        globs=[
            "src/app/network_*",
            "src/app/lan_*",
            "src/app/cloud_*",
            "src/app/drive_*",
            "src/app/device_registry*",
            "src/app/server_address*",
            "src/app/unc_*",
            "src/app/this_pc_*",
            "src/app/app_network_locations*",
        ],
        entry="src/app/network_locations.h",
        invariants="网络探测有超时与取消；断线不阻塞 UI。",
    ),
    dict(
        id="app-shell",
        title="Windows 集成与窗口接管",
        keywords=["Shell", "右键菜单", "Explorer", "接管", "Git"],
        globs=[
            "src/app/shell_integration*",
            "src/app/shell_window*",
            "src/app/shell_verbs*",
            "src/app/shell_registry*",
            "src/app/shell_cache*",
            "src/app/explorer_*",
            "src/app/open_routing*",
            "src/app/external_open*",
            "src/app/default_file_manager*",
            "src/app/system_ctx*",
            "src/app/git_*",
            "src/app/win_e_*",
            "src/app/open_request_pipe*",
            "src/app/link_resolve*",
            "src/app/single_instance*",
        ],
        entry="src/app/shell_integration_registry.h",
        invariants="接管 Explorer 前要保留原窗口；失败必须可回退。",
    ),
    dict(
        id="app-preview",
        title="预览的应用侧编排",
        keywords=["预览", "Quick Look", "悬停", "媒体", "对比", "preview"],
        globs=[
            "src/app/app_preview*",
            "src/app/app_hover*",
            "src/app/hover_*",
            "src/app/quicklook*",
            "src/app/preview_*",
            "src/app/media_*",
            "src/app/text_diff*",
            "src/app/tray_compare*",
            "src/app/quick_preview*",
        ],
        entry="src/app/app_preview_policy.h",
        invariants="预览宿主未就绪时不得阻塞；开关关闭时不启动宿主。",
    ),
    dict(
        id="app-files",
        title="文件列表操作与选择",
        keywords=["复制", "删除", "重命名", "文件夹大小", "选择", "打包", "拖放"],
        globs=[
            "src/app/app_ops_ui*",
            "src/app/folder_size*",
            "src/app/folder_statistic*",
            "src/app/batch_rename*",
            "src/app/zip_compress*",
            "src/app/drop_staging*",
            "src/app/delete_*",
            "src/app/paste_*",
            "src/app/file_click*",
            "src/app/file_press*",
            "src/app/selection_*",
            "src/app/list_selection_gesture*",
            "src/app/list_type_ahead*",
            "src/app/marquee_*",
            "src/app/entry_*",
            "src/app/listing_*",
            "src/app/group_selection*",
            "src/app/rename_*",
            "src/app/create_rename*",
            "src/app/new_item_name*",
            "src/app/operation_*",
            "src/app/recycle_*",
            "src/app/dup_*",
            "src/app/duplicate_*",
            "src/app/tray_*",
            "src/app/archive_plugin*",
            "src/app/download_source*",
            "src/app/apply_all_sort*",
            "src/app/arrival_select*",
            "src/app/locked_item_prompt*",
            "src/app/folder_move_remap*",
            "src/app/folder_sort_prefs*",
            "src/app/folder_view_prefs*",
            "src/app/folder_alias*",
        ],
        entry="src/app/app_ops_ui.h",
        invariants="文件操作先排队再执行；失败要回到原标签页提示。",
    ),
    dict(
        id="app-navigation",
        title="导航、标签页与会话",
        keywords=["导航", "标签页", "历史", "面包屑", "会话", "分栏"],
        globs=[
            "src/app/places*",
            "src/app/session*",
            "src/app/tab_*",
            "src/app/closed_*",
            "src/app/reopen_closed*",
            "src/app/breadcrumb*",
            "src/app/address_*",
            "src/app/jump_*",
            "src/app/history*",
            "src/app/content_navigation*",
            "src/app/workspace_*",
            "src/app/sidebar_*",
            "src/app/favorites_bar*",
            "src/app/quick_access*",
            "src/app/vertical_tabs*",
            "src/app/group_wheel*",
            "src/app/pane_*",
            "src/app/quick_access*",
            "src/app/navigation_audit*",
            "src/app/split_tree_shape*",
            "src/app/blank_pane_click*",
            "src/app/last_tab_close*",
            "src/app/tab_group_model*",
            "src/app/layout_pane_selection*",
        ],
        entry="src/app/app_navigation.h",
        invariants="导航先切模型再刷视图；关闭标签前保存可恢复状态。",
    ),
    dict(
        id="app-settings",
        title="设置与偏好",
        keywords=["设置", "偏好", "快捷键", "外观", "settings"],
        globs=[
            "src/app/settings_*",
            "src/app/app_prefs*",
            "src/app/shortcut_*",
            "src/app/picker_appearance*",
            "src/app/mtime_hint_prefs*",
            "src/app/quicklook_settings*",
            "src/app/keymap*",
            "src/app/settings_flow*",
            "src/app/settings_interaction*",
        ],
        entry="src/app/settings_controller.h",
        invariants="设置改动经过 SettingsController 的 effect；不要在别处直接写 prefs。",
    ),
    dict(
        id="app-system",
        title="系统信息、更新与生命周期",
        keywords=["更新", "系统信息", "启动", "托盘", "崩溃", "诊断"],
        globs=[
            "src/app/update_*",
            "src/app/system_*",
            "src/app/tray_controller*",
            "src/app/tray_reveal*",
            "src/app/startup_*",
            "src/app/app_updates*",
            "src/app/about_info*",
            "src/app/app_catalog*",
            "src/app/local_profile*",
            "src/app/hang_watch*",
            "src/app/app_diagnostics*",
            "src/app/frame_perf*",
            "src/app/frame_pump*",
            "src/app/double_ctrl*",
            "src/app/window_jump_list*",
            "src/app/window_placement*",
            "src/app/app_window_title*",
            "src/app/bitlocker_unlock*",
            "src/app/hot_entry_*",
            "src/app/host_routing*",
            "src/app/dir_notify_queue*",
            "src/app/io_task_queue*",
            "src/app/snapshot_patch*",
            "src/app/pch_app_state*",
            "src/app/resource.h",
            "src/app/pulse.rc",
        ],
        entry="src/app/update_checker.h",
        invariants="更新下载校验大小与哈希；启动路径不得阻塞首帧。",
    ),
    dict(
        id="app-window",
        title="主窗口、输入与状态",
        keywords=["主窗口", "WndProc", "输入", "状态", "AppState"],
        globs=[
            "src/app/app_main*",
            "src/app/app_input*",
            "src/app/app_runtime*",
            "src/app/app_messages*",
            "src/app/app_model*",
            "src/app/app_core*",
            "src/app/app_state*",
            "src/app/app_internal*",
            "src/app/app_worker*",
            "src/app/app_prompts*",
            "src/app/app_commands*",
            "src/app/app_hosted_edit*",
            "src/app/app_change_tracking*",
            "src/app/app_background*",
            "src/app/window_*",
            "src/app/host_window*",
            "src/app/wndproc_guard*",
            "src/app/dialog_*",
            "src/app/pane_layout*",
            "src/app/partial_redraw*",
            "src/app/ui_timer_pacer*",
            "src/app/animation_input_budget*",
            "src/app/app_column_view*",
            "src/app/app_details_tree*",
            "src/app/app_list_selection*",
            "src/app/app_selection_input*",
            "src/app/app_sidebar_refresh*",
            "src/app/app_toolbar_apps*",
            "src/app/app_wall_hint*",
            "src/app/app_network_page*",
            "src/app/app_navigation*",
            "src/app/app_catalog*",
        ],
        entry="src/app/app_state.h",
        invariants="UI 线程不做阻塞 IO；AppState 是唯一组合根。",
    ),
    dict(
        id="app-actions",
        title="动作注册表与命令",
        keywords=["动作", "命令面板", "action", "快捷键绑定"],
        globs=["src/app/actions/*"],
        entry="src/app/actions/action_inventory.h",
        invariants="每个动作在 inventory 登记；菜单 id 与动作 id 由 check_actions.py 保证一致。",
    ),
    dict(
        id="app-extensions",
        title="扩展应用侧",
        keywords=["扩展", "插件", "extension", "原生面板"],
        globs=["src/app/extensions/**"],
        entry="src/app/extensions/native/command_contributions.h",
        invariants="扩展通过声明式清单与受控 API 接入。",
    ),
    dict(
        id="app-menu",
        title="右键菜单与菜单布局",
        keywords=["右键菜单", "上下文菜单", "菜单", "context menu"],
        globs=[
            "src/app/context_menu*",
            "src/app/ctx_*",
            "src/app/builtin_menu_items*",
            "src/app/details_column_menu*",
        ],
        entry="src/app/context_menu_controller.h",
        invariants="菜单构建与执行分离；Shell 动词在后台预取。",
    ),
    dict(
        id="app-details",
        title="详情面板与列视图",
        keywords=["详情", "属性", "列", "元数据", "details"],
        globs=[
            "src/app/details_*",
            "src/app/column_*",
            "src/app/change_tracking*",
            "src/app/sync_status*",
            "src/app/change_time*",
            "src/app/name_pattern*",
        ],
        entry="src/app/details_info.h",
        invariants="详情只在面板可见时探测；属性读取不阻塞列表。",
    ),
    dict(
        id="app-toolbar",
        title="工具栏与快捷入口",
        keywords=["工具栏", "快捷访问", "按钮", "toolbar"],
        globs=[
            "src/app/toolbar_*",
            "src/app/app_toolbar_apps*",
            "src/app/favorites_bar*",
            "src/app/vertical_tabs*",
            "src/app/group_wheel*",
            "src/app/quick_access*",
        ],
        entry="src/app/toolbar_tools.h",
        invariants="工具栏开关与设置页走同一个 effect。",
    ),
    dict(
        id="app-packs",
        title="增强包安装与状态",
        keywords=["增强包", "安装包", "pack", "组件"],
        globs=["src/app/pack_*", "src/app/content_pack*", "src/app/ai_pack*"],
        entry="src/app/pack_installer.h",
        invariants="包下载校验大小与 SHA-256；失败不改现有安装。",
    ),
    dict(
        id="app-tests",
        title="app 内的测试驱动",
        keywords=["测试驱动", "probe", "flow test"],
        globs=["src/app/*_test.cpp", "src/app/*_test.h", "src/app/*_probe.cpp", "src/app/*_probe.h", "src/app/*_probe_contract.h", "src/app/p1_regression_probe.cpp", "src/app/selftest_1b2*"],
        entry="src/app/selftest_1b2.cpp",
        invariants="测试驱动不进发布 exe（PULSE_TEST_HOOKS 关闭时排除）。",
    ),
    dict(
        id="app-misc",
        title="app 未归类文件",
        keywords=[],
        globs=[],
        entry="",
        invariants="若这里增长，说明命名缺少可检索的领域前缀。",
    ),
    # ---- src/ui: specific clusters first ----
    dict(
        id="ui-ai",
        title="AI 面板与扩展面板",
        keywords=["AI 面板", "扩展", "标签管理面板", "托盘搁架"],
        globs=[
            "src/ui/ai_*",
            "src/ui/extension_*",
            "src/ui/group_wheel*",
            "src/ui/tag_manager_panel*",
            "src/ui/tray_shelf*",
            "src/ui/window_overview*",
            "src/ui/window_switcher*",
            "src/ui/ui_tray_shelf*",
        ],
        entry="src/ui/ai_panel_view.h",
        invariants="面板只读模型，不直接改文件。",
    ),
    dict(
        id="ui-settings",
        title="设置页 UI",
        keywords=["设置页", "开关", "设置布局"],
        globs=["src/ui/ui_settings_*", "src/ui/settings_*"],
        entry="src/ui/ui_settings_view.cpp",
        invariants="布局与绘制共用 settings_page_ids 的顺序。",
    ),
    dict(
        id="ui-dialogs",
        title="对话框与弹层",
        keywords=["对话框", "批量重命名", "选择器", "取色器", "提示条"],
        globs=[
            "src/ui/file_operation_dialog*",
            "src/ui/batch_rename_dialog*",
            "src/ui/folder_picker*",
            "src/ui/advanced_search_dialog*",
            "src/ui/dialog_*",
            "src/ui/edit_host*",
            "src/ui/color_picker*",
            "src/ui/confirm_*",
            "src/ui/notification_toast*",
            "src/ui/picker_*",
            "src/ui/snack_style*",
        ],
        entry="src/ui/file_operation_dialog.h",
        invariants="弹层外壳与阴影走同一套原语（见 snack_style）。",
    ),
    dict(
        id="ui-preview",
        title="预览 UI",
        keywords=["预览", "Quick Look", "视频", "Markdown", "差异"],
        globs=[
            "src/ui/quick_preview*",
            "src/ui/audio_waveform*",
            "src/ui/ffmpeg_playback*",
            "src/ui/native_preview_view*",
            "src/ui/playback_timeline*",
            "src/ui/markdown_image_metadata*",
            "src/ui/ui_hover_preview*",
            "src/ui/hover_preview_content*",
            "src/ui/link_pill*",
            "src/ui/preview_*",
            "src/ui/video_preview*",
            "src/ui/markdown_view*",
            "src/ui/text_diff_window*",
            "src/ui/archive_preview*",
            "src/ui/math_*",
            "src/ui/syntax_highlight*",
        ],
        entry="src/ui/quick_preview_window.cpp",
        invariants="预览帧与主窗口动画解耦；关闭即停宿主。",
    ),
    dict(
        id="ui-pane",
        title="文件列表、命中测试与视图模型",
        keywords=["文件列表", "命中测试", "表格", "树", "视图", "照片墙"],
        globs=[
            "src/ui/ui_pane_list*",
            "src/ui/ui_hit_test*",
            "src/ui/ui_column_strip*",
            "src/ui/ui_address_search*",
            "src/ui/table_view*",
            "src/ui/tree_view*",
            "src/ui/column_*",
            "src/ui/wall_*",
            "src/ui/visible_*",
            "src/ui/ui_view_model*",
            "src/ui/view_layout*",
            "src/ui/view_zoom*",
            "src/ui/pane_*",
        ],
        entry="src/ui/ui_pane_list.cpp",
        invariants="绘制与命中测试必须共用同一份几何（当前仍有重复公式）。",
    ),
    dict(
        id="ui-render",
        title="渲染、主题与 Fluent 控件",
        keywords=["主题", "颜色", "Fluent", "绘制", "图标", "字体"],
        globs=[
            "src/ui/ui_renderer*",
            "src/ui/ui_compositor*",
            "src/ui/window_material*",
            "src/ui/typography*",
            "src/ui/text_*",
            "src/ui/fluent_*",
            "src/ui/ui_fluent_svg*",
            "src/ui/legacy_icons*",
            "src/ui/command_icons*",
            "src/ui/layout*",
            "src/ui/tab_shape*",
            "src/ui/pane_layout_strip*",
            "src/ui/layer_alphas*",
            "src/ui/bloom_*",
            "src/ui/svg_*",
            "src/ui/cover_palette*",
            "src/ui/ui_motion*",
            "src/ui/empty_state*",
            "src/ui/ellipsis_*",
            "src/ui/recent_day*",
            "src/ui/drive_capacity*",
            "src/ui/link_type*",
            "src/ui/preview_format_catalog*",
            "src/ui/known_extensions*",
            "src/ui/name_highlight*",
            "src/ui/panel_metrics*",
            "src/ui/FluentTokens.h",
            "src/ui/tab_*",
            "src/ui/toolbar_layout*",
            "src/ui/responsive_chrome_layout*",
            "src/ui/window_layout_diagram*",
            "src/ui/window_action_icon*",
            "src/ui/d2d_device_binding*",
            "src/ui/surface_capture*",
            "src/ui/render_caches*",
            "src/ui/repaint_targets*",
            "src/ui/high_contrast_state*",
            "src/ui/motion_probe*",
            "src/ui/hover_animation*",
            "src/ui/anchored_popup_position*",
            "src/ui/optional_lumatext*",
            "src/ui/release_note_*",
            "src/ui/lumatext_*",
            "src/ui/window_helpers*",
            "src/ui/drag_*",
            "src/ui/drop_*",
            "src/ui/thumbnail_*",
        ],
        entry="src/ui/FluentTokens.h",
        invariants="颜色与几何 token 只从 Theme 取；不要在绘制里另造一套。",
    ),
    dict(
        id="ui-chrome",
        title="窗口外壳（侧栏、工具栏、标签条）",
        keywords=["侧栏", "工具栏", "标签条", "chrome"],
        globs=[
            "src/ui/ui_sidebar*",
            "src/ui/ui_toolbar*",
            "src/ui/ui_pane_header_icons*",
            "src/ui/ui_pane_zoom_motion*",
            "src/ui/toolbar_picker*",
            "src/ui/toolbar_pin_drag*",
            "src/ui/favorites_bar_layout*",
        ],
        entry="src/ui/ui_toolbar.cpp",
        invariants="外壳只读 ViewModel；悬停重绘走 partial redraw。",
    ),
    dict(
        id="ui-widgets",
        title="可复用控件与小型视图",
        keywords=["控件", "详情面板", "保存", "图标", "高亮", "帮助"],
        globs=[
            "src/ui/address_search_*",
            "src/ui/cloud_swap*",
            "src/ui/connect_server_dialog*",
            "src/ui/details_column_*",
            "src/ui/details_tree_rows*",
            "src/ui/device_crumbs*",
            "src/ui/edit_word_range*",
            "src/ui/file_*",
            "src/ui/folder_thumbnail_cache*",
            "src/ui/git_mark*",
            "src/ui/icon_*",
            "src/ui/item_*",
            "src/ui/mtime_hint*",
            "src/ui/open_with_icons*",
            "src/ui/plain_highlight_lines*",
            "src/ui/quick_access_editor*",
            "src/ui/search_highlights*",
            "src/ui/shell_icons*",
            "src/ui/shortcut_help*",
            "src/ui/shortcuts_view*",
            "src/ui/startup_dialog_info*",
            "src/ui/sysctx_view*",
            "src/ui/ui_details_panel*",
            "src/ui/ui_drive_release*",
            "src/ui/ui_view_morph*",
            "src/ui/operation_flight*",
        ],
        entry="src/ui/ui_details_panel.cpp",
        invariants="控件只做局部状态；文件操作通过 app 的命令进入。",
    ),
    dict(
        id="ui-misc",
        title="ui 未归类文件",
        keywords=[],
        globs=[],
        entry="",
        invariants="若这里增长，说明命名缺少可检索的领域前缀。",
    ),
]

SOURCE_RE = re.compile(r"(?:^|[\s\"']|/)((?:src|third_party)/[A-Za-z0-9_./+-]+\.(?:cpp|c|h|hpp|rc))")
TARGET_RE = re.compile(r"\badd_(executable|library)\(\s*([A-Za-z0-9_]+)")
TARGET_SOURCES_RE = re.compile(r"\btarget_sources\(\s*([A-Za-z0-9_]+)")


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="replace")


def block_after(text: str, open_paren: int) -> str:
    """Return the inside of the parenthesised command starting at open_paren."""
    depth = 1
    i = open_paren + 1
    in_quote = False
    while i < len(text) and depth:
        ch = text[i]
        if ch == '"':
            in_quote = not in_quote
        elif not in_quote:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
        i += 1
    return text[open_paren + 1 : i - 1]


def expand_variables(body: str, variables: dict[str, str]) -> str:
    for _ in range(3):
        replaced = False
        for match in re.finditer(r"\$\{([A-Za-z0-9_]+)\}", body):
            if match.group(1) not in variables:
                continue
            body = body.replace(match.group(0), " " + variables[match.group(1)] + " ")
            replaced = True
        if not replaced:
            break
    return body


def collect_variables(text: str) -> dict[str, str]:
    variables: dict[str, str] = {}
    for match in re.finditer(r"\bset\(\s*([A-Za-z0-9_]+)\s*", text):
        name = match.group(1)
        body = block_after(text, text.index("(", match.start()))
        if len(body) < 4000:
            variables[name] = body
    return variables


def parse_cmake() -> dict[str, dict]:
    targets: dict[str, dict] = {}
    for path in CMAKE_FILES:
        if not path.exists():
            continue
        text = read_text(path)
        variables = collect_variables(text)
        aliases = {
            match.group(1): match.group(2)
            for match in re.finditer(
                r"get_target_property\(\s*([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)\s+SOURCES\)", text
            )
        }
        for match in TARGET_RE.finditer(text):
            kind, name = match.group(1), match.group(2)
            if "$" in name:
                continue
            body = expand_variables(
                block_after(text, text.index("(", match.start())), variables
            )
            for alias, source_target in aliases.items():
                if "${" + alias + "}" in body and source_target in targets:
                    body = body.replace(
                        "${" + alias + "}", " ".join(sorted(targets[source_target]["sources"]))
                    )
            record = targets.setdefault(
                name, {"name": name, "kind": kind, "sources": set(), "file": path.name}
            )
            record["sources"].update(SOURCE_RE.findall(body))
        for match in TARGET_SOURCES_RE.finditer(text):
            name = match.group(1)
            if "$" in name:
                continue
            body = expand_variables(
                block_after(text, text.index("(", match.start())), variables
            )
            record = targets.setdefault(
                name, {"name": name, "kind": "unknown", "sources": set(), "file": path.name}
            )
            record["sources"].update(SOURCE_RE.findall(body))
    return targets


def git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout


def ref_exists(ref: str) -> bool:
    return (
        subprocess.run(
            ["git", "rev-parse", "--verify", "--quiet", ref],
            cwd=ROOT,
            capture_output=True,
        ).returncode
        == 0
    )


def git_sets() -> tuple[set[str], set[str]]:
    base = "main" if ref_exists("main") else "HEAD"
    upstream = set(git("ls-tree", "-r", "--name-only", base).splitlines())
    if ref_exists("upstream/main") and ref_exists("local-main"):
        spec = "upstream/main...local-main"
    else:
        spec = "HEAD"
    local_changed = set(git("diff", "--name-only", spec).splitlines())
    return upstream, local_changed


def source_files() -> list[str]:
    files = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.is_file() and path.suffix in {".cpp", ".c", ".h", ".hpp", ".rc"}:
            files.append(path.relative_to(ROOT).as_posix())
    return files


def owner_of(files: list[str], upstream: set[str], local_changed: set[str]) -> str:
    has_upstream = any(f in upstream for f in files)
    has_local = any(f not in upstream or f in local_changed for f in files)
    if has_local and has_upstream:
        return "mixed"
    return "local" if has_local else "upstream"


def glob_match(path: str, pattern: str) -> bool:
    """Glob with `*` limited to one path component and `**` crossing them."""
    regex = ["^"]
    i = 0
    while i < len(pattern):
        ch = pattern[i]
        if ch == "*" and i + 1 < len(pattern) and pattern[i + 1] == "*":
            regex.append(".*")
            i += 2
            continue
        if ch == "*":
            regex.append("[^/]*")
        elif ch == "?":
            regex.append("[^/]")
        else:
            regex.append(re.escape(ch))
        i += 1
    regex.append("$")
    return re.match("".join(regex), path) is not None


def module_for(path: str) -> str | None:
    for module in MODULES:
        for pattern in module["globs"]:
            if glob_match(path, pattern):
                return module["id"]
    return None


def parse_gate_targets() -> set[str]:
    text = read_text(CI_SCRIPT) if CI_SCRIPT.exists() else ""
    gate: set[str] = set()
    for match in re.finditer(r"\$testNames\s*=\s*@\((.*?)\)", text, re.S):
        gate.update(re.findall(r"'([A-Za-z0-9_]+)'", match.group(1)))
    for match in re.finditer(r"\$testTargets\s*=\s*\((.*?)\)\s*-join", text, re.S):
        gate.update(re.findall(r"'([A-Za-z0-9_]+)'", match.group(1)))
    gate.discard("pulse")
    return gate


def test_kind(name: str) -> str:
    if name.endswith("_test"):
        return "test"
    if name.endswith("_probe"):
        return "probe"
    if name.startswith("pulse_bench") or name.endswith(("_perf", "_stress")):
        return "bench"
    return "other"


def include_modules(source: str, known: set[str]) -> set[str]:
    """Modules named by the local headers a test driver includes.

    Bench drivers often include another bench file (`measure_enum.cpp`), so the
    walk follows bench includes for a few levels and collects the product
    headers they reach.
    """
    modules: set[str] = set()
    queue = [(source, 0)]
    visited: set[str] = set()
    while queue:
        current, depth = queue.pop()
        if current in visited or depth > 3:
            continue
        visited.add(current)
        path = ROOT / current
        if not path.exists():
            continue
        for match in re.finditer(r'#include\s+"([^"]+)"', read_text(path)):
            resolved = (path.parent / match.group(1))
            try:
                relative = resolved.resolve().relative_to(ROOT).as_posix()
            except ValueError:
                continue
            if relative not in known:
                continue
            if relative.startswith("src/bench/"):
                queue.append((relative, depth + 1))
            else:
                module = module_for(relative)
                if module:
                    modules.add(module)
    return modules


def build() -> dict:
    upstream, local_changed = git_sets()
    files = source_files()
    known = set(files)
    targets = parse_cmake()
    gate = parse_gate_targets()

    per_module: dict[str, list[str]] = {m["id"]: [] for m in MODULES}
    unmapped: list[str] = []
    for path in files:
        module = module_for(path)
        if module is None and path.startswith("src/app/"):
            module = "app-misc"
        elif module is None and path.startswith("src/ui/"):
            module = "ui-misc"
        if module is None:
            unmapped.append(path)
        else:
            per_module[module].append(path)

    # Map every target's product sources (ignore test drivers) to modules,
    # ordered by how much of the target each module contributes.
    for target in targets.values():
        product_sources = [s for s in target["sources"] if not s.startswith("src/bench/")]
        counts: dict[str, int] = {}
        for source in product_sources:
            module = module_for(source)
            if module:
                counts[module] = counts.get(module, 0) + 1
        if not counts:
            # Bench-only drivers often include the product headers they test.
            for source in target["sources"]:
                if source.startswith("src/bench/"):
                    for module in include_modules(source, known):
                        counts[module] = counts.get(module, 0) + 1
        target["modules"] = [
            module for module, _ in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))
        ]
        target["module_counts"] = counts
        target["test_kind"] = test_kind(target["name"])
        target["gate"] = target["name"] in gate
        target["product_sources"] = sorted(product_sources)

    module_tests: dict[str, list[tuple[int, str]]] = {m["id"]: [] for m in MODULES}
    for target in targets.values():
        if target["test_kind"] not in {"test", "probe"}:
            continue
        for module, count in target["module_counts"].items():
            module_tests[module].append((count, target["name"]))

    modules_out = []
    for spec in MODULES:
        owned = per_module[spec["id"]]
        if not owned and spec["id"] in {"app-misc", "ui-misc"}:
            # The catch-all modules only exist when something is unclassified.
            continue
        modules_out.append(
            {
                "id": spec["id"],
                "title": spec["title"],
                "keywords": spec["keywords"],
                "globs": spec["globs"],
                "entry": spec["entry"],
                "invariants": spec["invariants"],
                "files": len(owned),
                "owner": owner_of(owned, upstream, local_changed),
                "local_files": sorted(
                    f for f in owned if f not in upstream or f in local_changed
                ),
                "tests": [
                    name
                    for _, name in sorted(module_tests[spec["id"]], key=lambda item: (-item[0], item[1]))
                ],
            }
        )

    tests_out = []
    for target in sorted(targets.values(), key=lambda t: t["name"]):
        if target["test_kind"] not in {"test", "probe", "bench"}:
            continue
        tests_out.append(
            {
                "name": target["name"],
                "kind": target["test_kind"],
                "gate": target["gate"],
                "modules": target["modules"],
                "sources": len(target["sources"]),
                "product_sources": len(target["product_sources"]),
                "owner": owner_of(
                    sorted(target["sources"]), upstream, local_changed
                ),
            }
        )

    return {
        "schema": 1,
        "repo": "pulse",
        "upstream": "jimmgreen/pulse",
        "generated_by": "tools/ci/gen_module_map.py",
        "note": "Ownership is measured against main (upstream mirror). Line counts are not included; see the files.",
        "source_files": len(files),
        "unmapped": unmapped,
        "modules": modules_out,
        "tests": tests_out,
        "gate_tests": sorted(gate),
    }


def render_testing_map(data: dict) -> str:
    gate = [t for t in data["tests"] if t["gate"]]
    gate_names = {t["name"] for t in gate}
    lines = [
        "# 测试地图",
        "",
        "由 `python tools/ci/gen_module_map.py --write` 生成，不要手改。",
        "完整的 target → 模块映射在 [module-map.json](module-map.json) 的 `tests` 字段。",
        "",
        "## 怎么跑",
        "",
        "```powershell",
        "cmake --build build --target <target>",
        ".\\build\\<target>.exe        # exit 0 = 通过；失败打印 [FAIL]",
        "PULSE_SELFTEST_CASE=<case> .\\build\\pulse.exe --selftest",
        "```",
        "",
        "仓库没有注册 CTest（`add_test` 为 0），所以按 target 单跑；",
        "发布门禁清单来自 `scripts/build_release_ci.ps1` 的 `$testNames` / `$testTargets`。",
        "",
        "## 发布门禁（%d）" % len(gate),
        "",
        "| target | 主导模块 | 产品源文件 |",
        "| --- | --- | ---: |",
    ]
    for target in gate:
        modules = ", ".join(target["modules"][:3]) or "—"
        lines.append(f"| `{target['name']}` | {modules} | {target['product_sources']} |")
    lines.append("")
    lines.append("产品源文件为 0 的 target 只编译 `src/bench/` 驱动；模块由 `#include` 推断。")

    lines += [
        "",
        "## 按模块",
        "",
        "按「该 target 编译了多少本模块的产品源文件」排序；门禁 target 标 †。",
        "",
        "| 模块 | 测试数 | 相关 target（前 6） |",
        "| --- | ---: | --- |",
    ]
    for module in data["modules"]:
        tests = module["tests"]
        if not tests:
            continue
        shown = ", ".join(
            f"`{t}`" + ("†" if t in gate_names else "") for t in tests[:6]
        )
        more = f" … +{len(tests) - 6}" if len(tests) > 6 else ""
        lines.append(f"| {module['id']} | {len(tests)} | {shown}{more} |")

    probes = [t for t in data["tests"] if t["kind"] == "probe"]
    if probes:
        lines += [
            "",
            "## 手动探针（不是 pass/fail 测试）",
            "",
            "| target | 模块 |",
            "| --- | --- |",
        ]
        for target in probes:
            lines.append(f"| `{target['name']}` | {', '.join(target['modules']) or '—'} |")

    lines += [
        "",
        "## 运行前要知道",
        "",
        "- 部分 target 需要真实服务、私有桌面或 `bench_data/` fixture；先看 target 名与 `src/bench/` 源文件。",
        "- `pulse_preview_test`、`pulse_ops_test` 等会驱动外部宿主；失败时先确认宿主已构建。",
        "- 只有 `PULSE_SELFTEST_CASE` 用例跑在 `pulse.exe` 内，其余都是独立 exe。",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="regenerate outputs")
    parser.add_argument("--check", action="store_true", help="fail if outputs are stale")
    parser.add_argument("--strict", action="store_true", help="fail on unmapped source files")
    args = parser.parse_args()
    if not args.write and not args.check:
        parser.error("pass --write or --check")

    data = build()
    payload = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    testing = render_testing_map(data)

    if args.strict:
        misc = [m for m in data["modules"] if m["id"].endswith("-misc") and m["files"]]
        empty = [m["id"] for m in data["modules"] if not m["files"]]
        missing = [
            m["entry"] for m in data["modules"] if m["entry"] and not (ROOT / m["entry"]).exists()
        ]
        if data["unmapped"] or misc or empty or missing:
            if data["unmapped"] or misc:
                print("unclassified source files:")
                for path in data["unmapped"]:
                    print("  " + path)
                for module in misc:
                    print("  " + module["id"] + ": " + str(module["files"]) + " file(s)")
                print("add a glob to MODULES in tools/ci/gen_module_map.py")
            if empty:
                print("modules with no files: " + ", ".join(empty))
            for entry in missing:
                print("missing entry: " + entry)
            return 1

    if args.check:
        stale = []
        if not MAP_PATH.exists() or read_text(MAP_PATH) != payload:
            stale.append(MAP_PATH.relative_to(ROOT).as_posix())
        if not TESTING_PATH.exists() or read_text(TESTING_PATH) != testing:
            stale.append(TESTING_PATH.relative_to(ROOT).as_posix())
        if stale:
            print("stale: " + ", ".join(stale))
            print("run: python tools/ci/gen_module_map.py --write")
            return 1
        print(
            "module map OK: %d modules, %d targets, %d gate tests, %d unmapped"
            % (
                len(data["modules"]),
                len(data["tests"]),
                len(data["gate_tests"]),
                len(data["unmapped"]),
            )
        )
        return 0

    MAP_PATH.write_text(payload, encoding="utf-8", newline="\n")
    TESTING_PATH.write_text(testing, encoding="utf-8", newline="\n")
    print(
        "wrote %s and %s (%d modules, %d tests, %d unmapped)"
        % (
            MAP_PATH.relative_to(ROOT).as_posix(),
            TESTING_PATH.relative_to(ROOT).as_posix(),
            len(data["modules"]),
            len(data["tests"]),
            len(data["unmapped"]),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
























