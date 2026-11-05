// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace wawvr::xr {

// Result handling only. The caller retains ownership, action, and pulse gates.
// OpenXR uses zero for XR_SUCCESS; positive statuses are not proof that a
// haptic request was accepted (XR_SESSION_NOT_FOCUSED produces no event).
class HapticResultDispatch final {
public:
    static constexpr unsigned maximum_reports = 64;
    struct Report final {
        std::int32_t result{};
        unsigned ordinal{};
        bool api_succeeded{}, exact_success{};
    };

    template<class Invoke, class Check, class Diagnose>
    bool dispatch(Invoke&& invoke, Check&& check, Diagnose&& diagnose) {
        const auto result = invoke();
        const bool api_succeeded = check(result);
        const auto numeric = static_cast<std::int32_t>(result);
        const bool exact_success = numeric == 0;
        if (!exact_success && reports_ < maximum_reports) {
            diagnose(Report{numeric, ++reports_, api_succeeded, exact_success});
        }
        return exact_success;
    }

private:
    unsigned reports_{};
};

} // namespace wawvr::xr
