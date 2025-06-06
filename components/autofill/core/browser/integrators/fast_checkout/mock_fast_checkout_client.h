// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_FAST_CHECKOUT_MOCK_FAST_CHECKOUT_CLIENT_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_FAST_CHECKOUT_MOCK_FAST_CHECKOUT_CLIENT_H_

#include "components/autofill/core/browser/integrators/fast_checkout/fast_checkout_client.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill {

class MockFastCheckoutClient : public FastCheckoutClient {
 public:
  MockFastCheckoutClient();
  ~MockFastCheckoutClient() override;

  MOCK_METHOD(bool,
              TryToStart,
              (const GURL& url,
               const FormData& form,
               const FormFieldData& field,
               base::WeakPtr<AutofillManager> autofill_manager),
              (override));
  MOCK_METHOD(void, Stop, (bool allow_further_runs), (override));
  MOCK_METHOD(bool, IsRunning, (), (const override));
  MOCK_METHOD(bool, IsShowing, (), (const override));
  MOCK_METHOD(void,
              OnNavigation,
              (const GURL& url, bool is_cart_or_checkout_url),
              (override));
  MOCK_METHOD(FastCheckoutTriggerOutcome,
              CanRun,
              (const FormData&, const FormFieldData&, const AutofillManager&),
              (const override));
  MOCK_METHOD(bool, IsNotShownYet, (), (const override));
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_INTEGRATORS_FAST_CHECKOUT_MOCK_FAST_CHECKOUT_CLIENT_H_
