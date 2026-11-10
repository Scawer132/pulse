# 测试地图

由 `python tools/ci/gen_module_map.py --write` 生成，不要手改。
完整的 target → 模块映射在 [module-map.json](module-map.json) 的 `tests` 字段。

## 怎么跑

```powershell
cmake --build build --target <target>
.\build\<target>.exe        # exit 0 = 通过；失败打印 [FAIL]
PULSE_SELFTEST_CASE=<case> .\build\pulse.exe --selftest
```

仓库没有注册 CTest（`add_test` 为 0），所以按 target 单跑；
发布门禁清单来自 `scripts/build_release_ci.ps1` 的 `$testNames` / `$testTargets`。

## 发布门禁（44）

| target | 主导模块 | 产品源文件 |
| --- | --- | ---: |
| `pulse_ai_broker_test` | app-ai, app-system, app-packs | 11 |
| `pulse_ai_json_test` | common | 1 |
| `pulse_ai_mcp_test` | app-ai, app-extensions, app-system | 12 |
| `pulse_ai_pack_test` | app-system, app-ai, app-packs | 6 |
| `pulse_ai_tools_test` | app-ai, app-files, common | 3 |
| `pulse_app_controllers_test` | app-shell, app-files, app-system | 35 |
| `pulse_archive_integrity_test` | preview-host | 2 |
| `pulse_change_feed_memory_test` | index | 0 |
| `pulse_change_tracking_memory_test` | index | 0 |
| `pulse_change_tracking_polling_test` | app-details | 0 |
| `pulse_change_tracking_test` | index | 1 |
| `pulse_child_edit_test` | ui-render, ui-dialogs | 3 |
| `pulse_column_strip_test` | ui-pane | 1 |
| `pulse_content_progress_ui_test` | app-search, app-system, app-window | 0 |
| `pulse_diagnostics_export_test` | common | 0 |
| `pulse_dialogs_test` | ui-render, ui-dialogs, ui-widgets | 12 |
| `pulse_document_completeness_test` | preview-host | 5 |
| `pulse_entry_sort_key_test` | app-files | 2 |
| `pulse_file_lock_test` | ops, ipc | 5 |
| `pulse_image_preview_integrity_test` | preview-host | 2 |
| `pulse_index_delta_replay_test` | index, fs | 11 |
| `pulse_index_engine_test` | index, fs | 11 |
| `pulse_index_host_stress` | index, ipc | 0 |
| `pulse_link_destination_test` | app-files, app-shell, app-system | 7 |
| `pulse_link_pill_test` | ui-preview | 0 |
| `pulse_localization_test` | common | 0 |
| `pulse_network_locations_test` | app-network | 1 |
| `pulse_operation_presentation_test` | ops | 0 |
| `pulse_ops_test` | ops, ipc | 6 |
| `pulse_playback_controls_test` | ui-preview, common | 3 |
| `pulse_preview_command_test` | common, ui-preview | 0 |
| `pulse_preview_handler_probe` | ui-preview | 1 |
| `pulse_preview_integrity_test` | preview-host | 0 |
| `pulse_preview_test` | preview-host, ui-render | 8 |
| `pulse_rename_ops_test` | ops, app-files, ipc | 6 |
| `pulse_reparse_entry_test` | common, fs, ui-render | 0 |
| `pulse_runtime_log_test` | app-shell, common | 0 |
| `pulse_shell_command_test` | ops | 1 |
| `pulse_shell_icons_test` | ui-widgets | 1 |
| `pulse_udf_listing_test` | preview-host | 2 |
| `pulse_update_installer_test` | app-system | 3 |
| `pulse_update_session_test` | app-system | 1 |
| `pulse_update_test` | app-system | 2 |
| `pulse_usn_packet_queue_test` | index | 0 |

产品源文件为 0 的 target 只编译 `src/bench/` 驱动；模块由 `#include` 推断。

## 按模块

按「该 target 编译了多少本模块的产品源文件」排序；门禁 target 标 †。

