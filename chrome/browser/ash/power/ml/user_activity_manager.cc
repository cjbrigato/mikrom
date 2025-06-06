// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/power/ml/user_activity_manager.h"

#include <cmath>
#include <optional>

#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "ash/shell.h"
#include "ash/wm/mru_window_tracker.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/ash/browser_delegate/browser_controller.h"
#include "chrome/browser/ash/browser_delegate/browser_delegate.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/tab_contents/form_interaction_tab_helper.h"
#include "chromeos/ash/components/dbus/dbus_thread_manager.h"
#include "chromeos/ash/components/install_attributes/install_attributes.h"
#include "chromeos/constants/devicetype.h"
#include "chromeos/dbus/power_manager/power_supply_properties.pb.h"
#include "chromeos/ui/base/app_types.h"
#include "chromeos/ui/base/window_properties.h"
#include "components/prefs/pref_service.h"
#include "components/site_engagement/content/site_engagement_service.h"
#include "ui/aura/client/aura_constants.h"

namespace ash {
namespace power {
namespace ml {

namespace {

void LogPowerMLPreviousEventLoggingResult(PreviousEventLoggingResult result) {
  UMA_HISTOGRAM_ENUMERATION("PowerML.PreviousEventLogging.Result", result);
}

void LogPowerMLDimImminentAction(DimImminentAction action) {
  UMA_HISTOGRAM_ENUMERATION("PowerML.DimImminent.Action", action);
}

void LogPowerMLNonModelDimResult(FinalResult result) {
  UMA_HISTOGRAM_ENUMERATION("PowerML.NonModelDim.Result", result);
}

void LogPowerMLModelDimResult(FinalResult result) {
  UMA_HISTOGRAM_ENUMERATION("PowerML.ModelDim.Result", result);
}

void LogPowerMLModelNoDimResult(FinalResult result) {
  UMA_HISTOGRAM_ENUMERATION("PowerML.ModelNoDim.Result", result);
}

void LogPowerMLSmartDimModelRequestCancel(base::TimeDelta time) {
  UMA_HISTOGRAM_TIMES("PowerML.SmartDimModel.RequestCanceledDuration", time);
}

void LogPowerMLSmartDimModelRequestComplete(base::TimeDelta time) {
  UMA_HISTOGRAM_TIMES("PowerML.SmartDimModel.RequestCompleteDuration", time);
}

void LogMetricsToUMA(const UserActivityEvent& event) {
  const FinalResult result =
      event.event().type() == UserActivityEvent::Event::REACTIVATE
          ? FinalResult::kReactivation
          : FinalResult::kOff;
  if (!event.has_model_prediction() ||
      !event.model_prediction().model_applied()) {
    LogPowerMLDimImminentAction(DimImminentAction::kModelIgnored);
    LogPowerMLNonModelDimResult(result);
    return;
  }

  if (event.model_prediction().response() ==
      UserActivityEvent::ModelPrediction::DIM) {
    LogPowerMLDimImminentAction(DimImminentAction::kModelDim);
    LogPowerMLModelDimResult(result);
    return;
  }

  CHECK_EQ(UserActivityEvent::ModelPrediction::NO_DIM,
           event.model_prediction().response());

  LogPowerMLDimImminentAction(DimImminentAction::kModelNoDim);
  LogPowerMLModelNoDimResult(result);
}

int GetRoundedOrInvalidEngagementScore(content::WebContents* contents) {
  if (!site_engagement::SiteEngagementService::IsEnabled()) {
    return -1;
  }

  auto* service = site_engagement::SiteEngagementService::Get(
      contents->GetBrowserContext());
  DCHECK(service);

  // Scores range from 0 to 100. Round down to a multiple of 10 to conform to
  // privacy guidelines.
  double raw_score = service->GetScore(contents->GetVisibleURL());
  int rounded_score = static_cast<int>(raw_score / 10) * 10;
  DCHECK_LE(0, rounded_score);
  DCHECK_GE(100, rounded_score);
  return rounded_score;
}

}  // namespace

struct UserActivityManager::PreviousIdleEventData {
  // Gap between two smart dim decision requests.
  base::TimeDelta smart_dim_request_interval;
  // Features recorded for the smart dim decision request at the beginning of
  // |smart_dim_request_interval|.
  UserActivityEvent::Features features;
  // Model prediction recorded for the smart dim decision request signal at the
  // beginning of |smart_dim_request_interval|.
  UserActivityEvent::ModelPrediction model_prediction;
};

UserActivityManager::UserActivityManager(
    UserActivityUkmLogger* ukm_logger,
    ui::UserActivityDetector* detector,
    chromeos::PowerManagerClient* power_manager_client,
    session_manager::SessionManager* session_manager,
    mojo::PendingReceiver<viz::mojom::VideoDetectorObserver> receiver)
    : ukm_logger_(ukm_logger),
      session_manager_(session_manager),
      receiver_(this, std::move(receiver)),
      power_manager_client_(power_manager_client) {
  DCHECK(ukm_logger_);

  DCHECK(detector);
  user_activity_observation_.Observe(detector);

  DCHECK(power_manager_client);
  power_manager_client_observation_.Observe(power_manager_client);
  power_manager_client->RequestStatusUpdate();
  power_manager_client->GetSwitchStates(
      base::BindOnce(&UserActivityManager::OnReceiveSwitchStates,
                     weak_ptr_factory_.GetWeakPtr()));
  power_manager_client->GetInactivityDelays(
      base::BindOnce(&UserActivityManager::OnReceiveInactivityDelays,
                     weak_ptr_factory_.GetWeakPtr()));

  DCHECK(session_manager);
  session_manager_observation_.Observe(session_manager);

  if (chromeos::GetDeviceType() == chromeos::DeviceType::kChromebook) {
    device_type_ = UserActivityEvent::Features::CHROMEBOOK;
  } else {
    device_type_ = UserActivityEvent::Features::UNKNOWN_DEVICE;
  }
}

UserActivityManager::~UserActivityManager() = default;

void UserActivityManager::OnUserActivity(const ui::Event* /* event */) {
  MaybeLogEvent(UserActivityEvent::Event::REACTIVATE,
                UserActivityEvent::Event::USER_ACTIVITY);
}

void UserActivityManager::LidEventReceived(
    chromeos::PowerManagerClient::LidState state,
    base::TimeTicks /* timestamp */) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  lid_state_ = state;
}

void UserActivityManager::PowerChanged(
    const power_manager::PowerSupplyProperties& proto) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (external_power_.has_value()) {
    bool power_source_changed = (*external_power_ != proto.external_power());

    // Only log when power source changed, don't care about percentage change.
    if (power_source_changed) {
      MaybeLogEvent(UserActivityEvent::Event::REACTIVATE,
                    UserActivityEvent::Event::POWER_CHANGED);
    }
  }
  external_power_ = proto.external_power();

