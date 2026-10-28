#pragma once

#include "hid_power_device.h"
#include "data_composite.h"
#include "constants_ups.h"
#include <set>

namespace esphome {
namespace ups_hid {

// Descriptor-based, read-only support. IDs/offsets/widths are never device identity.
class Tripplite3024 {
 public:
  hid_power::Descriptor descriptor;

  // Reader receives the descriptor's exact type, ID, and wire length. It returns
  // false for stalls/timeouts and replaces length with the actual byte count.
  template<typename Reader> bool read(Reader reader, UpsData &data) const {
    using namespace hid_power;
    data.hid_status.descriptor_based = true;
    struct Report { bool ok; std::vector<uint8_t> bytes; };
    std::map<uint16_t, Report> reports;
    auto get = [&](const Field &f, int64_t &raw) {
      uint16_t key = (uint16_t(f.type) << 8) | f.id;
      auto it = reports.find(key);
      if (it == reports.end()) {
        size_t length = descriptor.report_length(f);
        Report report{false, std::vector<uint8_t>(length)};
        report.ok = reader(uint8_t(f.type), f.id, report.bytes.data(), &length);
        if (length != report.bytes.size()) report.ok = false;
        it = reports.emplace(key, std::move(report)).first;
      }
      return it->second.ok && descriptor.raw_value(f, it->second.bytes.data(), it->second.bytes.size(), raw);
    };
    // Prefer the PowerSummary input over converter input, and Feature over Input.
    // Multiple equal-priority fields are ambiguous (for example several outlets).
    auto value = [&](uint32_t usage, uint32_t collection, bool summary_only) -> std::optional<double> {
      for (int summary = 1; summary >= (summary_only ? 1 : 0); --summary) {
        for (auto type : {ReportType::FEATURE, ReportType::INPUT}) {
          const Field *candidate = nullptr;
          size_t matches = 0;
          for (const auto &f : descriptor.fields) {
            if (!f.has(UPS) || f.usage != usage || f.type != type ||
                (collection && !f.has(collection)) || f.has(SUMMARY) != bool(summary)) continue;
            candidate = &f;
            ++matches;
          }
          if (matches > 1) return {};
          if (!candidate) continue;
          const Field &f = *candidate;
          int64_t raw;
          if (!get(f, raw)) continue;
          if (usage == VOLTAGE || usage == CONFIG_VOLTAGE || usage == TRANSFER_LOW || usage == TRANSFER_HIGH) {
            if (f.unit != UNIT_VOLT) continue;
          } else if (usage == RUNTIME) {
            if (f.unit != UNIT_SECOND) continue;
          } else if (f.unit != 0 || f.exponent != 0) continue;
          double result = Descriptor::physical_value(f, raw);
          // NUT tripplite-hid.c smart1500lcdt_scale(): 09ae:3024 firmware
          // declares live voltage 10^5 too small. Restrict the workaround to
          // its known unit/exponent signature; configuration volts are correct.
          if (usage == VOLTAGE && f.unit == UNIT_VOLT && f.exponent == 1 &&
              f.physical_min == f.physical_max) result *= 100000.0;
          return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
        }
      }
      return {};
    };
    bool success = false;
    auto flag = [&](uint32_t usage, std::optional<bool> &dest) {
      auto v = value(usage, 0, true);
      if (v && (*v == 0 || *v == 1)) { dest = *v != 0; success = true; }
    };
    auto &s = data.hid_status;
    flag(AC_PRESENT, s.ac_present);
    flag(CHARGING, s.charging);
    flag(DISCHARGING, s.discharging);
    flag(LOW, s.low);
    flag(FULL, s.full);
    flag(EMPTY, s.empty);
    flag(REPLACE, s.replace);
    flag(INTERNAL_FAILURE, s.internal_failure);
    flag(SHUTDOWN_IMMINENT, s.shutdown_imminent);
    if (auto online = s.online()) data.power.status = *online ? status::ONLINE : status::ON_BATTERY;
    if (s.charging.value_or(false) && !s.discharging.value_or(false)) data.battery.status = "Charging";
    else if (s.discharging.value_or(false) && !s.charging.value_or(false)) data.battery.status = "Discharging";
    else if (s.full.value_or(false)) data.battery.status = "Fully Charged";
    auto append = [&](const char *text) {
      if (!data.battery.status.empty()) data.battery.status += " / ";
      data.battery.status += text;
    };
    if (s.low.value_or(false)) append("Low");
    if (s.empty.value_or(false)) append("Fully Discharged");
    if (s.replace.value_or(false)) append("Replace Battery");

    auto number = [&](uint32_t usage, uint32_t collection, bool summary, float &dest, double max, double divisor = 1.0) {
      auto v = value(usage, collection, summary);
      if (v && *v >= 0 && *v <= max) { dest = *v / divisor; success = true; }
    };
    // CapacityMode 2 means percent. A small mAh/10mWh value is not a percentage.
    auto mode = value(CAPACITY_MODE, 0, true);
    if (mode && *mode == 2) {
      number(CAPACITY, 0, true, data.battery.level, 100);
      number(LOW_LIMIT, 0, true, data.battery.charge_low, 100);
      number(WARNING_LIMIT, 0, true, data.battery.charge_warning, 100);
    }
    number(RUNTIME, 0, true, data.battery.runtime_minutes, 65534, 60);
    auto load = value(LOAD, OUTPUT, false);
    if (!load) load = value(LOAD, OUTLET, false);
    if (load && *load >= 0 && *load <= 100) { data.power.load_percent = *load; success = true; }
    number(VOLTAGE, INPUT, false, data.power.input_voltage, 300);
    number(VOLTAGE, OUTPUT, false, data.power.output_voltage, 300);
    number(CONFIG_VOLTAGE, INPUT, false, data.power.input_voltage_nominal, 300);
    number(TRANSFER_LOW, OUTPUT, false, data.power.input_transfer_low, 300);
    number(TRANSFER_HIGH, OUTPUT, false, data.power.input_transfer_high, 300);
    return success;
  }
};

}  // namespace ups_hid
}  // namespace esphome