| 模块 | 测试数 | 相关 target（前 6） |
| --- | ---: | --- |
| index | 74 | `pulse_search_audit_test`, `pulse_index_capacity_test`, `pulse_index_deep_path_test`, `pulse_index_delta_replay_test`†, `pulse_index_engine_test`†, `pulse_index_exclusion_concurrency_test` … +68 |
| ops | 41 | `pulse_controlled_ops_test`, `pulse_delete_delivery_test`, `pulse_elevated_session_test`, `pulse_external_tasks_test`, `pulse_generated_output_test`, `pulse_ops_conflict_baseline_test` … +35 |
| fs | 51 | `pulse_device_source_test`, `pulse_cloud_roots_test`, `pulse_listing_filter_perf_test`, `pulse_path_normalize_test`, `pulse_recycle_metadata_test`, `pulse_snapshot_store_test` … +45 |
| ipc | 71 | `pulse_file_dialog_remote_multiselect_live_test`, `pulse_legacy_adapter_test`, `pulse_legacy_ansi_adapter_test`, `pulse_legacy_ansi_utf8_test`, `pulse_picker_host_test`, `pulse_file_dialog_activation_ex_test` … +65 |
| preview-host | 39 | `pulse_preview_ack_audit_test`, `pulse_preview_format_audit_test`, `pulse_preview_test`†, `pulse_audit_preview_test`, `pulse_document_completeness_test`†, `pulse_notebook_clipping_test` … +33 |
| shell-bridge | 36 | `pulse_file_dialog_runtime_entry_test`, `pulse_picker_host_test`, `pulse_file_dialog_class_factory_test`, `pulse_picker_bridge_probe`, `pulse_file_dialog_activation_ex_test`, `pulse_file_dialog_iat_test` … +30 |
| shell-host | 10 | `pulse_ctx_parity_probe`, `pulse_isolated_start_pin_test`, `pulse_ctx_handlers_test`, `pulse_app_controllers_test`†, `pulse_host_log_batch_test`, `pulse_packaged_ctx_test` … +4 |
| ai-host | 2 | `pulse_ai_authority_boundary_test`, `pulse_extension_developer_contract_test` |
| common | 103 | `pulse_address_edit_stability_test`, `pulse_ai_assistant_settings_cache_test`, `pulse_ai_authority_boundary_test`, `pulse_ai_broker_test`†, `pulse_ai_chat_store_test`, `pulse_ai_json_test`† … +97 |
| dialog-launcher | 4 | `pulse_dialog_bundle_test`, `pulse_dialog_management_controller_test`, `pulse_managed_launch_profile_test`, `pulse_process_launch_path_test` |
| picker-inprocess | 7 | `pulse_picker_activation_stack_test`, `pulse_picker_inprocess_test`, `pulse_picker_inprocess_v2_input_test`, `pulse_picker_inprocess_v2_test`, `pulse_picker_low_integrity_owner_test`, `pulse_picker_native_options_ui_test` … +1 |
| extension-host | 2 | `pulse_extension_isolation_probe`, `pulse_extension_loopback_evidence_test` |
| app-ai | 8 | `pulse_ai_broker_test`†, `pulse_ai_chat_store_test`, `pulse_ai_mcp_test`†, `pulse_extension_mcp_test`, `pulse_model_thinking_test`, `pulse_ai_assistant_settings_cache_test` … +2 |
| app-picker | 3 | `pulse_picker_appearance_test`, `pulse_picker_bridge_model_test`, `pulse_picker_event_adapter_test` |
| app-tags | 19 | `pulse_shell_tag_com_test`, `pulse_file_press_refresh_test`, `pulse_listing_filter_perf_test`, `pulse_listing_selection_identity_test`, `pulse_selection_caps_test`, `pulse_shell_tag_registry_test` … +13 |
| app-search | 24 | `pulse_global_search_query_test`, `pulse_search_audit_test`, `pulse_advanced_search_query_test`, `pulse_file_press_refresh_test`, `pulse_listing_filter_perf_test`, `pulse_listing_selection_identity_test` … +18 |
| app-network | 18 | `pulse_cloud_roots_test`, `pulse_network_connect_parse_test`, `pulse_app_controllers_test`†, `pulse_explorer_open_queue_test`, `pulse_external_open_wait_test`, `pulse_file_press_refresh_test` … +12 |
| app-shell | 30 | `pulse_app_controllers_test`†, `pulse_explorer_open_queue_test`, `pulse_external_open_wait_test`, `pulse_host_window_test`, `pulse_open_handoff_test`, `pulse_persistence_test` … +24 |
| app-preview | 3 | `pulse_media_props_batch_test`, `pulse_selection_input_test`, `pulse_tray_metadata_audit_test` |
| app-files | 43 | `pulse_app_controllers_test`†, `pulse_batch_rename_present_test`, `pulse_folder_sizes_refactor_test`, `pulse_folder_sizes_experience_test`, `pulse_folder_sizes_navigation_test`, `pulse_folder_sizes_perf_test` … +37 |
| app-navigation | 21 | `pulse_pane_drop_test`, `pulse_session_window_test`, `pulse_app_controllers_test`†, `pulse_persistence_test`, `pulse_file_press_refresh_test`, `pulse_listing_filter_perf_test` … +15 |
| app-settings | 3 | `pulse_app_controllers_test`†, `pulse_persistence_test`, `pulse_settings_transfer_test` |
| app-system | 24 | `pulse_app_controllers_test`†, `pulse_ai_broker_test`†, `pulse_ai_mcp_test`†, `pulse_ai_pack_test`†, `pulse_content_pack_test`, `pulse_extension_mcp_test` … +18 |
| app-window | 20 | `pulse_pane_drop_test`, `pulse_session_window_test`, `pulse_address_edit_stability_test`, `pulse_app_core_alias_test`, `pulse_content_progress_ui_test`†, `pulse_dialog_jump_popup_test` … +14 |
| app-actions | 1 | `pulse_ai_authority_boundary_test` |
| app-extensions | 45 | `pulse_extension_developer_cli_test`, `pulse_extension_development_reload_test`, `pulse_extension_runtime_test`, `pulse_extension_store_trust_test`, `pulse_native_package_runtime_test`, `pulse_extension_native_contract_test` … +39 |
| app-menu | 7 | `pulse_app_controllers_test`†, `pulse_ctx_menu_controller_test`, `pulse_ctx_parity_probe`, `pulse_builtin_menu_coverage_test`, `pulse_audit_persistence_test`, `pulse_context_menu_identity_test` … +1 |
| app-details | 7 | `pulse_change_tracking_polling_test`†, `pulse_file_press_refresh_test`, `pulse_listing_filter_perf_test`, `pulse_listing_selection_identity_test`, `pulse_pane_drop_test`, `pulse_selection_caps_test` … +1 |
| app-toolbar | 1 | `pulse_toolbar_view_order_test` |
| app-packs | 7 | `pulse_ai_broker_test`†, `pulse_ai_mcp_test`†, `pulse_ai_pack_test`†, `pulse_app_controllers_test`†, `pulse_content_pack_test`, `pulse_extension_mcp_test` … +1 |
| ui-ai | 9 | `pulse_extension_control_identity_test`, `pulse_extension_panel_test`, `pulse_extension_tree_catalog_test`, `pulse_extension_tree_test`, `pulse_ai_rich_text_test`, `pulse_batch_rename_present_test` … +3 |
| ui-dialogs | 21 | `pulse_file_picker_operations_test`, `pulse_conflict_dialog_test`, `pulse_delete_dialog_test`, `pulse_dialogs_test`†, `pulse_file_operation_authorization_dialog_test`, `pulse_locked_dialog_test` … +15 |
| ui-preview | 27 | `pulse_preview_buffer_lifetime_test`, `pulse_preview_identity_test`, `pulse_preview_storage_test`, `pulse_document_image_view_test`, `pulse_markdown_image_uri_test`, `pulse_math_test` … +21 |
| ui-pane | 36 | `pulse_preview_buffer_lifetime_test`, `pulse_preview_identity_test`, `pulse_preview_storage_test`, `pulse_preview_view_audit_test`, `pulse_search_highlight_render_test`, `pulse_wall_layout_source_test` … +30 |
| ui-render | 61 | `pulse_conflict_dialog_test`, `pulse_delete_dialog_test`, `pulse_extension_control_identity_test`, `pulse_extension_panel_test`, `pulse_extension_tree_catalog_test`, `pulse_extension_tree_test` … +55 |
| ui-widgets | 29 | `pulse_dialogs_test`†, `pulse_file_picker_model_test`, `pulse_file_save_access_policy_test`, `pulse_file_save_revalidation_test`, `pulse_file_dialog_activation_ex_test`, `pulse_file_dialog_activation_test` … +23 |

## 手动探针（不是 pass/fail 测试）

| target | 模块 |
| --- | --- |
| `pulse_app_launcher_probe` | common |
| `pulse_ctx_parity_probe` | app-menu, shell-host, app-shell |
| `pulse_explorer_source_probe` | app-shell |
| `pulse_extension_isolation_probe` | extension-host |
| `pulse_layered_child_style_probe` | — |
| `pulse_native_empty_properties_probe` | — |
| `pulse_native_file_dialog_events_probe` | — |
| `pulse_native_file_dialog_multisink_probe` | — |
| `pulse_native_open_extension_probe` | — |
| `pulse_native_save_events_probe` | — |
| `pulse_picker_bridge_probe` | shell-bridge, ipc, common, ui-widgets |
| `pulse_preview_handler_probe` | ui-preview |
| `pulse_preview_resource_probe` | common, ipc |

## 运行前要知道

- 部分 target 需要真实服务、私有桌面或 `bench_data/` fixture；先看 target 名与 `src/bench/` 源文件。
- `pulse_preview_test`、`pulse_ops_test` 等会驱动外部宿主；失败时先确认宿主已构建。
- 只有 `PULSE_SELFTEST_CASE` 用例跑在 `pulse.exe` 内，其余都是独立 exe。
