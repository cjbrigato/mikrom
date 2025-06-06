// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_SECURE_PAYMENT_CONFIRMATION_APP_FACTORY_H_
#define COMPONENTS_PAYMENTS_CONTENT_SECURE_PAYMENT_CONFIRMATION_APP_FACTORY_H_

#include <map>
#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/payments/content/payment_app_factory.h"
#include "components/webdata/common/web_data_service_consumer.h"

namespace payments {

#if BUILDFLAG(IS_ANDROID)
class BrowserBoundKeyStore;
#endif  // BUILDFLAG(IS_ANDROID)
struct SecurePaymentConfirmationCredential;

class SecurePaymentConfirmationAppFactory : public PaymentAppFactory,
                                            public WebDataServiceConsumer {
 public:
  SecurePaymentConfirmationAppFactory();
  ~SecurePaymentConfirmationAppFactory() override;

  SecurePaymentConfirmationAppFactory(
      const SecurePaymentConfirmationAppFactory& other) = delete;
  SecurePaymentConfirmationAppFactory& operator=(
      const SecurePaymentConfirmationAppFactory& other) = delete;

  // PaymentAppFactory:
  void Create(base::WeakPtr<Delegate> delegate) override;

  // WebDataServiceConsumer:
  void OnWebDataServiceRequestDone(
      WebDataServiceBase::Handle handle,
      std::unique_ptr<WDTypedResult> result) override;

#if BUILDFLAG(IS_ANDROID)
  void SetBrowserBoundKeyStoreForTesting(
      scoped_refptr<BrowserBoundKeyStore> key_store);
#endif  // BUILDFLAG(IS_ANDROID)

 private:
  struct Request;

  void OnIsUserVerifyingPlatformAuthenticatorAvailable(
      std::unique_ptr<Request> request,
      bool is_available);

  // On platforms where we have credential-store level support for retrieving
  // credentials (i.e., rather than using the user profile database), this
  // callback will be called with the retrieved and matching credential ids.
  //
  // |relying_party_id| and |matching_credentials| are always std::move'd in,
  // and so are not const-ref.
  void OnGetMatchingCredentialIdsFromStore(
      std::unique_ptr<Request> request,
      std::string relying_party_id,
      std::vector<std::vector<uint8_t>> matching_credentials);

  void OnRetrievedCredentials(
      std::unique_ptr<Request> request,
      std::vector<std::unique_ptr<SecurePaymentConfirmationCredential>>
          credentials);

  void OnRetrievedBrowserBoundKeyId(
      std::unique_ptr<Request> request,
      std::optional<std::vector<uint8_t>> maybe_browser_bound_key_id);

  // Called once all icons are downloaded and their respective SkBitmaps have
  // been set into the Request.
  void DidDownloadAllIcons(std::unique_ptr<Request> request);

  std::map<WebDataServiceBase::Handle, std::unique_ptr<Request>> requests_;
#if BUILDFLAG(IS_ANDROID)
  scoped_refptr<BrowserBoundKeyStore> browser_bound_key_store_for_testing_;
#endif  // BUILDFLAG(IS_ANDROID)
  base::WeakPtrFactory<SecurePaymentConfirmationAppFactory> weak_ptr_factory_{
      this};
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_SECURE_PAYMENT_CONFIRMATION_APP_FACTORY_H_