  if (proto.has_battery_percent()) {
    battery_percent_ = proto.battery_percent();
  }
}

void UserActivityManager::TabletModeEventReceived(
    chromeos::PowerManagerClient::TabletMode mode,
    base::TimeTicks /* timestamp */) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tablet_mode_ = mode;
}

void UserActivityManager::ScreenIdleStateChanged(
    const power_manager::ScreenIdleState& proto) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!screen_dimmed_ && proto.dimmed()) {
    screen_dim_occurred_ = true;
  }
  screen_dimmed_ = proto.dimmed();

  if (!screen_off_ && proto.off()) {
    screen_off_occurred_ = true;
  }
  screen_off_ = proto.off();
}

// We log event when SuspendImminent is received. There is a chance that a
// Suspend is cancelled, so that the corresponding SuspendDone has a short
// sleep duration. However, we ignore these cases because it's infeasible to
// to wait for a SuspendDone before deciding what to log.
void UserActivityManager::SuspendImminent(
    power_manager::SuspendImminent::Reason reason) {
  switch (reason) {
    case power_manager::SuspendImminent_Reason_IDLE:
      MaybeLogEvent(UserActivityEvent::Event::TIMEOUT,
                    UserActivityEvent::Event::IDLE_SLEEP);
      break;
    case power_manager::SuspendImminent_Reason_LID_CLOSED:
      MaybeLogEvent(UserActivityEvent::Event::OFF,
                    UserActivityEvent::Event::LID_CLOSED);
      break;
    case power_manager::SuspendImminent_Reason_OTHER:
      MaybeLogEvent(UserActivityEvent::Event::OFF,
                    UserActivityEvent::Event::MANUAL_SLEEP);
      break;
    default:
      // We don't track other suspend reason.
      break;
  }
}

void UserActivityManager::InactivityDelaysChanged(
    const power_manager::PowerManagementPolicy::Delays& delays) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  OnReceiveInactivityDelays(delays);
}

void UserActivityManager::OnVideoActivityStarted() {
  MaybeLogEvent(UserActivityEvent::Event::REACTIVATE,
                UserActivityEvent::Event::VIDEO_ACTIVITY);
}

