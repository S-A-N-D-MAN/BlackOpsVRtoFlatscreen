// SPDX-License-Identifier: GPL-3.0-only
#include "haptic_result_dispatch.hpp"
#include <openxr/openxr.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
unsigned checks{};
void check(bool value, const char* why) {
    ++checks;
    if (!value) { std::printf("FAIL %s\n", why); std::exit(1); }
}

struct Fixture final {
    wawvr::xr::HapticResultDispatch dispatch;
    unsigned provider_calls{}, checks_called{}, errors{};
    std::vector<wawvr::xr::HapticResultDispatch::Report> reports;
    bool invoke(XrResult result) {
        return dispatch.dispatch(
            [&] { ++provider_calls; return result; },
            [&](XrResult value) {
                ++checks_called;
                if (XR_FAILED(value)) ++errors;
                return XR_SUCCEEDED(value);
            },
            [&](const auto& report) { reports.push_back(report); });
    }
};
}

int main() {
    static_assert(XR_SUCCESS == 0);
    {
        Fixture f;
        check(f.invoke(XR_SUCCESS), "exact XR_SUCCESS remains successful");
        check(f.provider_calls == 1 && f.checks_called == 1 && f.reports.empty(),
              "successful provider is invoked and checked once without anomaly log");
        check(!f.invoke(XR_SESSION_NOT_FOCUSED), "positive not-focused status cannot count as haptic success");
        check(f.provider_calls == 2 && f.checks_called == 2 && f.errors == 0 && f.reports.size() == 1,
              "positive status neither retries provider nor creates generic error");
        check(f.reports[0].result == 8 && f.reports[0].api_succeeded && !f.reports[0].exact_success,
              "diagnostic retains positive numeric status separately from generic API success");
        check(!f.invoke(XR_SESSION_LOSS_PENDING), "positive session-loss status requires exact-result diagnostic");
        check(f.reports.back().result == static_cast<int>(XR_SESSION_LOSS_PENDING) &&
              f.reports.back().api_succeeded && f.reports.back().ordinal == 2,
              "second positive status is not collapsed into boolean success");
        check(!f.invoke(XR_ERROR_RUNTIME_FAILURE), "negative provider error remains unsuccessful");
        check(f.provider_calls == 4 && f.checks_called == 4 && f.errors == 1 &&
              !f.reports.back().api_succeeded && f.reports.back().result == static_cast<int>(XR_ERROR_RUNTIME_FAILURE),
              "negative result preserves generic error check and exact diagnostic without retry");
        check(f.invoke(XR_SUCCESS) && f.provider_calls == 5 && f.reports.size() == 3,
              "fresh successful request works after positive and negative statuses");
    }
    {
        Fixture f;
        for (unsigned i = 0; i < wawvr::xr::HapticResultDispatch::maximum_reports + 8; ++i)
            check(!f.invoke(XR_SESSION_NOT_FOCUSED), "every focus rejection remains false after log budget");
        check(f.reports.size() == 64 && f.reports.back().ordinal == 64 &&
              f.provider_calls == 72 && f.checks_called == 72,
              "finite anomaly budget suppresses only logs, never provider calls or checks");
        check(f.invoke(XR_SUCCESS) && f.provider_calls == 73 && f.checks_called == 73 && f.reports.size() == 64,
              "success after exhausted diagnostic budget remains successful with one provider call");
    }
    std::printf("haptic_result_dispatch_checks=%u failures=0 native_calls=0 xr_calls=0\n", checks);
}
