#include "wndproc_guard.h"

#include "../common/runtime_log.h"

#include <windows.h>

namespace pulse::app {
namespace {

// Normal messages nest 1-3 deep (a menu or a drag over a frame); anything
// deeper is worth a line in the diagnostics log so the peak can be tuned.
constexpr int kPeakReportDepth = 8;
constexpr ULONGLONG kPeakReportMs = 5000;

thread_local int t_depth = 0;
thread_local int t_peak = 0;
thread_local int t_reported_peak = 0;
thread_local unsigned int t_outer_msg = 0;
thread_local ULONGLONG t_last_peak_report = 0;

void ReportPeak() noexcept {
    if (t_peak < kPeakReportDepth || t_peak <= t_reported_peak) return;
    const ULONGLONG now = GetTickCount64();
    if (t_last_peak_report && now - t_last_peak_report < kPeakReportMs) return;
    diagnostics::runtime::Event("wndproc_depth_peak",
        {{"depth", static_cast<uint64_t>(t_peak)}, {"msg", t_outer_msg}});
    t_reported_peak = t_peak;
    t_last_peak_report = now;
}

}  // namespace

WndProcDepthScope::WndProcDepthScope(unsigned int msg) noexcept {
    const int depth = ++t_depth;
    allowed_ = depth <= kWndProcDepthLimit;
    if (depth == 1) t_outer_msg = msg;
    if (depth > t_peak) t_peak = depth;
}

WndProcDepthScope::~WndProcDepthScope() {
    if (--t_depth != 0) return;
    ReportPeak();
}

int CurrentWndProcDepth() noexcept { return t_depth; }

int PeakWndProcDepth() noexcept { return t_peak; }

void ResetWndProcDepth() noexcept {
    t_depth = 0;
    t_peak = 0;
    t_reported_peak = 0;
    t_outer_msg = 0;
    t_last_peak_report = 0;
}

}  // namespace pulse::app
