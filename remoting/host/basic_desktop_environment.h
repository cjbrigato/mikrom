// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_BASIC_DESKTOP_ENVIRONMENT_H_
#define REMOTING_HOST_BASIC_DESKTOP_ENVIRONMENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "remoting/host/base/desktop_environment_options.h"
#include "remoting/host/desktop_environment.h"
#include "remoting/host/desktop_interaction_strategy.h"
#include "remoting/protocol/desktop_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_options.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_types.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace remoting {

class DesktopDisplayInfoMonitor;

// Used to create audio/video capturers and event executor that work with
// the local console.
class BasicDesktopEnvironment : public DesktopEnvironment {
 public:
  BasicDesktopEnvironment(const BasicDesktopEnvironment&) = delete;
  BasicDesktopEnvironment& operator=(const BasicDesktopEnvironment&) = delete;

  ~BasicDesktopEnvironment() override;

  // DesktopEnvironment implementation.
  std::unique_ptr<ActionExecutor> CreateActionExecutor() override;
  std::unique_ptr<AudioCapturer> CreateAudioCapturer() override;
  std::unique_ptr<InputInjector> CreateInputInjector() override;
  std::unique_ptr<ScreenControls> CreateScreenControls() override;
  std::unique_ptr<DesktopCapturer> CreateVideoCapturer(
      webrtc::ScreenId id) override;
  DesktopDisplayInfoMonitor* GetDisplayInfoMonitor() override;
  std::unique_ptr<webrtc::MouseCursorMonitor> CreateMouseCursorMonitor()
      override;
  std::unique_ptr<KeyboardLayoutMonitor> CreateKeyboardLayoutMonitor(
      base::RepeatingCallback<void(const protocol::KeyboardLayout&)> callback)
      override;
  std::unique_ptr<ActiveDisplayMonitor> CreateActiveDisplayMonitor(
      base::RepeatingCallback<void(webrtc::ScreenId)> callback) override;
  std::unique_ptr<FileOperations> CreateFileOperations() override;
  std::unique_ptr<UrlForwarderConfigurator> CreateUrlForwarderConfigurator()
      override;
  std::string GetCapabilities() const override;
  void SetCapabilities(const std::string& capabilities) override;
  std::uint32_t GetDesktopSessionId() const override;
  std::unique_ptr<RemoteWebAuthnStateChangeNotifier>
  CreateRemoteWebAuthnStateChangeNotifier() override;

 protected:
  friend class BasicDesktopEnvironmentFactory;

  BasicDesktopEnvironment(
      scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
      std::unique_ptr<DesktopInteractionStrategy> interaction_strategy,
      base::WeakPtr<ClientSessionControl> client_session_control,
      const DesktopEnvironmentOptions& options);

  scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner() const {
    return caller_task_runner_;
  }

  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner() const {
    return ui_task_runner_;
  }

  webrtc::DesktopCaptureOptions* mutable_desktop_capture_options() {
    return options_.desktop_capture_options();
  }

  const webrtc::DesktopCaptureOptions& desktop_capture_options() const {
    return *options_.desktop_capture_options();
  }

  const DesktopEnvironmentOptions& desktop_environment_options() const {
    return options_;
  }

  DesktopInteractionStrategy& interaction_strategy() {
    return *interaction_strategy_;
  }

 private:
  // Task runner on which methods of DesktopEnvironment interface should be
  // called.
  scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner_;

  // Used to run UI code.
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;

  std::unique_ptr<DesktopInteractionStrategy> interaction_strategy_;

  // Used to send messages directly to the client session.
  base::WeakPtr<ClientSessionControl> client_session_control_;

  std::unique_ptr<DesktopDisplayInfoMonitor> display_info_monitor_;

  DesktopEnvironmentOptions options_;
};

// Used to create |BasicDesktopEnvironment| instances.
class BasicDesktopEnvironmentFactory : public DesktopEnvironmentFactory {
 public:
  BasicDesktopEnvironmentFactory(
      scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
      std::unique_ptr<DesktopInteractionStrategyFactory>
          interaction_strategy_factory);

  BasicDesktopEnvironmentFactory(const BasicDesktopEnvironmentFactory&) =
      delete;
  BasicDesktopEnvironmentFactory& operator=(
      const BasicDesktopEnvironmentFactory&) = delete;

  ~BasicDesktopEnvironmentFactory() override;

  // DesktopEnvironmentFactory implementation.
  bool SupportsAudioCapture() const override;

 protected:
  scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner() const {
    return caller_task_runner_;
  }

  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner() const {
    return ui_task_runner_;
  }

  void CreateInteractionStrategy(
      const DesktopEnvironmentOptions& options,
      DesktopInteractionStrategyFactory::CreateCallback callback);

 private:
  // Task runner on which methods of DesktopEnvironmentFactory interface should
  // be called.
  scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner_;

  // Used to run UI code.
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;

  std::unique_ptr<DesktopInteractionStrategyFactory>
      interaction_strategy_factory_;
};

}  // namespace remoting

#endif  // REMOTING_HOST_BASIC_DESKTOP_ENVIRONMENT_H_
