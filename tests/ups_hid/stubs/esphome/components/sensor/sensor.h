#pragma once
#include <cmath>
#include <string>
namespace esphome::sensor {
class Sensor {
 public:
  float state{NAN};
  unsigned publications{0};
  void publish_state(float value) { state = value; ++publications; }
};
}  // namespace esphome::sensor
