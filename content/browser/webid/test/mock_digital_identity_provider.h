// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WEBID_TEST_MOCK_DIGITAL_IDENTITY_PROVIDER_H_
#define CONTENT_BROWSER_WEBID_TEST_MOCK_DIGITAL_IDENTITY_PROVIDER_H_

#include "base/values.h"
#include "content/public/browser/digital_identity_provider.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace content {

class MockDigitalIdentityProvider : public DigitalIdentityProvider {
 public:
  MockDigitalIdentityProvider();

  ~MockDigitalIdentityProvider() override;

  MockDigitalIdentityProvider(const MockDigitalIdentityProvider&) = delete;
  MockDigitalIdentityProvider& operator=(const MockDigitalIdentityProvider&) =
      delete;

  MOCK_METHOD(bool,
              IsLastCommittedOriginLowRisk,
              (content::RenderFrameHost & render_frame_host),
              (const override));
  MOCK_METHOD(DigitalIdentityInterstitialAbortCallback,
              ShowDigitalIdentityInterstitial,
              (WebContents & web_contents,
               const url::Origin& origin,
               DigitalIdentityInterstitialType interstitial_type,
               DigitalIdentityInterstitialCallback callback),
              (override));
  MOCK_METHOD(void,
              Get,
              (WebContents*,
               const url::Origin& origin,
               base::ValueView request,
               DigitalIdentityCallback),
              (override));
  MOCK_METHOD(void,
              Create,
              (WebContents*,
               const url::Origin& origin,
               base::ValueView request,
               DigitalIdentityCallback),
              (override));
};

}  // namespace content

#endif  // CONTENT_BROWSER_WEBID_TEST_MOCK_DIGITAL_IDENTITY_PROVIDER_H_