void UserActivityManager::UpdateAndGetSmartDimDecision(
    const IdleEventNotifier::ActivityData& activity_data,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::TimeDelta now = boot_clock_.GetTimeSinceBoot();
  if (waiting_for_final_action_) {
    if (waiting_for_model_decision_) {
      CancelDimDecisionRequest();
    } else {
      // Smart dim request comes again after an earlier request event without
      // any user action/suspend in between.
      PopulatePreviousEventData(now);
    }
  }

  idle_event_start_since_boot_ = now;

  screen_dim_occurred_ = false;
  screen_off_occurred_ = false;
  screen_lock_occurred_ = false;

  ExtractFeatures(activity_data);

  // Default is to enable smart dim, unless user profile specifically says
  // otherwise. If there are multiple users, the primary one may have
  // more-restrictive policy-controlled settings.
  bool smart_dim_enabled = true;
  const Profile* const profile = ProfileManager::GetPrimaryUserProfile();
  if (profile) {
    smart_dim_enabled =
        profile->GetPrefs()->GetBoolean(ash::prefs::kPowerSmartDimEnabled);
  }

  if (smart_dim_enabled &&
      base::FeatureList::IsEnabled(features::kUserActivityPrediction)) {
    time_dim_decision_requested_ = base::TimeTicks::Now();
    DimDecisionCallback request_callback =
        base::BindOnce(&UserActivityManager::HandleSmartDimDecision,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback));

    // Requests smart dim decision with current features_.
    waiting_for_model_decision_ = true;
    SmartDimMlAgent::GetInstance()->RequestDimDecision(
        features_, std::move(request_callback));
  } else {
    // Smart Dim feature is unsupported or disabled by policy => the dim
    // decision defaults to false (i.e. do not defer screen dim).
    std::move(callback).Run(false);
  }

  waiting_for_final_action_ = true;
}

void UserActivityManager::HandleSmartDimDecision(
    base::OnceCallback<void(bool)> callback,
    std::optional<UserActivityEvent::ModelPrediction> prediction) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  waiting_for_model_decision_ = false;

  if (!prediction.has_value()) {
    // No decision from Smart Dim (e.g. it was canceled). Return default value
    // (false means "allow dim") to the D-bus caller.
    std::move(callback).Run(false);
    return;
  }

  // Smart Dim model actually ran:

  const base::TimeDelta wait_time =
      base::TimeTicks::Now() - time_dim_decision_requested_;
  LogPowerMLSmartDimModelRequestComplete(wait_time);
  time_dim_decision_requested_ = base::TimeTicks();

  // Only defer the dim if the model predicts so and also if the dim was not
  // previously deferred.
  if (prediction->response() == UserActivityEvent::ModelPrediction::NO_DIM &&
      !dim_deferred_) {
    dim_deferred_ = true;
    prediction->set_model_applied(true);
  } else {
    // Either model predicts dim or model fails, or it was previously dimmed.
    // TODO(amoylan): Are the below in the wrong order?! dim_deferred_ has no
    //                effect in the second line...
    dim_deferred_ = false;
    prediction->set_model_applied(prediction->response() ==
                                      UserActivityEvent::ModelPrediction::DIM &&
                                  !dim_deferred_);
  }
  model_prediction_ = prediction;
  std::move(callback).Run(dim_deferred_);
}

void UserActivityManager::OnSessionStateChanged() {
  TRACE_EVENT0("ui", "UserActivityManager::OnSessionStateChanged");
  DCHECK(session_manager_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool was_locked = screen_is_locked_;
  screen_is_locked_ = session_manager_->IsScreenLocked();
  if (!was_locked && screen_is_locked_) {
    screen_lock_occurred_ = true;
  }
}

void UserActivityManager::OnReceiveSwitchStates(
    std::optional<chromeos::PowerManagerClient::SwitchStates> switch_states) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (switch_states.has_value()) {
    lid_state_ = switch_states->lid_state;
    tablet_mode_ = switch_states->tablet_mode;
  }
}

void UserActivityManager::OnReceiveInactivityDelays(
    std::optional<power_manager::PowerManagementPolicy::Delays> delays) {
  if (delays.has_value()) {
    screen_dim_delay_ = base::Milliseconds(delays->screen_dim_ms());
    screen_off_delay_ = base::Milliseconds(delays->screen_off_ms());
  }
}

