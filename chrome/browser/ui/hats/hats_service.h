// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_HATS_HATS_SERVICE_H_
#define CHROME_BROWSER_UI_HATS_HATS_SERVICE_H_

#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/hats/survey_config.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/messages/android/message_enums.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class WebContents;
}

class Profile;

// Key-value mapping type for survey's product specific bits data.
typedef std::map<std::string, bool> SurveyBitsData;

// Key-value mapping type for survey's product specific string data.
typedef std::map<std::string, std::string> SurveyStringData;

// This class provides the client side logic for determining if a
// survey should be shown for any trigger based on input from a finch
// configuration. It is created on a per profile basis.
class HatsService : public KeyedService {
 public:
  struct SurveyMetadata {
    SurveyMetadata();
    ~SurveyMetadata();

    // Trigger specific metadata.
    std::optional<int> last_major_version;
    std::optional<base::Time> last_survey_started_time;
    std::optional<bool> is_survey_full;
    std::optional<base::Time> last_survey_check_time;

    // Metadata affecting all triggers.
    std::optional<base::Time> any_last_survey_started_time;
    std::optional<base::Time>
        any_last_survey_with_cooldown_override_started_time;
  };

  struct SurveyOptions {
    explicit SurveyOptions(
        std::optional<std::u16string> custom_invitation = std::nullopt,
        std::optional<messages::MessageIdentifier> message_identifier =
            std::nullopt);
    SurveyOptions(const SurveyOptions& other);
    ~SurveyOptions();

    std::optional<std::u16string> custom_invitation;
    std::optional<messages::MessageIdentifier> message_identifier;
  };

  enum NavigationBehavior {
    ALLOW_ANY = 0,              // allow any navigation
    REQUIRE_SAME_ORIGIN = 1,    // abort survey on cross-origin navigation
    REQUIRE_SAME_DOCUMENT = 2,  // abort survey on cross-document navigation
  };

  explicit HatsService(Profile* profile);
  HatsService(const HatsService&) = delete;
  HatsService& operator=(const HatsService&) = delete;

  ~HatsService() override;

  // Launches survey with identifier |trigger| if appropriate.
  // |success_callback| is called when the survey is shown to the user.
  // |failure_callback| is called if the survey does not launch for any reason.
  // |product_specific_bits_data| and |product_specific_string_data| must
  // contain key-value pairs where the keys match the field names set for the
  // survey in survey_config.cc, and the values are those which will be
  // associated with the survey response.
  // |supplied_trigger_id| allows the caller to specify a trigger id. If set,
  // overrides the survey's trigger_id defined in
  // `SurveyConfig::GetAllSurveyConfigs`
  // |survey_options| can be used to.
  // customize survey invitations on Android. This is an experimental feature
  // and may be removed in the future. For a NOP, use the default constructor of
  // SurveyOptions.
  virtual void LaunchSurvey(
      const std::string& trigger,
      base::OnceClosure success_callback,
      base::OnceClosure failure_callback,
      const SurveyBitsData& product_specific_bits_data,
      const SurveyStringData& product_specific_string_data,
      const std::optional<std::string>& supplied_trigger_id,
      const SurveyOptions& survey_options) = 0;

  void LaunchSurvey(const std::string& trigger,
                    base::OnceClosure success_callback = base::DoNothing(),
                    base::OnceClosure failure_callback = base::DoNothing(),
                    const SurveyBitsData& product_specific_bits_data = {},
                    const SurveyStringData& product_specific_string_data = {}) {
    LaunchSurvey(trigger, std::move(success_callback),
                 std::move(failure_callback), product_specific_bits_data,
                 product_specific_string_data, std::nullopt, SurveyOptions());
  }

  // Launches survey with id |trigger|.
  // |product_specific_bits_data| and |product_specific_string_data| must
  // contain key-value pairs where the keys match the field names set for the
  // survey in survey_config.cc, and the values are those which will be
  // associated with the survey response.
  // |web_contents| specifies the `WebContents` where the survey should be
  // displayed.
  virtual void LaunchSurveyForWebContents(
      const std::string& trigger,
      content::WebContents* web_contents,
      const SurveyBitsData& product_specific_bits_data,
      const SurveyStringData& product_specific_string_data,
      base::OnceClosure success_callback,
      base::OnceClosure failure_callback,
      const std::optional<std::string>& supplied_trigger_id,
      const SurveyOptions& survey_options) = 0;
  void LaunchSurveyForWebContents(
      const std::string& trigger,
      content::WebContents* web_contents,
      const SurveyBitsData& product_specific_bits_data,
      const SurveyStringData& product_specific_string_data,
      base::OnceClosure success_callback = base::DoNothing(),
      base::OnceClosure failure_callback = base::DoNothing(),
      const std::optional<std::string>& supplied_trigger_id = std::nullopt) {
    LaunchSurveyForWebContents(
        trigger, web_contents, product_specific_bits_data,
        product_specific_string_data, std::move(success_callback),
        std::move(failure_callback), supplied_trigger_id, SurveyOptions());
  }

