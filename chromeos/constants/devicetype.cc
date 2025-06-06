// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/constants/devicetype.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/system/sys_info.h"

namespace chromeos {

namespace {
constexpr char kDeviceTypeKey[] = "DEVICETYPE";
constexpr char kFormFactor[] = "form-factor";
}  // namespace

std::string GetFormFactor() {
  std::string value;
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kFormFactor)) {
    return command_line->GetSwitchValueASCII(kFormFactor);
  } else if (base::SysInfo::GetLsbReleaseValue(kDeviceTypeKey, &value)) {
    return value;
  }
  return std::string();
}

DeviceType GetDeviceType() {
  std::string value = GetFormFactor();
  // Most devices are Chromebooks, so we will also consider reference boards
  // as Chromebooks.
  if (value == "CHROMEBOOK" || value == "REFERENCE" || value == "CHROMESLATE" ||
      value == "CLAMSHELL" || value == "CONVERTIBLE" || value == "DETACHABLE")
    return DeviceType::kChromebook;
  if (value == "CHROMEBASE")
    return DeviceType::kChromebase;
  if (value == "CHROMEBIT")
    return DeviceType::kChromebit;
  if (value == "CHROMEBOX")
    return DeviceType::kChromebox;
  // Don't log errors for VMs, which are type "OTHER".
  if (value == "OTHER") {
    return DeviceType::kUnknown;
  }
  LOG(ERROR) << "Unknown device type \"" << value << "\"";
  return DeviceType::kUnknown;
}

}  // namespace chromeos
