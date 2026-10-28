#pragma once

#include "data_composite.h"
#include "constants_ups.h"
#include <optional>

namespace esphome {
namespace ups_hid {

inline std::optional<bool> online_state(const UpsData &data) {
  if (data.hid_status.descriptor_based) return data.hid_status.online();
  if (data.power.status == status::ONLINE) return true;
  if (data.power.status == status::ON_BATTERY) return false;
  if (data.power.input_voltage_valid()) return true;
  return {};
}

inline std::optional<bool> low_battery_state(const UpsData &data) {
  if (data.hid_status.descriptor_based && data.hid_status.low) return data.hid_status.low;
  if (std::isnan(data.battery.level) || std::isnan(data.battery.charge_low)) return {};
  return data.battery.is_low();
}

inline std::optional<bool> charging_state(const UpsData &data) {
  if (data.hid_status.descriptor_based) return data.hid_status.charging;
  if (!data.battery.is_valid()) return {};
  return data.power.input_voltage_valid() && !std::isnan(data.battery.level) && data.battery.level < 100.0f;
}

inline std::optional<bool> fault_state(const UpsData &data) {
  if (data.hid_status.descriptor_based) return data.hid_status.fault();
  return data.power.is_input_out_of_range() || (!data.power.is_valid() && !data.battery.is_valid());
}

inline std::optional<bool> binary_state(const UpsData &data, const std::string &type) {
  if (type == binary_sensor_type::ONLINE) return online_state(data);
  if (type == binary_sensor_type::ON_BATTERY) {
    auto online = online_state(data);
    return online ? std::optional<bool>(!*online) : std::nullopt;
  }
  if (type == binary_sensor_type::LOW_BATTERY) return low_battery_state(data);
  if (type == binary_sensor_type::CHARGING) return charging_state(data);
  if (type == binary_sensor_type::FAULT) return fault_state(data);
  if (type == binary_sensor_type::OVERLOAD && !std::isnan(data.power.load_percent)) return data.power.is_overloaded();
  return {};
}

}  // namespace ups_hid
}  // namespace esphome
