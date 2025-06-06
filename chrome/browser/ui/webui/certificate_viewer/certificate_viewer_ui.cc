// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/certificate_viewer/certificate_viewer_ui.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/certificate_viewer/certificate_viewer_webui.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/certificate_viewer_resources.h"
#include "chrome/grit/certificate_viewer_resources_map.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/web_dialogs/web_dialog_delegate.h"
#include "ui/webui/webui_util.h"

namespace {

void CreateAndAddWebUIDataSource(Profile* profile, const std::string& host) {
  content::WebUIDataSource* html_source =
      content::WebUIDataSource::CreateAndAdd(profile, host);

  static constexpr webui::LocalizedString kStrings[] = {
      {"general", IDS_CERT_INFO_GENERAL_TAB_LABEL},
      {"details", IDS_CERT_INFO_DETAILS_TAB_LABEL},
      {"modifications", IDS_CERT_INFO_MODIFICATIONS_TAB_LABEL},
      {"close", IDS_CLOSE},
      {"export", IDS_CERT_DETAILS_EXPORT_CERTIFICATE},
      {"exportA11yLabel", IDS_CERT_DETAILS_EXPORT_CERTIFICATE_A11Y_LABEL},
      {"issuedTo", IDS_CERT_INFO_SUBJECT_GROUP},
      {"issuedBy", IDS_CERT_INFO_ISSUER_GROUP},
      {"cn", IDS_CERT_INFO_COMMON_NAME_LABEL},
      {"o", IDS_CERT_INFO_ORGANIZATION_LABEL},
      {"ou", IDS_CERT_INFO_ORGANIZATIONAL_UNIT_LABEL},
      {"validity", IDS_CERT_INFO_VALIDITY_GROUP},
      {"issuedOn", IDS_CERT_INFO_ISSUED_ON_LABEL},
      {"expiresOn", IDS_CERT_INFO_EXPIRES_ON_LABEL},
      {"fingerprints", IDS_CERT_INFO_FINGERPRINTS_GROUP},
      {"sha256", IDS_CERT_INFO_SHA256_FINGERPRINT_LABEL},
      {"spki", IDS_CERT_INFO_SHA256_SPKI_FINGERPRINT_LABEL},
      {"hierarchy", IDS_CERT_DETAILS_CERTIFICATE_HIERARCHY_LABEL},
      {"certFields", IDS_CERT_DETAILS_CERTIFICATE_FIELDS_LABEL},
      {"certFieldVal", IDS_CERT_DETAILS_CERTIFICATE_FIELD_VALUE_LABEL},
      {"certError", IDS_CERT_DUMP_ERROR},
      {"trust", IDS_CERT_INFO_TRUST_LABEL},
      {"trustState", IDS_CERT_INFO_TRUST_STATE_LABEL},
      {"trustStateDistrusted", IDS_CERT_INFO_TRUST_STATE_DISTRUSTED},
      {"trustStateHint", IDS_CERT_INFO_TRUST_STATE_HINT},
      {"trustStateTrusted", IDS_CERT_INFO_TRUST_STATE_TRUSTED},
      {"constraints", IDS_CERT_INFO_CONSTRAINTS_LABEL},
      {"add", IDS_CERT_INFO_ADD_CONSTRAINTS_BUTTON_LABEL},
      {"addConstraints", IDS_CERT_INFO_ADD_CONSTRAINTS_LABEL},
      {"addConstraintsPlaceholder", IDS_CERT_INFO_ADD_CONSTRAINTS_PLACEHOLDER},
      {"addConstraintErrorMessage", IDS_CERT_INFO_ADD_CONSTRAINT_ERROR_MESSAGE},
      {"deleteConstraintErrorMessage",
       IDS_CERT_INFO_DELETE_CONSTRAINT_ERROR_MESSAGE},
      {"trustStateErrorMessage", IDS_CERT_INFO_TRUST_STATE_ERROR_MESSAGE},
  };
  html_source->AddLocalizedStrings(kStrings);

  webui::SetupWebUIDataSource(html_source, kCertificateViewerResources,
                              IDR_CERTIFICATE_VIEWER_CERTIFICATE_VIEWER_HTML);
}

}  // namespace

CertificateViewerUI::CertificateViewerUI(content::WebUI* web_ui)
    : ConstrainedWebDialogUI(web_ui) {
  // Set up the chrome://view-cert source.
  CreateAndAddWebUIDataSource(Profile::FromWebUI(web_ui),
                              chrome::kChromeUICertificateViewerHost);
}

CertificateViewerUI::~CertificateViewerUI() = default;
