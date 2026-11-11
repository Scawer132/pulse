// selftest_1b2_local.h — Fork-only --selftest cases kept out of selftest_1b2.cpp.
//
// selftest_1b2.cpp owns the PASS/FAIL counters and the log file; Attach() hands
// them over once, before a case runs, so the cases moved here print the same
// lines and return the same exit codes. The two Check* functions hold the extra
// assertions upstream's TestDetailsMeta and TestDetailsPreviewInteraction make.
#pragma once

#include <cstdio>

namespace pulse::app::selftest_local {

void Attach(int* pass, int* fail, std::FILE* log);

// One of the local case names (details-info-card, details-panel-scroll,
// safe-mode-appearance, tree-plate-damage); -1 when `name` is not ours.
int RunCase(const wchar_t* name);

// Volume-root facts on top of the upstream details-meta assertions.
void CheckVolumeMeta();

// 详情面板预览 off keeps the well and drops every content action in it.
void CheckIconOnlyWell();

} // namespace pulse::app::selftest_local
