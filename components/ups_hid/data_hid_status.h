#pragma once

#include <optional>

namespace esphome {
namespace ups_hid {

struct HidStatus {
  bool descriptor_based{false};
  std::optional<bool> ac_present, charging, discharging, low, full, empty, replace;
  std::optional<bool> internal_failure, shutdown_imminent;

  std::optional<bool> online() const {
    if (ac_present && discharging && *ac_present == *discharging) return {};
    if (ac_present && *ac_present) return true;
    if (discharging && *discharging) return false;
    return {};
  }

  std::optional<bool> fault() const {
    if (internal_failure.value_or(false) || shutdown_imminent.value_or(false)) return true;
    if (internal_failure && shutdown_imminent) return false;
    return {};
  }
};

}  // namespace ups_hid
}  // namespace esphome