void UserActivityManager::ExtractFeatures(
    const IdleEventNotifier::ActivityData& activity_data) {
  // Set transition times for dim and screen-off.
  if (!screen_dim_delay_.is_zero()) {
    features_.set_on_to_dim_sec(std::ceil(screen_dim_delay_.InSecondsF()));
  }
  if (!screen_off_delay_.is_zero()) {
    features_.set_dim_to_screen_off_sec(
        std::ceil((screen_off_delay_ - screen_dim_delay_).InSecondsF()));
  }

  // Set time related features.
  features_.set_last_activity_day(activity_data.last_activity_day);

  features_.set_last_activity_time_sec(
      activity_data.last_activity_time_of_day.InSeconds());

  if (activity_data.last_user_activity_time_of_day) {
    features_.set_last_user_activity_time_sec(
        activity_data.last_user_activity_time_of_day.value().InSeconds());
  }

  features_.set_recent_time_active_sec(
      activity_data.recent_time_active.InSeconds());

  if (activity_data.time_since_last_key) {
    features_.set_time_since_last_key_sec(
        activity_data.time_since_last_key.value().InSeconds());
  }
  if (activity_data.time_since_last_mouse) {
    features_.set_time_since_last_mouse_sec(
        activity_data.time_since_last_mouse.value().InSeconds());
  }
  if (activity_data.time_since_last_touch) {
    features_.set_time_since_last_touch_sec(
        activity_data.time_since_last_touch.value().InSeconds());
  }

  features_.set_video_playing_time_sec(
      activity_data.video_playing_time.InSeconds());

  if (activity_data.time_since_video_ended) {
    features_.set_time_since_video_ended_sec(
        activity_data.time_since_video_ended.value().InSeconds());
  }

  features_.set_key_events_in_last_hour(activity_data.key_events_in_last_hour);
  features_.set_mouse_events_in_last_hour(
      activity_data.mouse_events_in_last_hour);
  features_.set_touch_events_in_last_hour(
      activity_data.touch_events_in_last_hour);

  // Set device mode.
  if (lid_state_ == chromeos::PowerManagerClient::LidState::CLOSED) {
    features_.set_device_mode(UserActivityEvent::Features::CLOSED_LID);
  } else if (lid_state_ == chromeos::PowerManagerClient::LidState::OPEN) {
    if (tablet_mode_ == chromeos::PowerManagerClient::TabletMode::ON) {
      features_.set_device_mode(UserActivityEvent::Features::TABLET);
    } else {
      features_.set_device_mode(UserActivityEvent::Features::CLAMSHELL);
    }
  } else {
    features_.set_device_mode(UserActivityEvent::Features::UNKNOWN_MODE);
  }

  features_.set_device_type(device_type_);

  if (battery_percent_.has_value()) {
    features_.set_battery_percent(*battery_percent_);
  }
  if (external_power_.has_value()) {
    features_.set_on_battery(
        *external_power_ == power_manager::PowerSupplyProperties::DISCONNECTED);
  }

  features_.set_device_management(
      ash::InstallAttributes::Get()->IsEnterpriseManaged()
          ? UserActivityEvent::Features::MANAGED
          : UserActivityEvent::Features::UNMANAGED);

  features_.set_screen_dimmed_initially(screen_dimmed_);
  features_.set_screen_off_initially(screen_off_);
  features_.set_screen_locked_initially(screen_is_locked_);

  features_.set_previous_negative_actions_count(
      previous_negative_actions_count_);
  features_.set_previous_positive_actions_count(
      previous_positive_actions_count_);

  const TabProperty ash_tab_property = UpdateOpenTabURL();

  if (ash_tab_property.source_id == -1) {
    return;
  }

  features_.set_source_id(ash_tab_property.source_id);

  if (!ash_tab_property.domain.empty()) {
    features_.set_tab_domain(ash_tab_property.domain);
  }

  if (ash_tab_property.engagement_score != -1) {
    features_.set_engagement_score(ash_tab_property.engagement_score);
  }

  features_.set_has_form_entry(ash_tab_property.has_form_entry);
}

TabProperty UserActivityManager::UpdateOpenTabURL() {
  BrowserDelegate* browser =
      BrowserController::GetInstance()->GetLastUsedVisibleBrowser();
  if (!browser || browser->IsOffTheRecord()) {
    return TabProperty{};
  }

  content::WebContents* contents = browser->GetActiveWebContents();
  if (!contents) {
    return TabProperty{};
  }

  ukm::SourceId source_id =
      contents->GetPrimaryMainFrame()->GetPageUkmSourceId();
  if (source_id == ukm::kInvalidSourceId) {
    return TabProperty{};
  }

  TabProperty property;
  property.source_id = source_id;
  // Domain could be empty.
  property.domain = contents->GetLastCommittedURL().host();
  // Engagement score could be -1 if engagement service is disabled.
  property.engagement_score = GetRoundedOrInvalidEngagementScore(contents);
  property.has_form_entry = FormInteractionTabHelper::FromWebContents(contents)
                                ->had_form_interaction();
  return property;
}

