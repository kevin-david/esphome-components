#include "../../components/ups_hid/tripplite_3024.h"
#include "../../components/ups_hid/state_helpers.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>

using namespace esphome::ups_hid;
using namespace esphome::ups_hid::hid_power;

// Synthetic descriptor based on USB HID/PDC usages and NUT's AVR900U field
// listing. This is NOT a dump from the user's INTERNET350U.
struct Fixture {
  std::vector<uint8_t> bytes{0x05,0x84,0x09,0x04,0xa1,0x01};
  std::map<uint16_t, std::vector<uint8_t>> reports;
  void add(std::initializer_list<uint8_t> b) { bytes.insert(bytes.end(), b); }
  void collection(uint8_t usage) { add({0x05,0x84,0x09,usage,0xa1,0x00}); }
  void end() { add({0xc0}); }
  void field(uint8_t id, uint8_t page, uint8_t usage, uint8_t bits, uint32_t max,
             uint32_t unit = 0, uint8_t exponent = 0, uint8_t main = 0xb1) {
    add({0x85,id,0x05,page,0x09,usage,0x15,0x00,0x27});
    for (int i = 0; i < 4; ++i) bytes.push_back(max >> (8*i));
    add({0x35,0x00,0x45,0x00,0x67});
    for (int i = 0; i < 4; ++i) bytes.push_back(unit >> (8*i));
    add({0x55,exponent,0x75,bits,0x95,0x01,main,0x02});
  }
  Fixture(uint8_t status_id = 0x32, uint8_t capacity_id = 0x34, uint8_t voltage_exponent = 1) {
    collection(0x24); // PowerSummary
    collection(0x1a); // Input
    field(0x30,0x84,0x40,8,255,UNIT_VOLT,7);
    field(0x31,0x84,0x30,16,65535,UNIT_VOLT,voltage_exponent);
    end();
    collection(0x02); // PresentStatus
    add({0x85,status_id,0x75,0x01,0x95,0x07,0xb1,0x03});
    field(status_id,0x84,0x62,1,1); // InternalFailure bit 7
    add({0x75,0x01,0x95,0x01,0xb1,0x03});
    field(status_id,0x84,0x69,1,1); // ShutdownImminent bit 9
    add({0x75,0x01,0x95,0x06,0xb1,0x03});
    field(status_id,0x85,0xd0,1,1); // AC present bit 16
    add({0x75,0x01,0x95,0x01,0xb1,0x03}); // padding bit 17
    for (uint8_t usage : {0x42,0x46,0x44,0x45,0x47,0x4b}) field(status_id,0x85,usage,1,1);
    end();
    field(0x33,0x85,0x2c,8,2);
    field(capacity_id,0x85,0x66,8,100);
    field(0x35,0x85,0x68,16,65535,UNIT_SECOND);
    field(0x38,0x85,0x8c,8,100);
    end();
    collection(0x16); collection(0x1c); // PowerConverter.Output
    field(0x1b,0x84,0x30,16,65535,UNIT_VOLT,voltage_exponent);

    field(0x06,0x84,0x53,16,300,UNIT_VOLT,7);
    end(); end();
    collection(0x18); collection(0x20); // OutletSystem.Outlet, as in the NUT capture
    field(0x1e,0x84,0x35,8,100);
    end(); end(); end();
    reports = {{uint16_t(0x300|status_id),{status_id,0,0,0x19}}, // AC+charging+full
               {0x333,{0x33,2}}, {uint16_t(0x300|capacity_id),{capacity_id,94}},
               {0x335,{0x35,0x42,0x0e}}, {0x338,{0x38,30}}, {0x330,{0x30,120}},
               {0x331,{0x31,0xb7,0x04}}, {0x31b,{0x1b,0xc1,0x04}},
               {0x31e,{0x1e,0}}, {0x306,{0x06,95,0}}};
  }
  bool read(Tripplite3024 &decoder, UpsData &data) {
    data.reset();
    std::set<uint16_t> read_once;
    return decoder.read([&](uint8_t type, uint8_t id, uint8_t *buffer, size_t *length) {
      uint16_t key = (type << 8) | id;
      assert(read_once.insert(key).second); // one request per type+ID per cycle
      auto it = reports.find(key);
      if (it == reports.end()) return false;
      assert(*length >= it->second.size());
      std::copy(it->second.begin(), it->second.end(), buffer);
      *length = it->second.size();
      return true;
    }, data);
  }
};