  // Launches survey with id |trigger| with a timeout |timeout_ms| if
  // appropriate.
  // |product_specific_bits_data| and |product_specific_string_data| must
  // contain key-value pairs where the keys match the field names set for the
  // survey in survey_config.cc, and the values are those which will be
  // associated with the survey response.
  // Returns true if the survey was successfully scheduled.
  virtual bool LaunchDelayedSurvey(
      const std::string& trigger,
      int timeout_ms,
      const SurveyBitsData& product_specific_bits_data,
      const SurveyStringData& product_specific_string_data) = 0;
  bool LaunchDelayedSurvey(
      const std::string& trigger,
      int timeout_ms,
      const SurveyBitsData& product_specific_bits_data = {}) {
    return LaunchDelayedSurvey(trigger, timeout_ms, product_specific_bits_data,
                               {});
  }

  // Launches survey with id |trigger| with a timeout |timeout_ms| for tab
  // |web_contents| if appropriate. |web_contents| required to be non-nullptr.
  // Launch is cancelled if |web_contents| killed before end of timeout.
  // Rejects (and returns false) if there is already an identical delayed-task
  // (same |trigger| and same |web_contents|) waiting to be fulfilled. Also
  // rejects if the underlying task posting fails.
  // |navigation_behavior| specifies whether cross-origin or cross-document
  // navigations should abort the survey.
  // |success_callback| is called when the survey is shown to the user.
  // |failure_callback| is called if the survey does not launch for any reason.
  // Returns true if the survey was successfully scheduled.
  virtual bool LaunchDelayedSurveyForWebContents(
      const std::string& trigger,
      content::WebContents* web_contents,
      int timeout_ms,
      const SurveyBitsData& product_specific_bits_data,
      const SurveyStringData& product_specific_string_data,
      NavigationBehavior navigation_behavior,
      base::OnceClosure success_callback,
      base::OnceClosure failure_callback,
      const std::optional<std::string>& supplied_trigger_id,
      const SurveyOptions& survey_options) = 0;
  bool LaunchDelayedSurveyForWebContents(
      const std::string& trigger,
      content::WebContents* web_contents,
      int timeout_ms,
      const SurveyBitsData& product_specific_bits_data = {},
      const SurveyStringData& product_specific_string_data = {},
      NavigationBehavior navigation_behavior = NavigationBehavior::ALLOW_ANY,
      base::OnceClosure success_callback = base::DoNothing(),
      base::OnceClosure failure_callback = base::DoNothing(),
      const std::optional<std::string>& supplied_trigger_id = std::nullopt) {
    return LaunchDelayedSurveyForWebContents(
        trigger, web_contents, timeout_ms, product_specific_bits_data,
        product_specific_string_data, navigation_behavior,
        std::move(success_callback), std::move(failure_callback),
        supplied_trigger_id, SurveyOptions());
  }

  // Whether the user is eligible for any survey (of the type |user_prompted|
  // or not) to be shown. A return value of false is always a true-negative,
  // and means the user is currently ineligible for all surveys. A return value
  // of true should not be interpreted as a guarantee that requests to show a
  // survey will succeed.
  virtual bool CanShowAnySurvey(bool user_prompted) const = 0;

  // Whether the survey specified by |trigger| can be shown to the user. This
  // is a pre-check that calculates as many conditions as possible, but could
  // still return a false positive due to client-side rate limiting, a change
  // in network conditions, or intervening calls to this API.
  virtual bool CanShowSurvey(const std::string& trigger) const = 0;

  // Updates the user preferences to record that the survey associated with
  // |survey_id| was shown to the user. |trigger_id| is the HaTS next Trigger
  // ID for the survey.
  virtual void RecordSurveyAsShown(std::string trigger_id) = 0;

  hats::SurveyConfigs& GetSurveyConfigsByTriggersForTesting();

 protected:
  hats::SurveyConfigs survey_configs_by_triggers_;
  using SurveyConfigs = base::flat_map<std::string, hats::SurveyConfig>;

  Profile* profile() const { return profile_; }

  // Checks whether the navigation is allowed under the given navigation
  // behavior.
  bool IsNavigationAllowed(content::NavigationHandle* navigation_handle,
                           HatsService::NavigationBehavior navigation_behavior);

 private:
  friend class DelayedSurveyTask;
  FRIEND_TEST_ALL_PREFIXES(HatsServiceProbabilityOne, SingleHatsNextDialog);

  // Profile associated with this service.
  const raw_ptr<Profile> profile_;

  base::WeakPtrFactory<HatsService> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_HATS_HATS_SERVICE_H_
