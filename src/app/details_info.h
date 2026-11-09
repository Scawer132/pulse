// details_info.h — What the details panel shows while nothing is selected: the
// current folder, described the same way a selected folder is, or the machine
// at 此电脑. Both only set the panel's subject; the sections draw the facts.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "../ui/ui_view_model.h"

namespace pulse::app {

// Virtual views (搜索 / 最近 / 回收站 / 网络 / 标签) keep the empty-state
// illustration: their "folder" is a query, not a place on disk.
bool DetailsInfoAppliesTo(const std::wstring& path);

// True when `path` is the root of the volume whose root Windows reports as
// `volume_root`: "C:\", its \\?\ spelling, or a mounted volume's mount point.
// The size row shows a disk's 已用 only for such a subject, never for a folder
// inside the volume.
bool DetailsPathIsVolumeRoot(const std::wstring& path, const std::wstring& volume_root);

struct DetailsInfoInput {
    bool this_pc = false;
    std::wstring folder_path;   // current directory for a folder card
};

// Fills @p view with a context card. Callers must not lay out or draw the
// card's action buttons, name-row toggles or its interactive sections: nothing
// is selected, so those would target the folder the tab already shows.
void FillDetailsInfo(const DetailsInfoInput& input, ui::DetailsPanelView& view);

} // namespace pulse::app
