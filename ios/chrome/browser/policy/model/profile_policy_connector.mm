// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/policy/model/profile_policy_connector.h"

#import "components/policy/core/common/cloud/cloud_policy_store.h"
#import "components/policy/core/common/local_test_policy_provider.h"
#import "components/policy/core/common/policy_service_impl.h"
#import "components/policy/core/common/schema_registry.h"
#import "ios/chrome/browser/policy/model/browser_policy_connector_ios.h"

ProfilePolicyConnector::ProfilePolicyConnector() = default;
ProfilePolicyConnector::~ProfilePolicyConnector() = default;

void ProfilePolicyConnector::Init(
    policy::SchemaRegistry* schema_registry,
    BrowserPolicyConnectorIOS* browser_policy_connector,
    policy::ConfigurationPolicyProvider* user_policy_provider,
    policy::CloudPolicyStore* policy_store) {
  schema_registry_ = schema_registry;
  policy_store_ = policy_store;

  // The object returned by GetPlatformConnector() may or may not be in the list
  // returned by GetPolicyProviders().  Explicitly add it to `policy_providers_`
  // here in case it will not be added by the loop below (for example, this
  // could happen if the platform provider is overridden for testing)..
  policy::ConfigurationPolicyProvider* platform_provider =
      browser_policy_connector->GetPlatformProvider();
  policy_providers_.push_back(platform_provider);

  for (auto* provider : browser_policy_connector->GetPolicyProviders()) {
    // Skip the platform provider since it was already handled above. Do not
    // reorder any of the remaining providers because the ordering in this list
    // determines the precedence of the providers.
    if (provider != platform_provider) {
      policy_providers_.push_back(provider);
    }
  }

  // Put `user_policy_provider` at the end of the list because it is the
  // provider with the lowest priority.
  if (user_policy_provider) {
    policy_providers_.push_back(user_policy_provider);
  }

  if (browser_policy_connector->local_test_policy_provider()) {
    local_test_policy_provider_ =
        browser_policy_connector->local_test_policy_provider();
  }

  policy_service_ = std::make_unique<policy::PolicyServiceImpl>(
      policy_providers_, policy::PolicyServiceImpl::ScopeForMetrics::kMachine);
}

bool ProfilePolicyConnector::IsManaged() const {
  if (policy_store_) {
    return policy_store_->is_managed();
  }
  return false;
}

void ProfilePolicyConnector::UseLocalTestPolicyProvider() {
  for (policy::ConfigurationPolicyProvider* provider : policy_providers_) {
    provider->set_active(false);
  }

  if (local_test_policy_provider_) {
    local_test_policy_provider_->set_active(true);
  }
  policy_service_->RefreshPolicies(base::DoNothing(),
                                   policy::PolicyFetchReason::kTest);
}

void ProfilePolicyConnector::RevertUseLocalTestPolicyProvider() {
  for (policy::ConfigurationPolicyProvider* provider : policy_providers_) {
    provider->set_active(true);
  }

  if (local_test_policy_provider_) {
    local_test_policy_provider_->set_active(false);
    local_test_policy_provider_->ClearPolicies();
  }
  policy_service_->RefreshPolicies(base::DoNothing(),
                                   policy::PolicyFetchReason::kTest);
}

bool ProfilePolicyConnector::IsUsingLocalTestPolicyProvider() const {
  return local_test_policy_provider_ &&
         local_test_policy_provider_->is_active();
}

void ProfilePolicyConnector::Shutdown() {}
