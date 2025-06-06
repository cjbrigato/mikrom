// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_FRAME_SINKS_EXTERNAL_BEGIN_FRAME_SOURCE_MOJO_H_
#define COMPONENTS_VIZ_SERVICE_FRAME_SINKS_EXTERNAL_BEGIN_FRAME_SOURCE_MOJO_H_

#include <optional>

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/frame_sinks/begin_frame_source.h"
#include "components/viz/service/display/display.h"
#include "components/viz/service/frame_sinks/frame_sink_observer.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "services/viz/privileged/mojom/compositing/external_begin_frame_controller.mojom.h"

namespace viz {

class FrameSinkManagerImpl;

// Implementation of ExternalBeginFrameSource that's controlled by IPCs over
// the mojom::ExternalBeginFrameController interface. Replaces the Display's
// default BeginFrameSource. Observes the Display to be notified of BeginFrame
// completion.
class VIZ_SERVICE_EXPORT ExternalBeginFrameSourceMojo
    : public mojom::ExternalBeginFrameController,
      public DisplayObserver,
      public ExternalBeginFrameSource,
      public ExternalBeginFrameSourceClient,
      public FrameSinkObserver {
 public:
  // `controller_receiver` must be a valid mojo receiver.
  // `controller_client_remote` is optional and can be an invalid remote.
  ExternalBeginFrameSourceMojo(
      FrameSinkManagerImpl* frame_sink_manager,
      mojo::PendingAssociatedReceiver<mojom::ExternalBeginFrameController>
          controller_receiver,
      mojo::PendingAssociatedRemote<mojom::ExternalBeginFrameControllerClient>
          controller_client_remote,
      uint32_t restart_id);
  ~ExternalBeginFrameSourceMojo() override;

  // mojom::ExternalBeginFrameController implementation.
  void IssueExternalBeginFrame(
      const BeginFrameArgs& args,
      bool force,
      base::OnceCallback<void(const BeginFrameAck&)> callback) override;

  void SetDisplay(Display* display);

 private:
  // ExternalBeginFrameSourceClient implementation.
  void OnNeedsBeginFrames(bool needs_begin_frames) override;
  void SetPreferredInterval(base::TimeDelta interval) override;

  // DisplayObserver overrides.
  void OnDisplayDidFinishFrame(const BeginFrameAck& ack) override;
  void OnDisplayDestroyed() override;

  // FrameSinkObserver overrides.
  void OnDestroyedCompositorFrameSink(
      const FrameSinkId& frame_sink_id) override;
  void OnFrameSinkDidBeginFrame(const FrameSinkId& frame_sink_id,
                                const BeginFrameArgs& args) override;
  void OnFrameSinkDidFinishFrame(const FrameSinkId& frame_sink_id,
                                 const BeginFrameArgs& args) override;

  void MaybeProduceFrameCallback();
  void DispatchFrameCallback(const BeginFrameAck& ack);

  const raw_ptr<FrameSinkManagerImpl> frame_sink_manager_;

  // The pending_frame_callback_ needs to be destroyed after the mojo receiver,
  // or else we may get a DCHECK that the callback was dropped while the
  // receiver is open. Since destruction order is well defined, ensuring that
  // the callback is listed before the receiver is enough to guarantee this.
  base::OnceCallback<void(const BeginFrameAck& ack)> pending_frame_callback_;

  mojo::AssociatedReceiver<mojom::ExternalBeginFrameController> receiver_;
  mojo::AssociatedRemote<mojom::ExternalBeginFrameControllerClient>
      remote_client_;
  // The frame source id as specified in BeginFrameArgs passed to
  // IssueExternalBeginFrame. Note this is likely to be different from our
  // source id, but this is what will be reported to FrameSinkObserver methods.
  uint64_t original_source_id_ = BeginFrameArgs::kStartingSourceId;

  base::flat_set<FrameSinkId> pending_frame_sinks_;
  std::optional<BeginFrameAck> pending_ack_;
  raw_ptr<Display> display_ = nullptr;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_FRAME_SINKS_EXTERNAL_BEGIN_FRAME_SOURCE_MOJO_H_
