// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEVTOOLS_PROTOCOL_EMULATION_HANDLER_H_
#define CONTENT_BROWSER_DEVTOOLS_PROTOCOL_EMULATION_HANDLER_H_

#include <memory>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "content/browser/devtools/protocol/devtools_domain_handler.h"
#include "content/browser/devtools/protocol/emulation.h"
#include "content/browser/devtools/protocol/protocol.h"
#include "services/device/public/cpp/compute_pressure/buildflags.h"
#include "services/device/public/mojom/pressure_update.mojom-shared.h"
#include "services/device/public/mojom/sensor.mojom-shared.h"
#include "services/device/public/mojom/sensor_provider.mojom-shared.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/common/widget/device_emulation_params.h"

namespace net {
class HttpRequestHeaders;
}  // namespace net

namespace download {
class DownloadUrlParameters;
}  // namespace download

namespace content {

class DevToolsAgentHostImpl;
class RenderFrameHostImpl;
class RenderWidgetHostImpl;
class ScopedVirtualSensorForDevTools;
class ScopedVirtualPressureSourceForDevTools;
class WebContentsImpl;

namespace protocol {

class EmulationHandler : public DevToolsDomainHandler,
                         public Emulation::Backend {
 public:
  EmulationHandler();

  EmulationHandler(const EmulationHandler&) = delete;
  EmulationHandler& operator=(const EmulationHandler&) = delete;

  ~EmulationHandler() override;

  static std::vector<EmulationHandler*> ForAgentHost(
      DevToolsAgentHostImpl* host);

  void Wire(UberDispatcher* dispatcher) override;
  void SetRenderer(int process_host_id,
                   RenderFrameHostImpl* frame_host) override;

  Response Disable() override;

  void GetOverriddenSensorInformation(
      const Emulation::SensorType& type,
      std::unique_ptr<GetOverriddenSensorInformationCallback>) override;
  Response SetSensorOverrideEnabled(
      bool enabled,
      const Emulation::SensorType& type,
      std::unique_ptr<Emulation::SensorMetadata> metadata) override;
  void SetSensorOverrideReadings(
      const Emulation::SensorType& type,
      std::unique_ptr<Emulation::SensorReading> reading,
      std::unique_ptr<SetSensorOverrideReadingsCallback>) override;

  Response SetIdleOverride(bool is_user_active,
                           bool is_screen_unlocked) override;
  Response ClearIdleOverride() override;

  Response SetGeolocationOverride(std::optional<double> latitude,
                                  std::optional<double> longitude,
                                  std::optional<double> accuracy,
                                  std::optional<double> altitude,
                                  std::optional<double> altitude_accuracy,
                                  std::optional<double> heading,
                                  std::optional<double> speed
                            ) override;
  Response ClearGeolocationOverride() override;

  Response SetEmitTouchEventsForMouse(
      bool enabled,
      std::optional<std::string> configuration) override;

  Response SetUserAgentOverride(const std::string& user_agent,
                                std::optional<std::string> accept_language,
                                std::optional<std::string> platform,
                                std::unique_ptr<Emulation::UserAgentMetadata>
                                    ua_metadata_override) override;

  Response CanEmulate(bool* result) override;
  Response SetDeviceMetricsOverride(
      int width,
      int height,
      double device_scale_factor,
      bool mobile,
      std::optional<double> scale,
      std::optional<int> screen_widget,
      std::optional<int> screen_height,
      std::optional<int> position_x,
      std::optional<int> position_y,
      std::optional<bool> dont_set_visible_size,
      std::unique_ptr<Emulation::ScreenOrientation> screen_orientation,
      std::unique_ptr<protocol::Page::Viewport> viewport,
      std::unique_ptr<protocol::Emulation::DisplayFeature> display_feature,
      std::unique_ptr<protocol::Emulation::DevicePosture> device_posture)
      override;
  Response ClearDeviceMetricsOverride() override;

  Response SetVisibleSize(int width, int height) override;

  Response SetFocusEmulationEnabled(bool) override;

  Response SetEmulatedMedia(
      std::optional<std::string> media,
      std::unique_ptr<protocol::Array<protocol::Emulation::MediaFeature>>
          features) override;

  blink::DeviceEmulationParams GetDeviceEmulationParams();
  void SetDeviceEmulationParams(const blink::DeviceEmulationParams& params);

  bool device_emulation_enabled() { return device_emulation_enabled_; }

  // Applies the network request header overrides on `headers`.  If the
  // User-Agent header was overridden, `user_agent_overridden` is set to true;
  // otherwise, it's set to false. If the Accept-Language header was overridden,
  // `accept_language_overridden` is set to true; otherwise, it's set to false.
  void ApplyOverrides(net::HttpRequestHeaders* headers,
                      bool* user_agent_overridden,
                      bool* accept_language_overridden);
  bool ApplyUserAgentMetadataOverrides(
      std::optional<blink::UserAgentMetadata>* override_out);
  void ApplyNetworkOverridesForDownload(
      download::DownloadUrlParameters* parameters);

 private:
  WebContentsImpl* GetWebContents();

  void UpdateTouchEventEmulationState();
  void UpdateDeviceEmulationState();
  void UpdateDeviceEmulationStateForHost(
      RenderWidgetHostImpl* render_widget_host);

  Response SetDevicePostureOverride(
      std::unique_ptr<protocol::Emulation::DevicePosture> posture) override;
  Response ClearDevicePostureOverride() override;
  Response SetDisplayFeaturesOverride(
      std::unique_ptr<protocol::Array<protocol::Emulation::DisplayFeature>>
          features) override;
  Response ClearDisplayFeaturesOverride() override;

  Response SetPressureSourceOverrideEnabled(
      bool enabled,
      const Emulation::PressureSource& source,
      std::unique_ptr<Emulation::PressureMetadata> metadata) override;
  // TODO: Remove obsolete method.
  // `SetPressureStateOverride` will be replaced by SetPressureDataOverride.
  // The method UpdateVirtualPressureSourceState called previously
  // was removed in //content.
  void SetPressureStateOverride(
      const Emulation::PressureSource& source,
      const Emulation::PressureState& state,
      std::unique_ptr<SetPressureStateOverrideCallback>) override;
  void SetPressureDataOverride(
      const Emulation::PressureSource& source,
      const Emulation::PressureState& state,
      std::optional<double> own_contribution_estimate,
      std::unique_ptr<SetPressureDataOverrideCallback>) override;

  bool touch_emulation_enabled_;
  std::string touch_emulation_configuration_;
  bool device_emulation_enabled_;
  bool focus_emulation_enabled_;
  blink::DeviceEmulationParams device_emulation_params_;
  std::string user_agent_;

  // |user_agent_metadata_| is meaningful if |user_agent_| is non-empty.
  // In that case nullopt will disable sending of client hints, and a
  // non-nullopt value will be sent.
  std::optional<blink::UserAgentMetadata> user_agent_metadata_;
  std::string accept_language_;
  // If |prefers_color_scheme_| is either "light" or "dark", it is used to
  // override the "prefers-color-scheme" client hint header, when present.
  std::string prefers_color_scheme_;
  // If |prefers_reduced_motion_| is "reduce", it is used to override the
  // "prefers-reduced-motion" client hint header, when present.
  std::string prefers_reduced_motion_;
  // If |prefers_reduced_transparency_| is "reduce", it is used to override the
  // "prefers-reduced-transparency" client hint header, when present.
  std::string prefers_reduced_transparency_;

  base::flat_map<device::mojom::SensorType,
                 std::unique_ptr<ScopedVirtualSensorForDevTools>>
      sensor_overrides_;

#if BUILDFLAG(ENABLE_COMPUTE_PRESSURE)
  base::flat_map<device::mojom::PressureSource,
                 std::unique_ptr<ScopedVirtualPressureSourceForDevTools>>
      pressure_overrides_;
#endif  // BUILDFLAG(ENABLE_COMPUTE_PRESSURE)

  // True when SetDevicePostureOverride() has been called.
  bool device_posture_emulation_enabled_ = false;

  raw_ptr<RenderFrameHostImpl> host_;

  base::ScopedClosureRunner capture_handle_;
};

}  // namespace protocol
}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_PROTOCOL_EMULATION_HANDLER_H_
