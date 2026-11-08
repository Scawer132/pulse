// app_preview_policy.h - what the 启用预览 master switch turns off.
//
// The master is a gate, not a reset: each child keeps the value it had while
// the gate is closed, so opening it again restores the same behaviour. Callers
// that read file content for a preview go through these predicates; the
// settings page only writes the stored flags.
#pragma once

#include "app_prefs.h"

namespace pulse::app {

// 悬停与空格预览: the hover card and the Space Quick Look. preview_trigger
// still decides which of the two actually runs.
inline bool HoverSpacePreviewOn(const AppPrefs& prefs) {
    return prefs.preview_enabled && prefs.preview_hover_space;
}

// 详情面板预览文件内容. Off: the well keeps its place and shows the file-type
// icon instead, so no preview host is asked for the selection.
inline bool DetailsPreviewContentOn(const AppPrefs& prefs) {
    return prefs.preview_enabled && prefs.preview_details_content;
}

} // namespace pulse::app
