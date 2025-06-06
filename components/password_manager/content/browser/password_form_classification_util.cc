// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/content/browser/password_form_classification_util.h"

#include <ranges>

#include "base/containers/to_vector.h"
#include "components/autofill/content/browser/renderer_forms_from_browser_form.h"
#include "components/autofill/core/browser/foundations/autofill_manager.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/unique_ids.h"
#include "components/password_manager/core/browser/form_parsing/form_data_parser.h"
#include "components/password_manager/core/browser/form_parsing/password_field_prediction.h"
#include "components/password_manager/core/browser/password_form.h"
#include "content/public/browser/global_routing_id.h"

namespace password_manager {

autofill::PasswordFormClassification ClassifyAsPasswordForm(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    autofill::FieldGlobalId field_id) {
  std::optional<autofill::RendererForms> renderer_forms =
      autofill::RendererFormsFromBrowserForm(manager, form_id);
  if (!renderer_forms.has_value()) {
    return {};
  }

  // Find the form to which `field_id` belongs.
  auto it = std::ranges::find_if(
      renderer_forms.value(),
      [field_id](
          const std::pair<autofill::FormData, content::GlobalRenderFrameHostId>&
              form_rfh_pair) {
        const autofill::FormData& form = form_rfh_pair.first;
        return std::ranges::find(form.fields(), field_id,
                                 &autofill::FormFieldData::global_id) !=
               form.fields().end();
      });
  if (it == renderer_forms.value().end()) {
    return {};
  }

  // The driver id is irrelevant here because it would only be used by password
  // manager logic that handles the `PasswordForm` returned by the parser.
  std::vector<autofill::FieldGlobalId> field_ids =
      base::ToVector(it->first.fields(), &autofill::FormFieldData::global_id);
  return ClassifyAsPasswordForm(
      it->first, ConvertToFormPredictions(
                     /*driver_id=*/0, it->first,
                     manager.GetServerPredictionsForForm(form_id, field_ids)));
}

}  // namespace password_manager