void test_device_read() {
  Fixture f;
  Tripplite3024 decoder;
  assert(decoder.descriptor.parse(f.bytes.data(), f.bytes.size()));
  UpsData d;
  assert(f.read(decoder,d));
  assert(d.power.status == status::ONLINE && d.hid_status.online() == true);
  assert(std::abs(d.power.input_voltage-120.7f)<0.01f);
  assert(std::abs(d.power.output_voltage-121.7f)<0.01f);
  assert(d.power.input_voltage_nominal == 120 && d.power.input_transfer_low == 95);
  assert(d.power.load_percent == 0 && d.battery.level == 94);
  assert(std::abs(d.battery.runtime_minutes-3650.f/60)<0.01f);
  assert(d.battery.charge_warning == 30 && std::isnan(d.battery.charge_low));
  assert(d.hid_status.low == false && d.hid_status.replace == false);
  f.reports[0x332] = {0x32,0,0,0x20};
  assert(f.read(decoder,d));
  assert(d.power.status == status::ON_BATTERY && d.hid_status.online() == false);
  assert(d.battery.status == "Discharging");
  f.reports.erase(0x332);
  assert(f.read(decoder,d));
  assert(!d.hid_status.online().has_value() && d.power.status.empty());
  f.reports[0x332] = {0x32,0,0}; // short report
  f.reports[0x334] = {0x34,101}; // invalid percentage
  f.reports[0x335] = {0x35,255,255}; // unknown runtime
  assert(f.read(decoder,d));
  assert(!d.hid_status.low && std::isnan(d.battery.level) && std::isnan(d.battery.runtime_minutes));
  f.reports[0x334] = {0x34,50};
  f.reports[0x333] = {0x33,0}; // capacity is mAh, not percent
  assert(f.read(decoder,d) && std::isnan(d.battery.level));
  f.reports.clear();
  assert(!f.read(decoder,d) && !d.hid_status.online());
  assert(std::isnan(d.power.input_voltage) && d.battery.status.empty());
}

void test_input_fallback_and_invalid_identity() {
  Fixture f;
  f.add({0x05,0x84,0x09,0x04,0xa1,0x01});
  f.collection(0x24); f.collection(0x02);
  // Input and Feature can have different layouts despite an equal report ID.
  f.field(0x32,0x85,0xd0,1,1,0,0,0x81);
  f.field(0x32,0x85,0x45,1,1,0,0,0x81);
  f.end(); f.end(); f.end();
  Tripplite3024 decoder;
  assert(decoder.descriptor.parse(f.bytes.data(), f.bytes.size()));
  f.reports.erase(0x332);
  f.reports[0x132]={0x32,1};
  UpsData d;
  assert(f.read(decoder,d) && d.hid_status.online()==true);
  assert(!d.hid_status.charging.has_value());
  f.reports.erase(0x132);
  f.reports[0x332]={0x99,0,0,0x19};
  assert(f.read(decoder,d) && !d.hid_status.online().has_value());
}

void test_units_and_duplicate_paths() {
  Fixture f(0x32,0x34,6); // Correct descriptor: tenths of volts need no vendor factor.
  Tripplite3024 decoder;
  assert(decoder.descriptor.parse(f.bytes.data(), f.bytes.size()));
  UpsData d;
  assert(f.read(decoder,d) && std::abs(d.power.input_voltage-120.7f)<0.01f);
  f.add({5,0x84,9,4,0xa1,1});
  f.collection(0x24); f.collection(0x1a);
  f.field(0x73,0x84,0x30,16,65535,UNIT_VOLT,6);
  f.end(); f.end(); f.end();
  assert(decoder.descriptor.parse(f.bytes.data(), f.bytes.size()));
  assert(f.read(decoder,d) && std::isnan(d.power.input_voltage));
  assert(!std::isnan(d.power.output_voltage));
}

void test_ids_are_not_semantics() {
  Fixture f(0x71,0x72);
  Tripplite3024 decoder;
  assert(decoder.descriptor.parse(f.bytes.data(), f.bytes.size()));
  UpsData d;
  assert(f.read(decoder,d) && d.power.status == status::ONLINE && d.battery.level == 94);
}

void test_captured_reports_without_descriptor_evidence() {
  Fixture f;
  f.reports.clear();
  std::ifstream capture("tests/ups_hid/fixtures/internet350u-mains-reports.txt");
  assert(capture.is_open());
  std::string line;
  while (std::getline(capture,line)) {
    if (line.empty() || line[0]=='#') continue;
    std::istringstream row(line);
    unsigned type, id, byte;
    assert(row >> std::hex >> type >> id);
    std::vector<uint8_t> payload;
    while (row >> std::hex >> byte) { assert(byte<=255); payload.push_back(byte); }
    assert(!payload.empty() && payload[0]==id);
    assert(f.reports.emplace(uint16_t((type<<8)|id),payload).second);
  }
  assert(f.reports.size()==10);
  // The real reports are paired with a SYNTHETIC related-layout descriptor.
  // This is a regression against fabricated status, not target-layout validation.
  Tripplite3024 decoder;
  assert(decoder.descriptor.parse(f.bytes.data(), f.bytes.size()));
  UpsData d;
  assert(f.read(decoder,d));
  assert(std::abs(d.power.input_voltage-120.7f)<0.01f);
  assert(d.power.input_transfer_low==95 && d.power.input_voltage_nominal==120);
  assert(d.power.status.empty() && !d.hid_status.low.has_value());
  assert(std::isnan(d.battery.level) && std::isnan(d.battery.runtime_minutes));
  assert(d.battery.status.empty() && std::isnan(d.power.output_voltage));
}

