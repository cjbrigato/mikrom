// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/privacy_sandbox/privacy_sandbox_dialog_ui.h"

#include <memory>
#include <utility>

#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/privacy_sandbox/privacy_sandbox_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/webui/privacy_sandbox/privacy_sandbox_dialog_handler.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/privacy_sandbox_resources.h"
#include "chrome/grit/privacy_sandbox_resources_map.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/strings/grit/components_strings.h"
#include "components/strings/grit/privacy_sandbox_strings.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/native_theme/native_theme.h"
#include "ui/webui/webui_util.h"

// The name of the on-click function when the privacy policy link is pressed.
inline constexpr char16_t kPrivacyPolicyFunc[] = u"onPrivacyPolicyLinkClicked_";

// The id of the html element that opens the privacy policy link.
inline constexpr char16_t kPrivacyPolicyId[] = u"privacyPolicyLink";

// The V2 id of the html element that opens the privacy policy link - Ads API UX
// Enhancements.
inline constexpr char16_t kPrivacyPolicyIdV2[] = u"privacyPolicyLinkV2";

// The V3 id of the html element that opens the privacy policy link - Ad Topics
// Content Parity
inline constexpr char16_t kPrivacyPolicyIdV3[] = u"privacyPolicyLinkV3";

PrivacySandboxDialogUI::PrivacySandboxDialogUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  auto* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), chrome::kChromeUIPrivacySandboxDialogHost);

  webui::SetupWebUIDataSource(
      source, kPrivacySandboxResources,
      IDR_PRIVACY_SANDBOX_PRIVACY_SANDBOX_NOTICE_DIALOG_HTML);

  // Allow the chrome-untrusted://privacy-sandbox-dialog/privacy-policy page to
  // load as an iframe in the page.
  std::string frame_src = base::StringPrintf(
      "frame-src %s 'self';",
      base::StrCat(
          {chrome::kChromeUIUntrustedPrivacySandboxDialogURL,
           chrome::kChromeUIUntrustedPrivacySandboxDialogPrivacyPolicyPath})
          .c_str());
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc, frame_src);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources chrome://webui-test 'self' "
      "'unsafe-inline';");

  // Set up Content Security Policy (CSP) for
  // chrome-untrusted://privacy-sandbox-dialog/ iframe.
  web_ui->AddRequestableScheme(content::kChromeUIUntrustedScheme);

  source->AddResourcePath(
      chrome::kChromeUIPrivacySandboxDialogCombinedPath,
      IDR_PRIVACY_SANDBOX_PRIVACY_SANDBOX_COMBINED_DIALOG_HTML);
  source->AddResourcePath(
      chrome::kChromeUIPrivacySandboxDialogNoticePath,
      IDR_PRIVACY_SANDBOX_PRIVACY_SANDBOX_NOTICE_DIALOG_HTML);
  source->AddResourcePath(
      chrome::kChromeUIPrivacySandboxDialogNoticeRestrictedPath,
      IDR_PRIVACY_SANDBOX_PRIVACY_SANDBOX_NOTICE_RESTRICTED_DIALOG_HTML);

  static constexpr webui::LocalizedString kStrings[] = {
      {"adPrivacyPageTitle", IDS_SETTINGS_AD_PRIVACY_PAGE_TITLE},

      // Strings for the consent step of the combined dialog (kM1Consent).
      {"m1ConsentTitle", IDS_PRIVACY_SANDBOX_M1_CONSENT_TITLE},
      {"m1ConsentDescription1", IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_1},
      {"m1ConsentDescription2", IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_2},
      {"m1ConsentDescription3", IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_3},
      {"m1ConsentLearnMoreExpandLabel",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_EXPAND_LABEL},
      {"m1ConsentDescription4", IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_4},
      {"m1ConsentSavingLabel", IDS_PRIVACY_SANDBOX_M1_CONSENT_SAVING_LABEL},
      {"m1ConsentAcceptButton", IDS_PRIVACY_SANDBOX_M1_CONSENT_ACCEPT_BUTTON},
      {"m1ConsentDeclineButton", IDS_PRIVACY_SANDBOX_M1_CONSENT_DECLINE_BUTTON},
      {"m1ConsentLearnMoreBullet1",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_1},
      {"m1ConsentLearnMoreBullet2",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_2},
      {"m1ConsentLearnMoreBullet3",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_3},

      // Strings for the consent step of the combined dialog with the Ads API UX
      // Enhancement.
      {"m1ConsentDescription2V2",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_2_V2},
      {"m1ConsentDescription4V2",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_4_V2},
      {"m1ConsentLearnmoreBullet1V2",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_1_V2},
      {"m1ConsentLearnmoreBullet2V2",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_2_V2},
      {"m1ConsentLearnmoreBullet3V2",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_3_V2},

      // Strings for the notice step of the combined dialog (kM1NoticeEEA).
      {"m1NoticeEeaTitle", IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_TITLE},
      {"m1NoticeEeaDescription1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_DESCRIPTION_1},
      {"m1NoticeEeaBullet1", IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_BULLET_1},
      {"m1NoticeEeaBullet2", IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_BULLET_2},
      {"m1NoticeEeaLearnMoreExpandLabel",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_EXPAND_LABEL},
      {"m1NoticeEeaDescription2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_DESCRIPTION_2},
      {"m1NoticeEeaAckButton", IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_ACK_BUTTON},
      {"m1NoticeEeaSettingsButton",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SETTINGS_BUTTON},
      {"m1NoticeEeaLearnMoreHeading1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_HEADING_1},
      {"m1NoticeEeaLearnMoreDescription",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_DESCRIPTION},
      {"m1NoticeEeaLearnMoreHeading2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_HEADING_2},
      {"m1NoticeEeaLearnMoreBullet1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_BULLET_1},
      {"m1NoticeEeaLearnMoreBullet2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_BULLET_2},
      {"m1NoticeEeaLearnMoreBullet3",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LEARN_MORE_BULLET_3},

      // Strings for the notice step of the combined dialog with the Ads API UX
      // Enhancement.
      {"m1NoticeEEADescription1V2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_DESCRIPTION_1_V2},
      {"m1NoticeEEASiteSuggestedAdsTitle",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SITE_SUGGESTED_ADS_TITLE},
      {"m1NoticeEEASiteSuggestedAdsDescription",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SITE_SUGGESTED_ADS_DESCRIPTION},
      {"m1NoticeEEASiteSuggestedAdsLearnMoreLabel",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SITE_SUGGESTED_ADS_LEARN_MORE_LABEL},
      {"m1NoticeEEASiteSuggestedAdsLearnMoreBullet1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SITE_SUGGESTED_ADS_LEARN_MORE_BULLET_1},
      {"m1NoticeEEASiteSuggestedAdsLearnMoreBullet2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SITE_SUGGESTED_ADS_LEARN_MORE_BULLET_2},
      {"m1NoticeEEAAdMeasurementTitle",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_AD_MEASUREMENT_TITLE},
      {"m1NoticeEEAAdMeasurementDescription",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_AD_MEASUREMENT_DESCRIPTION},
      {"m1NoticeEEAAdMeasurementLearnMoreLabel",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_AD_MEASUREMENT_LEARN_MORE_LABEL},
      {"m1NoticeEEAAdMeasurementLearnMoreBullet1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_AD_MEASUREMENT_LEARN_MORE_BULLET_1},
      {"m1NoticeEEALastText", IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_LAST_TEXT},

      // Strings for the notice dialog (kM1NoticeROW).
      {"m1NoticeRowTitle", IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_TITLE},
      {"m1NoticeRowDescription1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_DESCRIPTION_1},
      {"m1NoticeRowDescription2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_DESCRIPTION_2},
      {"m1NoticeRowDescription3",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_DESCRIPTION_3},
      {"m1NoticeRowDescription4",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_DESCRIPTION_4},
      {"m1NoticeRowAckButton", IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_ACK_BUTTON},
      {"m1NoticeRowSettingsButton",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_SETTINGS_BUTTON},
      {"m1NoticeRowLearnMoreExpandLabel",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_EXPAND_LABEL},
      {"m1NoticeRowLearnMoreHeading1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_HEADING_1},
      {"m1NoticeRowLearnMoreDescription1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_1},
      {"m1NoticeRowLearnMoreBullet1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_BULLET_1},
      {"m1NoticeRowLearnMoreBullet2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_BULLET_2},
      {"m1NoticeRowLearnMoreDescription2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_2},
      {"m1NoticeRowLearnMoreDescription3",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_3},
      {"m1NoticeRowLearnMoreHeading2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_HEADING_2},
      {"m1NoticeRowLearnMoreDescription4",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_4},
      {"m1NoticeRowLearnMoreDescription5",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_5},

      // Strings for the ROW notice with the Ads API UX Enhancement.
      {"m1NoticeLearnMoreBullet2V2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_BULLET_2_V2},
      {"m1NoticeRowLearnMoreDesription2V2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_2_V2},
      {"m1NoticeRowLearnMoreHeading3",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_HEADING_3},
      {"m1NoticeRowLastText", IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LAST_TEXT},

      // Strings for the EEA Consent with the Ad Topics Content Parity.
      {"m1ConsentDescription1ContentParity",
       IDS_PRIVACY_SANDBOX_M1_CONSENT_DESCRIPTION_1_CONTENT_PARITY},

      // Strings for the restricted notice dialog (kM1NoticeRestricted).
      {"m1NoticeRestrictedTitle",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_RESTRICTED_TITLE},
      {"m1NoticeRestrictedDescription1",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_RESTRICTED_DESCRIPTION_1},
      {"m1NoticeRestrictedDescription2",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_RESTRICTED_DESCRIPTION_2},
      {"m1NoticeRestrictedDescription3",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_RESTRICTED_DESCRIPTION_3},
      {"m1NoticeRestrictedAckButton",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_RESTRICTED_ACK_BUTTON},
      {"m1NoticeRestrictedSettingsButton",
       IDS_PRIVACY_SANDBOX_M1_NOTICE_RESTRICTED_SETTINGS_BUTTON},
      // Strings for the privacy policy page.
      {"privacyPolicyBackButtonAria",
       IDS_PRIVACY_SANDBOX_PRIVACY_POLICY_BACK_BUTTON},
      // Shared for all dialogs.
      {"m1DialogMoreButton", IDS_PRIVACY_SANDBOX_M1_DIALOG_MORE_BUTTON}};

  source->AddLocalizedStrings(kStrings);

  // Adding Privacy Policy link to EEA Consent
  source->AddString(
      "m1ConsentLearnMorePrivacyPolicyLink",
      l10n_util::GetStringFUTF16(
          IDS_PRIVACY_SANDBOX_M1_NOTICE_LEARN_MORE_V2_DESKTOP, kPrivacyPolicyId,
          l10n_util::GetStringUTF16(
              IDS_PRIVACY_SANDBOX_M1_NOTICE_LEARN_MORE_V2_DESKTOP_ARIA_DESCRIPTION),
          kPrivacyPolicyFunc));

  source->AddBoolean("isPrivacySandboxAdsApiUxEnhancementsEnabled",
                     base::FeatureList::IsEnabled(
                         privacy_sandbox::kPrivacySandboxAdsApiUxEnhancements));

  // Adding Privacy Policy Link to EEA Consent for V2 with Ads API UX
  // Enhancements.
  source->AddString(
      "m1ConsentLearnmoreBullet2Description",
      l10n_util::GetStringFUTF16(
          IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_2_DESCRIPTION,
          kPrivacyPolicyIdV2,
          l10n_util::GetStringUTF16(
              IDS_PRIVACY_SANDBOX_M1_NOTICE_LEARN_MORE_V2_DESKTOP_ARIA_DESCRIPTION),
          kPrivacyPolicyFunc));

  // Adding Privacy Policy Link to EEA Notice for V2 with Ads API UX
  // Enhancements.
  source->AddString(
      "m1NoticeEEASiteSuggestedAdsLearnMoreBullet1Description",
      l10n_util::GetStringFUTF16(
          IDS_PRIVACY_SANDBOX_M1_NOTICE_EEA_SITE_SUGGESTED_ADS_LEARN_MORE_BULLET_1_DESCRIPTION,
          kPrivacyPolicyIdV2,
          l10n_util::GetStringUTF16(
              IDS_PRIVACY_SANDBOX_M1_NOTICE_LEARN_MORE_V2_DESKTOP_ARIA_DESCRIPTION),
          kPrivacyPolicyFunc));

  // Adding Privacy Policy Link to ROW Notice for V2 with Ads API UX
  // Enhancements.
  source->AddString(
      "m1NoticeRowLearnMoreDescription5V2",
      l10n_util::GetStringFUTF16(
          IDS_PRIVACY_SANDBOX_M1_NOTICE_ROW_LEARN_MORE_DESCRIPTION_5_V2,
          kPrivacyPolicyIdV2,
          l10n_util::GetStringUTF16(
              IDS_PRIVACY_SANDBOX_M1_NOTICE_LEARN_MORE_V2_DESKTOP_ARIA_DESCRIPTION),
          kPrivacyPolicyFunc));

  // Adding Privacy Policy Link to EEA Consent for Ad Topics Content Parity
  source->AddString(
      "m1ConsentLearnMoreBullet2DescriptionContentParity",
      l10n_util::GetStringFUTF16(
          IDS_PRIVACY_SANDBOX_M1_CONSENT_LEARN_MORE_BULLET_2_DESCRIPTION_CONTENT_PARITY,
          kPrivacyPolicyIdV3,
          l10n_util::GetStringUTF16(
              IDS_PRIVACY_SANDBOX_M1_NOTICE_LEARN_MORE_V2_DESKTOP_ARIA_DESCRIPTION),
          kPrivacyPolicyFunc));

  const GURL& url = web_ui->GetWebContents()->GetVisibleURL();
  if (url.query().find("debug") != std::string::npos) {
    // Not intended to be hooked to anything. The dialog will not initialize
    // it so we force it here.
    InitializeForDebug(source);
  }
}

PrivacySandboxDialogUI::~PrivacySandboxDialogUI() = default;

void PrivacySandboxDialogUI::Initialize(
    Profile* profile,
    base::RepeatingCallback<void(
        PrivacySandboxService::AdsDialogCallbackNoArgsEvents)> dialog_callback,
    base::OnceCallback<void(int)> resize_callback,
    PrivacySandboxService::PromptType prompt_type) {
  base::Value::Dict update;
  content::WebUIDataSource::Update(
      profile, chrome::kChromeUIPrivacySandboxDialogHost, std::move(update));
  auto handler = std::make_unique<PrivacySandboxDialogHandler>(
      std::move(dialog_callback), std::move(resize_callback), prompt_type);
  web_ui()->AddMessageHandler(std::move(handler));
}

void PrivacySandboxDialogUI::InitializeForDebug(
    content::WebUIDataSource* source) {
  auto handler = std::make_unique<PrivacySandboxDialogHandler>(
      base::DoNothing(), base::DoNothing(),
      PrivacySandboxService::PromptType::kNone);
  source->AddBoolean("isConsent", false);
  web_ui()->AddMessageHandler(std::move(handler));
}

WEB_UI_CONTROLLER_TYPE_IMPL(PrivacySandboxDialogUI)
