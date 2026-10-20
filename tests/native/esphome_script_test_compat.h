#pragma once

#include "esphome/core/application.h"

namespace esphome {
class TestSourceGuard {
 public:
  explicit TestSourceGuard(const LogString *) {}
};
}  // namespace esphome

#define ScopedSourceGuard TestSourceGuard