void test_parser_boundaries() {
  Descriptor d;
  // Unnumbered, two packed fields, signed minimum and negative exponent.
  const uint8_t packed[] = {5,0x84,9,4,0xa1,1,0x15,0xf8,0x25,7,0x75,4,0x95,2,
                            0x19,0x30,0x29,0x31,0x55,0x0f,0x81,2,0xc0};
  assert(d.parse(packed,sizeof(packed)) && d.fields.size()==2);
  const uint8_t raw[] = {0x7e};
  int64_t v;
  assert(d.raw_value(d.fields[0],raw,1,v) && v==-2);
  assert(std::abs(Descriptor::physical_value(d.fields[0],v)+0.2)<1e-8);
  assert(d.raw_value(d.fields[1],raw,1,v) && v==7);
  assert(!d.raw_value(d.fields[0],raw,0,v));
  Field physical=d.fields[1]; physical.physical_min=0; physical.physical_max=150; physical.exponent=0;
  assert(std::abs(Descriptor::physical_value(physical,7)-150)<1e-8);
  Fixture f;
  for (size_t len=0;len<f.bytes.size();++len) {
    // This fixture has one outer collection, closed only at its final byte.
    assert(!d.parse(f.bytes.data(),len) && d.fields.empty());
  }
  const uint8_t malformed[] = {0x05};
  assert(!d.parse(malformed,sizeof(malformed)) && d.fields.empty());
  const uint8_t pop[] = {0xb4};
  assert(!d.parse(pop,sizeof(pop)));
  const uint8_t long_item[] = {0xfe,0xff,0};
  assert(!d.parse(long_item,sizeof(long_item)));
  // Push/pop restores ReportID and offsets remain separate for Input/Feature.
  const uint8_t scopes[] = {5,0x84,9,4,0xa1,1,0x15,0,0x25,100,0x75,8,0x95,1,0x85,1,
      0xa4,0x85,2,9,0x35,0x81,2,0xb4,9,0x35,0xb1,2,0xc0};
  assert(d.parse(scopes,sizeof(scopes)) && d.fields.size()==2);
  assert(d.fields[0].id==2 && d.fields[1].id==1);
  assert(d.fields[0].offset==0 && d.fields[1].offset==0);
  std::mt19937 random(3024);
  for (size_t n=0;n<20000;++n) {
    std::vector<uint8_t> noise(random()%256);
    for (auto &b:noise) b=random();
    d.parse(noise.data(),noise.size());
  }
}

void test_status_unknown() {
  HidStatus s;
  assert(!s.online().has_value() && !s.fault().has_value());
  s.ac_present = true; s.discharging = true;
  assert(!s.online().has_value());
  s.ac_present = false; s.discharging = false;
  assert(!s.online().has_value());
  s.internal_failure=false; s.shutdown_imminent=false;
  assert(s.fault()==false);
  s.shutdown_imminent=true;
  assert(s.fault()==true);
}

void test_published_state_resolution() {
  UpsData d;
  d.hid_status.descriptor_based=true;
  d.power.input_voltage=120.7f;
  // Plausible voltage never supplies AC state for this descriptor path.
  assert(!binary_state(d,"online").has_value() && !binary_state(d,"on_battery").has_value());
  assert(!binary_state(d,"low_battery").has_value() && !binary_state(d,"charging").has_value());
  d.hid_status.ac_present=false; d.hid_status.discharging=true;
  d.hid_status.charging=false; d.hid_status.low=true;
  assert(binary_state(d,"online")==false && binary_state(d,"on_battery")==true);
  assert(binary_state(d,"charging")==false && binary_state(d,"low_battery")==true);
  d.hid_status.low.reset(); d.battery.level=40; d.battery.charge_warning=50;
  assert(!binary_state(d,"low_battery").has_value());
  d.battery.charge_low=20;
  assert(binary_state(d,"low_battery")==false);
  d.hid_status=HidStatus{};
  d.power.status=status::ON_BATTERY;
  assert(binary_state(d,"online")==false && binary_state(d,"on_battery")==true);
}

void test_config_descriptor() {
  const uint8_t config[] = {9,2,34,0,1,1,0,0x80,50, 9,4,3,0,1,3,0,0,0,
      9,0x21,0x11,1,0,1,0x22,0x75,3, 7,5,0x81,3,64,0,255};
  assert(report_descriptor_length(config,sizeof(config),3)==885);
  assert(report_descriptor_length(config,sizeof(config),0)==0);
  assert(report_descriptor_length(config,20,3)==0);
}

int main() {
  test_device_read(); test_ids_are_not_semantics(); test_parser_boundaries();
  test_input_fallback_and_invalid_identity(); test_units_and_duplicate_paths();
  test_captured_reports_without_descriptor_evidence();
  test_status_unknown(); test_config_descriptor(); test_published_state_resolution();
  std::cout << "HID descriptor/decoder regression checks passed\n";
}