void UserActivityManager::MaybeLogEvent(
    UserActivityEvent::Event::Type type,
    UserActivityEvent::Event::Reason reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!waiting_for_final_action_)
    return;

  if (waiting_for_model_decision_) {
    CancelDimDecisionRequest();
    return;
  }
  UserActivityEvent activity_event;

  UserActivityEvent::Event* event = activity_event.mutable_event();
  event->set_type(type);
  event->set_reason(reason);
  if (idle_event_start_since_boot_) {
    event->set_log_duration_sec(
        (boot_clock_.GetTimeSinceBoot() - idle_event_start_since_boot_.value())
            .InSeconds());
  }
  event->set_screen_dim_occurred(screen_dim_occurred_);
  event->set_screen_lock_occurred(screen_lock_occurred_);
  event->set_screen_off_occurred(screen_off_occurred_);

  *activity_event.mutable_features() = features_;

  if (model_prediction_) {
    *activity_event.mutable_model_prediction() = model_prediction_.value();
  }

  // If there's an earlier idle event that has not received its own event,
  // log it here too. Note, we log the earlier event before the current
  // event.
  if (previous_idle_event_data_) {
    UserActivityEvent previous_activity_event = activity_event;
    UserActivityEvent::Event* previous_event =
        previous_activity_event.mutable_event();
    if (previous_event->has_log_duration_sec()) {
      previous_event->set_log_duration_sec(
          previous_event->log_duration_sec() +
          previous_idle_event_data_->smart_dim_request_interval.InSeconds());
    }

    *previous_activity_event.mutable_features() =
        previous_idle_event_data_->features;
    *previous_activity_event.mutable_model_prediction() =
        previous_idle_event_data_->model_prediction;
    ukm_logger_->LogActivity(previous_activity_event);
    LogMetricsToUMA(previous_activity_event);
  }

  // Log to metrics.
  ukm_logger_->LogActivity(activity_event);
  LogMetricsToUMA(activity_event);

  // Update the counters for next event logging.
  if (type == UserActivityEvent::Event::REACTIVATE) {
    previous_negative_actions_count_++;
  } else {
    previous_positive_actions_count_++;
  }
  ResetAfterLogging();
}

void UserActivityManager::PopulatePreviousEventData(
    const base::TimeDelta& now) {
  PreviousEventLoggingResult result = PreviousEventLoggingResult::kSuccess;
  if (!model_prediction_) {
    result = base::FeatureList::IsEnabled(features::kUserActivityPrediction)
                 ? PreviousEventLoggingResult::kErrorModelPredictionMissing
                 : PreviousEventLoggingResult::kErrorModelDisabled;
    LogPowerMLPreviousEventLoggingResult(result);
  }

  if (previous_idle_event_data_) {
    result = PreviousEventLoggingResult::kErrorMultiplePreviousEvents;
    previous_idle_event_data_.reset();
    LogPowerMLPreviousEventLoggingResult(result);
  }

  if (!idle_event_start_since_boot_) {
    result = PreviousEventLoggingResult::kErrorIdleStartMissing;
    LogPowerMLPreviousEventLoggingResult(result);
  }

  if (result != PreviousEventLoggingResult::kSuccess) {
    LogPowerMLPreviousEventLoggingResult(PreviousEventLoggingResult::kError);
    return;
  }

  // Only log if none of the errors above occurred.
  LogPowerMLPreviousEventLoggingResult(result);

  previous_idle_event_data_ = std::make_unique<PreviousIdleEventData>();
  previous_idle_event_data_->smart_dim_request_interval =
      now - idle_event_start_since_boot_.value();

  previous_idle_event_data_->features = features_;
  previous_idle_event_data_->model_prediction = model_prediction_.value();
}

void UserActivityManager::ResetAfterLogging() {
  features_.Clear();
  idle_event_start_since_boot_ = std::nullopt;
  waiting_for_final_action_ = false;
  model_prediction_ = std::nullopt;

  previous_idle_event_data_.reset();
}

void UserActivityManager::CancelDimDecisionRequest() {
  LOG(WARNING) << "Cancelling pending Smart Dim decision request.";
  SmartDimMlAgent::GetInstance()->CancelPreviousRequest();

  waiting_for_model_decision_ = false;
  const base::TimeDelta wait_time =
      base::TimeTicks::Now() - time_dim_decision_requested_;
  LogPowerMLSmartDimModelRequestCancel(wait_time);
  time_dim_decision_requested_ = base::TimeTicks();
}

}  // namespace ml
}  // namespace power
}  // namespace ash
