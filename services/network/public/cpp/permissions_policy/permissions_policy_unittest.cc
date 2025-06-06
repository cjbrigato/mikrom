// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/permissions_policy/permissions_policy.h"

#include <optional>
#include <unordered_set>

#include "base/containers/contains.h"
#include "base/strings/stringprintf.h"
#include "base/test/gtest_util.h"
#include "base/test/scoped_feature_list.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/permissions_policy/fenced_frame_permissions_policies.h"
#include "services/network/public/cpp/permissions_policy/origin_with_possible_wildcards.h"
#include "services/network/public/cpp/permissions_policy/permissions_policy_declaration.h"
#include "services/network/public/cpp/permissions_policy/permissions_policy_features.h"
#include "services/network/public/cpp/permissions_policy/permissions_policy_features_internal.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom-shared.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace network {

namespace {

const network::mojom::PermissionsPolicyFeature kDefaultOnFeature =
    network::mojom::PermissionsPolicyFeature::kDeferredFetchMinimal;

const network::mojom::PermissionsPolicyFeature kDefaultSelfFeature =
    network::mojom::PermissionsPolicyFeature::kAmbientLightSensor;

const network::mojom::PermissionsPolicyFeature kDefaultOffFeature =
    network::mojom::PermissionsPolicyFeature::kUnload;

// This feature is defined in mojo, but not present in the feature list.
const network::mojom::PermissionsPolicyFeature kUnavailableFeature =
    network::mojom::PermissionsPolicyFeature::kNotFound;

}  // namespace

class PermissionsPolicyTest : public testing::Test {
 protected:
  PermissionsPolicyTest()
      : feature_list_(
            {{kDefaultOnFeature,
              network::PermissionsPolicyFeatureDefault::EnableForAll},
             {kDefaultSelfFeature,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {kDefaultOffFeature,
              network::PermissionsPolicyFeatureDefault::EnableForNone},
             {network::mojom::PermissionsPolicyFeature::kBrowsingTopics,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {network::mojom::PermissionsPolicyFeature::kClientHintDPR,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {network::mojom::PermissionsPolicyFeature::kAttributionReporting,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {network::mojom::PermissionsPolicyFeature::kJoinAdInterestGroup,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {network::mojom::PermissionsPolicyFeature::kSharedStorage,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {network::mojom::PermissionsPolicyFeature::kSharedStorageSelectUrl,
              network::PermissionsPolicyFeatureDefault::EnableForSelf},
             {network::mojom::PermissionsPolicyFeature::kPrivateAggregation,
              network::PermissionsPolicyFeatureDefault::EnableForSelf}}) {}

  ~PermissionsPolicyTest() override = default;

  std::unique_ptr<PermissionsPolicy> CreateFromParentPolicy(
      const PermissionsPolicy* parent,
      network::ParsedPermissionsPolicy header_policy,
      const url::Origin& origin,
      bool headerless = false) {
    network::ParsedPermissionsPolicy empty_container_policy;
    return PermissionsPolicy::CreateFromParentPolicy(
        parent, header_policy, empty_container_policy, origin, feature_list_,
        headerless);
  }

  std::unique_ptr<PermissionsPolicy> CreateFromParsedPolicy(
      const network::ParsedPermissionsPolicy& parsed_policy,
      const url::Origin& origin,
      const std::optional<network::ParsedPermissionsPolicy>& base_policy =
          std::nullopt) {
    return PermissionsPolicy::CreateFromParsedPolicy(parsed_policy, base_policy,
                                                     origin, feature_list_);
  }

  std::unique_ptr<PermissionsPolicy> CreateFromParentWithFramePolicy(
      const PermissionsPolicy* parent,
      network::ParsedPermissionsPolicy header_policy,
      const network::ParsedPermissionsPolicy& frame_policy,
      const url::Origin& origin,
      bool headerless = false) {
    return PermissionsPolicy::CreateFromParentPolicy(
        parent, header_policy, frame_policy, origin, feature_list_, headerless);
  }

  std::unique_ptr<PermissionsPolicy> CreateFlexibleForFencedFrame(
      const PermissionsPolicy* parent,
      network::ParsedPermissionsPolicy header_policy,
      const url::Origin& origin) {
    network::ParsedPermissionsPolicy empty_container_policy;
    return PermissionsPolicy::CreateFlexibleForFencedFrame(
        parent, header_policy, empty_container_policy, origin, feature_list_);
  }

  std::unique_ptr<PermissionsPolicy> CreateFixedForFencedFrame(
      const url::Origin& origin,
      network::ParsedPermissionsPolicy header_policy,
      base::span<const network::mojom::PermissionsPolicyFeature>
          effective_enabled_permissions) {
    return PermissionsPolicy::CreateFixedForFencedFrame(
        origin, header_policy, feature_list_, effective_enabled_permissions);
  }

  bool PolicyContainsInheritedValue(
      const PermissionsPolicy* policy,
      network::mojom::PermissionsPolicyFeature feature) {
    return policy->inherited_policies_.Contains(feature);
  }

  url::Origin origin_a_ = url::Origin::Create(GURL("https://example.com/"));
  url::Origin origin_b_ = url::Origin::Create(GURL("https://example.net/"));
  url::Origin origin_c_ = url::Origin::Create(GURL("https://example.org/"));

 private:
  // Contains the list of controlled features, so that we are guaranteed to
  // have at least one of each kind of default behaviour represented.
  network::PermissionsPolicyFeatureList feature_list_;
};

TEST_F(PermissionsPolicyTest, TestInitialPolicy) {
  // +-------------+
  // |(1)Origin A  |
  // |No Policy    |
  // +-------------+
  // Default-on and top-level-only features should be enabled in top-level
  // frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest, TestCanEnableOffFeatureWithAll) {
  // +-----------------------------------+
  // |(1)Origin A                        |
  // |Permissions-Policy: default-off=*  |
  // +-----------------------------------+
  // Default-off feature be enabled with header policy *.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest, TestCanEnableOffFeatureWithSelf) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |Permissions-Policy: default-off=self  |
  // +--------------------------------------+
  // Default-off feature be enabled with header policy self.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_a_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest, TestInitialSameOriginChildPolicy) {
  // +-----------------+
  // |(1)Origin A      |
  // |No Policy        |
  // | +-------------+ |
  // | |(2)Origin A  | |
  // | |No Policy    | |
  // | +-------------+ |
  // +-----------------+
  // Default-on and Default-self features should be enabled in a same-origin
  // child frame. Default-off feature should be disabled.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest, TestInitialCrossOriginChildPolicy) {
  // +-----------------+
  // |(1)Origin A      |
  // |No Policy        |
  // | +-------------+ |
  // | |(2)Origin B  | |
  // | |No Policy    | |
  // | +-------------+ |
  // +-----------------+
  // Default-on features should be enabled in child frame. Default-self and
  // Default-off feature should be disabled.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest, TestCrossOriginChildCannotEnableFeature) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |No Policy                                 |
  // | +--------------------------------------+ |
  // | |(2) Origin B                          | |
  // | |Permissions-Policy: default-self=self | |
  // | +--------------------------------------+ |
  // +------------------------------------------+
  // Default-self feature should be disabled in cross origin frame, even if no
  // policy was specified in the parent frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultSelfFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_b_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_b_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestSameOriginChildCannotEnableOffFeature) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |No Policy                                 |
  // | +--------------------------------------+ |
  // | |(2) Origin A                          | |
  // | |Permissions-Policy: default-off=*     | |
  // | +--------------------------------------+ |
  // +------------------------------------------+
  // Default-off feature should be disabled in same origin frame, if no
  // policy was specified in the parent frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOffFeature));

  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest,
       TestSameOriginChildWithParentEnabledCannotEnableOffFeature) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-off=*         |
  // | +--------------------------------------+ |
  // | |(2) Origin A                          | |
  // | |No Policy                             | |
  // | +--------------------------------------+ |
  // +------------------------------------------+
  // Default-off feature should be disabled in same origin subframe, if no
  // policy was specified in the subframe.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  ASSERT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest,
       TestSameOriginChildWithParentEnabledCannotEnableOffFeatureWithoutAllow) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-off=*         |
  // | +--------------------------------------+ |
  // | |(2) Origin A                          | |
  // | |Permissions-Policy: default-off=*     | |
  // | +--------------------------------------+ |
  // | +--------------------------------------+ |
  // | |(3) Origin B                          | |
  // | |Permissions-Policy: default-off=*     | |
  // | +--------------------------------------+ |
  // +------------------------------------------+
  // Default-off feature should be disabled in same origin subframe, if no
  // iframe allow is present.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  ASSERT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest,
       TestSameOriginChildWithParentEnabledCanEnableOffFeatureWithAllow) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Permissions-Policy: default-off=self           |
  // | <iframe allow="default-off OriginA OriginB">  |
  // | +--------------------------------------+      |
  // | |(2) Origin A                          |      |
  // | |Permissions-Policy: default-off=self |       |
  // | +--------------------------------------+      |
  // | +--------------------------------------+      |
  // | |(3) Origin B                          |      |
  // | |Permissions-Policy: default-off=self |       |
  // | +--------------------------------------+      |
  // +-----------------------------------------------+
  // Default-off feature should be enabled in same origin subframe, if a
  // self policy was specified in both subframe and main frame and an iframe
  // allow is present for that origin. It should not be enabled in a
  // cross-origin subframe.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_a_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  ASSERT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultOffFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_a_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false},
       {kDefaultOffFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentWithFramePolicy(policy1.get(),
                                      {{{kDefaultOffFeature,
                                         /*allowed_origins=*/{},
                                         /*self_if_matches=*/origin_a_,
                                         /*matches_all_origins=*/false,
                                         /*matches_opaque_src=*/false}}},
                                      frame_policy, origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOffFeature));
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentWithFramePolicy(policy1.get(),
                                      {{{kDefaultOffFeature,
                                         /*allowed_origins=*/{},
                                         /*self_if_matches=*/origin_b_,
                                         /*matches_all_origins=*/false,
                                         /*matches_opaque_src=*/false}}},
                                      frame_policy, origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest,
       TestSameOriginChildWithParentEnabledCanAllowHeaderless) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Permissions-Policy: default-off=self           |
  // | <iframe allow="default-off OriginA">          |
  // | +--------------------------------------+      |
  // | |(2) Origin A                          |      |
  // | |Headerless                            |      |
  // | +--------------------------------------+      |
  // | <iframe>                                      |
  // | +--------------------------------------+      |
  // | |(3) Origin A                          |      |
  // | |Headerless                            |      |
  // | +--------------------------------------+      |
  // +-----------------------------------------------+
  // Features enabled in the parent should be able to be enabled in a headerless
  // subframe.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultOffFeature,
         /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  ASSERT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));

  {
    network::ParsedPermissionsPolicy frame_policy = {{
        {kDefaultOffFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false},
    }};

    std::unique_ptr<PermissionsPolicy> policy2 =
        CreateFromParentWithFramePolicy(policy1.get(), /*header_policy=*/{},
                                        frame_policy, origin_a_, true);
    EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOffFeature));
    EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
    EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  }
  {
    std::unique_ptr<PermissionsPolicy> policy2 =
        CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
    EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
    EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
    EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  }
}

TEST_F(PermissionsPolicyTest,
       TestSameOriginChildWithParentDisabledCannotAllowHeaderless) {
  // +--------------------------------------------------------------+
  // |(1) Origin A                                                  |
  // |Permissions-Policy: default-on=OriginB default-self=OriginB   |
  // | <iframe allow="default-off *; default-on *; default-self *"> |
  // | +--------------------------------------+                     |
  // | |(2) Origin A                          |                     |
  // | |Headerless                            |                     |
  // | +--------------------------------------+                     |
  // +--------------------------------------------------------------+
  // Features disabled in the parent should not be enabled in a headerless
  // subframe.
  network::ParsedPermissionsPolicy header_policy = {{
      {kDefaultOnFeature, /*allowed_origins=*/
       {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_b_,
           /*has_subdomain_wildcard=*/false)},
       /*self_if_matches=*/std::nullopt,
       /*matches_all_origins=*/false,
       /*matches_opaque_src=*/false},
      {kDefaultSelfFeature, /*allowed_origins=*/
       {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_b_,
           /*has_subdomain_wildcard=*/false)},
       /*self_if_matches=*/std::nullopt,
       /*matches_all_origins=*/false,
       /*matches_opaque_src=*/false},
  }};
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, header_policy, origin_a_);
  ASSERT_FALSE(policy1->IsFeatureEnabled(kDefaultOffFeature));

  {
    network::ParsedPermissionsPolicy frame_policy = {{
        {kDefaultOffFeature, /*allowed_origins=*/{},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/true,
         /*matches_opaque_src=*/false},
        {kDefaultOnFeature, /*allowed_origins=*/{},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/true,
         /*matches_opaque_src=*/false},
        {kDefaultSelfFeature, /*allowed_origins=*/{},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/true,
         /*matches_opaque_src=*/false},
    }};

    std::unique_ptr<PermissionsPolicy> policy2 =
        CreateFromParentWithFramePolicy(policy1.get(), /*header_policy=*/{},
                                        frame_policy, origin_a_, true);
    EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
    EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
    EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  }
}

TEST_F(PermissionsPolicyTest,
       TestCrossOriginChildWithParentEnabledCanEnableOffFeatureWithAllow) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-off=*         |
  // | <iframe allow="default-off OriginB">     |
  // | +--------------------------------------+ |
  // | |(2) Origin B                          | |
  // | |Permissions-Policy: default-off=self  | |
  // | +--------------------------------------+ |
  // +------------------------------------------+
  // Default-off feature should be enabled in cross origin subframe, if a
  // policy was specified in both frames and an iframe allow is present.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOffFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  ASSERT_TRUE(policy1->IsFeatureEnabled(kDefaultOffFeature));
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultOffFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentWithFramePolicy(policy1.get(),
                                      {{{kDefaultOffFeature,
                                         /*allowed_origins=*/{},
                                         /*self_if_matches=*/origin_b_,
                                         /*matches_all_origins=*/false,
                                         /*matches_opaque_src=*/false}}},
                                      frame_policy, origin_b_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(PermissionsPolicyTest, TestFrameSelfInheritance) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-self=self     |
  // | +-----------------+  +-----------------+ |
  // | |(2) Origin A     |  |(4) Origin B     | |
  // | |No Policy        |  |No Policy        | |
  // | | +-------------+ |  | +-------------+ | |
  // | | |(3)Origin A  | |  | |(5)Origin B  | | |
  // | | |No Policy    | |  | |No Policy    | | |
  // | | +-------------+ |  | +-------------+ | |
  // | +-----------------+  +-----------------+ |
  // +------------------------------------------+
  // Feature should be enabled at the top-level, and through the chain of
  // same-origin frames 2 and 3. It should be disabled in frames 4 and 5, as
  // they are at a different origin.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_a_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy5 =
      CreateFromParentPolicy(policy4.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy5->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestReflexiveFrameSelfInheritance) {
  // +--------------------------------------+
  // |(1) Origin A                          |
  // |Permissions-Policy: default-self=self |
  // | +-----------------+                  |
  // | |(2) Origin B     |                  |
  // | |No Policy        |                  |
  // | | +-------------+ |                  |
  // | | |(3)Origin A  | |                  |
  // | | |No Policy    | |                  |
  // | | +-------------+ |                  |
  // | +-----------------+                  |
  // +--------------------------------------+
  // Feature which is enabled at top-level should be disabled in frame 3, as
  // it is embedded by frame 2, for which the feature is not enabled.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_a_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestReflexiveFrameOriginAInheritance) {
  // +-------------------------------------------+
  // |(1) Origin A                               |
  // |Permissions-Policy: default-self="OriginA" |
  // | +-----------------+                       |
  // | |(2) Origin B     |                       |
  // | |No Policy        |                       |
  // | | +-------------+ |                       |
  // | | |(3)Origin A  | |                       |
  // | | |No Policy    | |                       |
  // | | +-------------+ |                       |
  // | +-----------------+                       |
  // +-------------------------------------------+
  // Feature which is enabled at top-level should be disabled in frame 3, as
  // it is embedded by frame 2, for which the feature is not enabled.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestSelectiveFrameInheritance) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-self="OriginB"|
  // | +-----------------+  +-----------------+ |
  // | |(2) Origin B     |  |(3) Origin C     | |
  // | |No Policy        |  |No Policy        | |
  // | |                 |  | +-------------+ | |
  // | |                 |  | |(4)Origin B  | | |
  // | |                 |  | |No Policy    | | |
  // | |                 |  | +-------------+ | |
  // | +-----------------+  +-----------------+ |
  // +------------------------------------------+
  // Feature should be disabled in all frames, even though the
  // header indicates Origin B, there is no container policy to explicitly
  // delegate to that origin, in either frame 2 or 4.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_c_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy3.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestSelectiveFrameInheritance2) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-self="OriginB"|
  // | <iframe allow="default-self OriginB">    |
  // | +-----------------+  +-----------------+ |
  // | |(2) Origin B     |  |(3) Origin C     | |
  // | |No Policy        |  |No Policy        | |
  // | |                 |  | +-------------+ | |
  // | |                 |  | |(4)Origin B  | | |
  // | |                 |  | |No Policy    | | |
  // | |                 |  | +-------------+ | |
  // | +-----------------+  +-----------------+ |
  // +------------------------------------------+
  // Feature should be enabled in second level Origin B frame, but disabled in
  // Frame 4, because it is embedded by frame 3, where the feature is not
  // enabled.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_c_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy3.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestPolicyCanBlockSelf) {
  // +----------------------------------+
  // |(1)Origin A                       |
  // |Permissions-Policy: default-on=() |
  // +----------------------------------+
  // Default-on feature should be disabled in top-level frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOnFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestParentPolicyBlocksSameOriginChildPolicy) {
  // +----------------------------------+
  // |(1)Origin A                       |
  // |Permissions-Policy: default-on=() |
  // | +-------------+                  |
  // | |(2)Origin A  |                  |
  // | |No Policy    |                  |
  // | +-------------+                  |
  // +----------------------------------+
  // Feature should be disabled in child frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOnFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestChildPolicyCanBlockSelf) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |No Policy                             |
  // | +----------------------------------+ |
  // | |(2)Origin B                       | |
  // | |Permissions-Policy: default-on=() | |
  // | +----------------------------------+ |
  // +--------------------------------------+
  // Default-on feature should be disabled by cross-origin child frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultOnFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_b_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestChildPolicyCanBlockChildren) {
  // +----------------------------------------+
  // |(1)Origin A                             |
  // |No Policy                               |
  // | +------------------------------------+ |
  // | |(2)Origin B                         | |
  // | |Permissions-Policy: default-on=self | |
  // | | +-------------+                    | |
  // | | |(3)Origin C  |                    | |
  // | | |No Policy    |                    | |
  // | | +-------------+                    | |
  // | +------------------------------------+ |
  // +----------------------------------------+
  // Default-on feature should be enabled in frames 1 and 2; disabled in frame
  // 3 by child frame policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultOnFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_b_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestParentPolicyBlocksCrossOriginChildPolicy) {
  // +----------------------------------+
  // |(1)Origin A                       |
  // |Permissions-Policy: default-on=() |
  // | +-------------+                  |
  // | |(2)Origin B  |                  |
  // | |No Policy    |                  |
  // | +-------------+                  |
  // +----------------------------------+
  // Default-on feature should be disabled in cross-origin child frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOnFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestEnableForAllOrigins) {
  // +----------------------------------+
  // |(1) Origin A                      |
  // |Permissions-Policy: default-self=*|
  // | +-----------------+              |
  // | |(2) Origin B     |              |
  // | |No Policy        |              |
  // | | +-------------+ |              |
  // | | |(3)Origin A  | |              |
  // | | |No Policy    | |              |
  // | | +-------------+ |              |
  // | +-----------------+              |
  // +----------------------------------+
  // Feature should be enabled in top level; disabled in frame 2 and 3.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestEnableForAllOriginsAndDelegate) {
  // +--------------------------------------+
  // |(1) Origin A                          |
  // |Permissions-Policy: default-self=*    |
  // |<iframe allow="default-self OriginB"> |
  // | +-----------------+                  |
  // | |(2) Origin B     |                  |
  // | |No Policy        |                  |
  // | | +-------------+ |                  |
  // | | |(3)Origin A  | |                  |
  // | | |No Policy    | |                  |
  // | | +-------------+ |                  |
  // | +-----------------+                  |
  // +--------------------------------------+
  // Feature should be enabled in top and second level; disabled in frame 3.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestDefaultOnStillNeedsSelf) {
  // +-----------------------------------------+
  // |(1) Origin A                             |
  // |Permissions-Policy: default-on="OriginB" |
  // | +-----------------------------------+   |
  // | |(2) Origin B                       |   |
  // | |No Policy                          |   |
  // | | +-------------+   +-------------+ |   |
  // | | |(3)Origin B  |   |(4)Origin C  | |   |
  // | | |No Policy    |   |No Policy    | |   |
  // | | +-------------+   +-------------+ |   |
  // | +-----------------------------------+   |
  // +-----------------------------------------+
  // Feature should be disabled in all frames.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultOnFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestDefaultOnEnablesForAllDescendants) {
  // +------------------------------------------------+
  // |(1) Origin A                                    |
  // |Permissions-Policy: default-on=(self "OriginB") |
  // | +-----------------------------------+          |
  // | |(2) Origin B                       |          |
  // | |No Policy                          |          |
  // | | +-------------+   +-------------+ |          |
  // | | |(3)Origin B  |   |(4)Origin C  | |          |
  // | | |No Policy    |   |No Policy    | |          |
  // | | +-------------+   +-------------+ |          |
  // | +-----------------------------------+          |
  // +------------------------------------------------+
  // Feature should be enabled in all frames.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultOnFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy4->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestDefaultSelfRequiresDelegation) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Permissions-Policy: default-self="OriginB"|
  // | +-----------------------------------+    |
  // | |(2) Origin B                       |    |
  // | |No Policy                          |    |
  // | | +-------------+   +-------------+ |    |
  // | | |(3)Origin B  |   |(4)Origin C  | |    |
  // | | |No Policy    |   |No Policy    | |    |
  // | | +-------------+   +-------------+ |    |
  // | +-----------------------------------+    |
  // +------------------------------------------+
  // Feature should be disabled in all frames.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestDefaultSelfRespectsSameOriginEmbedding) {
  // +--------------------------------------------------+
  // |(1) Origin A                                      |
  // |Permissions-Policy: default-self=(self "OriginB") |
  // |<iframe allow="default-self">                     |
  // | +-----------------------------------+            |
  // | |(2) Origin B                       |            |
  // | |No Policy                          |            |
  // | | +-------------+   +-------------+ |            |
  // | | |(3)Origin B  |   |(4)Origin C  | |            |
  // | | |No Policy    |   |No Policy    | |            |
  // | | +-------------+   +-------------+ |            |
  // | +-----------------------------------+            |
  // +--------------------------------------------------+
  // Feature should be disabled in frame 4; enabled in frames 1, 2 and 3.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_b_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestDelegationRequiredAtAllLevels) {
  // +------------------------------------+
  // |(1) Origin A                        |
  // |<iframe allow="default-self *">     |
  // | +--------------------------------+ |
  // | |(2) Origin B                    | |
  // | |No Policy                       | |
  // | | +-------------+                | |
  // | | |(3)Origin A  |                | |
  // | | |No Policy    |                | |
  // | | +-------------+                | |
  // | +--------------------------------+ |
  // +------------------------------------+
  // Feature should be enabled in frames 1 and 2. Feature is not enabled in
  // frame 3, even though it is the same origin as the top-level, because it is
  // not explicitly delegated.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestBlockedFrameCannotReenable) {
  // +----------------------------------------+
  // |(1)Origin A                             |
  // |Permissions-Policy: default-self=self   |
  // | +----------------------------------+   |
  // | |(2)Origin B                       |   |
  // | |Permissions-Policy: default-self=*|   |
  // | | +-------------+  +-------------+ |   |
  // | | |(3)Origin A  |  |(4)Origin C  | |   |
  // | | |No Policy    |  |No Policy    | |   |
  // | | +-------------+  +-------------+ |   |
  // | +----------------------------------+   |
  // +----------------------------------------+
  // Feature should be enabled at the top level; disabled in all other frames.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature,
                                /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_a_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(),
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_a_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestEnabledFrameCanDelegate) {
  // +---------------------------------------------------+
  // |(1) Origin A                                       |
  // |No Policy                                          |
  // |<iframe allow="default-self">                      |
  // | +-----------------------------------------------+ |
  // | |(2) Origin B                                   | |
  // | |No Policy                                      | |
  // | |<iframe allow="default-self">                  | |
  // | | +-------------+                               | |
  // | | |(3)Origin C  |                               | |
  // | | |No Policy    |                               | |
  // | | +-------------+                               | |
  // | +-----------------------------------------------+ |
  // +---------------------------------------------------+
  // Feature should be enabled in all frames.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_b_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_c_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy2.get(), /*header_policy=*/{}, frame_policy2, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestEnabledFrameCanDelegateByDefault) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Permissions-Policy: default-on=(self "OriginB")|
  // | +--------------------+ +--------------------+ |
  // | |(2) Origin B        | | (4) Origin C       | |
  // | |No Policy           | | No Policy          | |
  // | | +-------------+    | |                    | |
  // | | |(3)Origin C  |    | |                    | |
  // | | |No Policy    |    | |                    | |
  // | | +-------------+    | |                    | |
  // | +--------------------+ +--------------------+ |
  // +-----------------------------------------------+
  // Feature should be enabled in frames 1, 2, and 3, and disabled in frame 4.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{
                                 {kDefaultOnFeature, /*allowed_origins=*/
                                  {*network::OriginWithPossibleWildcards::
                                       FromOriginAndWildcardsForTest(
                                           origin_b_,
                                           /*has_subdomain_wildcard=*/false)},
                                  /*self_if_matches=*/origin_a_,
                                  /*matches_all_origins=*/false,
                                  /*matches_opaque_src=*/false},
                             }},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(PermissionsPolicyTest, TestFeaturesDontDelegateByDefault) {
  // +-------------------------------------------------+
  // |(1) Origin A                                     |
  // |Permissions-Policy: default-self=(self "OriginB")|
  // | +--------------------+ +--------------------+   |
  // | |(2) Origin B        | | (4) Origin C       |   |
  // | |No Policy           | | No Policy          |   |
  // | | +-------------+    | |                    |   |
  // | | |(3)Origin C  |    | |                    |   |
  // | | |No Policy    |    | |                    |   |
  // | | +-------------+    | |                    |   |
  // | +--------------------+ +--------------------+   |
  // +-------------------------------------------------+
  // Feature should be enabled in frames 1 only. Without a container policy, the
  // feature is not delegated to any child frames.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, TestFeaturesAreIndependent) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |No Policy                                      |
  // |<iframe allow="default-self 'self' OriginB;    |
  // |               default-on 'self'>              |
  // | +-------------------------------------------+ |
  // | |(2) Origin B                               | |
  // | |No Policy                                  | |
  // | |<iframe allow="default-self 'self' OriginC;| |
  // | |               default-on 'self'>          | |
  // | | +-------------+                           | |
  // | | |(3)Origin C  |                           | |
  // | | |No Policy    |                           | |
  // | | +-------------+                           | |
  // | +-------------------------------------------+ |
  // +-----------------------------------------------+
  // Default-self feature should be enabled in all frames; Default-on feature
  // should be enabled in frame 1, and disabled in frames 2 and 3.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false},
        {kDefaultOnFeature,
         /*allowed_origins=*/{},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/origin_a_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false},
       {kDefaultOnFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_a_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_c_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/origin_a_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false},
       {kDefaultOnFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_b_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy2.get(), /*header_policy=*/{}, frame_policy2, origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOnFeature));
}

// Test frame policies

TEST_F(PermissionsPolicyTest, TestSimpleFramePolicy) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |No Policy                             |
  // |                                      |
  // |<iframe allow="default-self OriginB"> |
  // | +-------------+                      |
  // | |(2)Origin B  |                      |
  // | |No Policy    |                      |
  // | +-------------+                      |
  // +--------------------------------------+
  // Default-self feature should be enabled in cross-origin child frame because
  // permission was delegated through frame policy.
  // This is the same scenario as when the iframe is declared as
  // <iframe allow="default-self">
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
}

TEST_F(PermissionsPolicyTest, TestAllOriginFramePolicy) {
  // +--------------------------------+
  // |(1)Origin A                     |
  // |No Policy                       |
  // |                                |
  // |<iframe allow="default-self *"> |
  // | +-------------+                |
  // | |(2)Origin B  |                |
  // | |No Policy    |                |
  // | +-------------+                |
  // +--------------------------------+
  // Default-self feature should be enabled in cross-origin child frame because
  // permission was delegated through frame policy.
  // This is the same scenario that arises when the iframe is declared as
  // <iframe allowfullscreen>
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
}

TEST_F(PermissionsPolicyTest, TestFramePolicyCanBeFurtherDelegated) {
  // +------------------------------------------+
  // |(1)Origin A                               |
  // |No Policy                                 |
  // |                                          |
  // |<iframe allow="default-self OriginB">     |
  // | +--------------------------------------+ |
  // | |(2)Origin B                           | |
  // | |No Policy                             | |
  // | |                                      | |
  // | |<iframe allow="default-self OriginC"> | |
  // | | +-------------+                      | |
  // | | |(3)Origin C  |                      | |
  // | | |No Policy    |                      | |
  // | | +-------------+                      | |
  // | |                                      | |
  // | |<iframe> (No frame policy)            | |
  // | | +-------------+                      | |
  // | | |(4)Origin C  |                      | |
  // | | |No Policy    |                      | |
  // | | +-------------+                      | |
  // | +--------------------------------------+ |
  // +------------------------------------------+
  // Default-self feature should be enabled in cross-origin child frames 2 and
  // 3. Feature should be disabled in frame 4 because it was not further
  // delegated through frame policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy1 = {{
      {kDefaultSelfFeature, /*allowed_origins=*/
       {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_b_,
           /*has_subdomain_wildcard=*/false)},
       /*self_if_matches=*/std::nullopt,
       /*matches_all_origins=*/false,
       /*matches_opaque_src=*/false},
  }};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy1, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {{
      {kDefaultSelfFeature, /*allowed_origins=*/
       {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_c_,
           /*has_subdomain_wildcard=*/false)},
       /*self_if_matches=*/std::nullopt,
       /*matches_all_origins=*/false,
       /*matches_opaque_src=*/false},
  }};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy2.get(), /*header_policy=*/{}, frame_policy2, origin_c_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_TRUE(
      policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_c_));
  EXPECT_FALSE(
      policy4->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_c_));
}

TEST_F(PermissionsPolicyTest, TestDefaultOnCanBeDisabledByFramePolicy) {
  // +-----------------------------------+
  // |(1)Origin A                        |
  // |No Policy                          |
  // |                                   |
  // |<iframe allow="default-on 'none'"> |
  // | +-------------+                   |
  // | |(2)Origin A  |                   |
  // | |No Policy    |                   |
  // | +-------------+                   |
  // |                                   |
  // |<iframe allow="default-on 'none'"> |
  // | +-------------+                   |
  // | |(3)Origin B  |                   |
  // | |No Policy    |                   |
  // | +-------------+                   |
  // +-----------------------------------+
  // Default-on feature should be disabled in both same-origin and cross-origin
  // child frames because permission was removed through frame policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy1 = {
      {{kDefaultOnFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy1, origin_a_);
  network::ParsedPermissionsPolicy frame_policy2 = {
      {{kDefaultOnFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy2, origin_b_);
  EXPECT_TRUE(policy1->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(policy1->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_b_));
  EXPECT_TRUE(policy1->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_c_));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_b_));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_c_));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_b_));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_c_));
}

TEST_F(PermissionsPolicyTest, TestFramePolicyModifiesHeaderPolicy) {
  // +-------------------------------------------------+
  // |(1)Origin A                                      |
  // |Permissions-Policy: default-self=(self "OriginB")|
  // |                                                 |
  // |<iframe allow="default-self 'none'">             |
  // | +-----------------------------------------+     |
  // | |(2)Origin B                              |     |
  // | |No Policy                                |     |
  // | +-----------------------------------------+     |
  // |                                                 |
  // |<iframe allow="default-self 'none'">             |
  // | +-----------------------------------------+     |
  // | |(3)Origin B                              |     |
  // | |Permissions-Policy: default-self=self    |     |
  // | +-----------------------------------------+     |
  // +-------------------------------------------------+
  // Default-self feature should be disabled in both cross-origin child frames
  // by frame policy, even though the parent frame's header policy would
  // otherwise enable it. This is true regardless of the child frame's header
  // policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{
                                 {kDefaultSelfFeature, /*allowed_origins=*/
                                  {*network::OriginWithPossibleWildcards::
                                       FromOriginAndWildcardsForTest(
                                           origin_b_,
                                           /*has_subdomain_wildcard=*/false)},
                                  /*self_if_matches=*/origin_a_,
                                  /*matches_all_origins=*/false,
                                  /*matches_opaque_src=*/false},
                             }},
                             origin_a_);
  network::ParsedPermissionsPolicy frame_policy1 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy1, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {{
      {kDefaultSelfFeature, /*allowed_origins=*/{},
       /*self_if_matches=*/std::nullopt,
       /*matches_all_origins=*/false,
       /*matches_opaque_src=*/false},
  }};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy1.get(),
      {{
          {kDefaultSelfFeature, /*allowed_origins=*/{},
           /*self_if_matches=*/origin_b_,
           /*matches_all_origins=*/false,
           /*matches_opaque_src=*/false},
      }},
      frame_policy2, origin_b_);
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
}

TEST_F(PermissionsPolicyTest, TestCombineFrameAndHeaderPolicies) {
  // +-----------------------------------------+
  // |(1)Origin A                              |
  // |No Policy                                |
  // |                                         |
  // |<iframe allow="default-self OriginB">    |
  // | +-------------------------------------+ |
  // | |(2)Origin B                          | |
  // | |Permissions-Policy: default-self=*   | |
  // | |                                     | |
  // | |<iframe allow="default-self 'none'"> | |
  // | | +-------------+                     | |
  // | | |(3)Origin C  |                     | |
  // | | |No Policy    |                     | |
  // | | +-------------+                     | |
  // | |                                     | |
  // | |<iframe> (No frame policy)           | |
  // | | +-------------+                     | |
  // | | |(4)Origin C  |                     | |
  // | | |No Policy    |                     | |
  // | | +-------------+                     | |
  // | +-------------------------------------+ |
  // +-----------------------------------------+
  // Default-self feature should be enabled in cross-origin child frames 2 and
  // 4. Feature should be disabled in frame 3 by frame policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy1 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(),
      {{{kDefaultSelfFeature, /*allowed_origins=*/{},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/true,
         /*matches_opaque_src=*/false}}},
      frame_policy1, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy2.get(), /*header_policy=*/{}, frame_policy2, origin_c_);
  std::unique_ptr<PermissionsPolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), /*header_policy=*/{}, origin_c_);
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_c_));
}

TEST_F(PermissionsPolicyTest, TestFeatureDeclinedAtTopLevel) {
  // +-----------------------------------------+
  // |(1)Origin A                              |
  // |Permissions-Policy: default-self=()      |
  // |                                         |
  // |<iframe allow="default-self OriginB">    |
  // | +-------------------------------------+ |
  // | |(2)Origin B                          | |
  // | |No Policy                            | |
  // | +-------------------------------------+ |
  // |                                         |
  // |<iframe allow="default-self *">          |
  // | +-------------------------------------+ |
  // | |(3)Origin A                          | |
  // | |No Policy                            | |
  // | +-------------------------------------+ |
  // +-----------------------------------------+
  // Default-self feature should be disabled in all frames.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{
                                 {kDefaultSelfFeature, /*allowed_origins=*/{},
                                  /*self_if_matches=*/std::nullopt,
                                  /*matches_all_origins=*/false,
                                  /*matches_opaque_src=*/false},
                             }},
                             origin_a_);
  network::ParsedPermissionsPolicy frame_policy1 = {{
      {kDefaultSelfFeature, /*allowed_origins=*/
       {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_b_,
           /*has_subdomain_wildcard=*/false)},
       /*self_if_matches=*/std::nullopt,
       /*matches_all_origins=*/false,
       /*matches_opaque_src=*/false},
  }};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy1, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy2, origin_a_);
  EXPECT_FALSE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
}

TEST_F(PermissionsPolicyTest, TestFeatureDelegatedAndAllowed) {
  // +--------------------------------------------------+
  // |(1)Origin A                                       |
  // |Permissions-Policy: default-self=(self "OriginB") |
  // |                                                  |
  // |<iframe allow="default-self OriginA">             |
  // | +-------------------------------------+          |
  // | |(2)Origin B                          |          |
  // | |No Policy                            |          |
  // | +-------------------------------------+          |
  // |                                                  |
  // |<iframe allow="default-self OriginB">             |
  // | +-------------------------------------+          |
  // | |(3)Origin B                          |          |
  // | |No Policy                            |          |
  // | +-------------------------------------+          |
  // |                                                  |
  // |<iframe allow="default-self *">                   |
  // | +-------------------------------------+          |
  // | |(4)Origin B                          |          |
  // | |No Policy                            |          |
  // | +-------------------------------------+          |
  // +--------------------------------------------------+
  // Default-self feature should be disabled in frame 2, as the origin does not
  // match, and enabled in the remaining frames.
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  network::ParsedPermissionsPolicy frame_policy1 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_a_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy1, origin_b_);
  network::ParsedPermissionsPolicy frame_policy2 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy2, origin_b_);
  network::ParsedPermissionsPolicy frame_policy3 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy4 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy3, origin_b_);
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_TRUE(
      policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_TRUE(
      policy4->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
}

TEST_F(PermissionsPolicyTest, TestDefaultSandboxedFramePolicy) {
  // +------------------+
  // |(1)Origin A       |
  // |No Policy         |
  // |                  |
  // |<iframe sandbox>  |
  // | +-------------+  |
  // | |(2)Sandboxed |  |
  // | |No Policy    |  |
  // | +-------------+  |
  // +------------------+
  // Default-on feature should be enabled in child frame with opaque origin.
  // Other features should be disabled.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  url::Origin sandboxed_origin = url::Origin();
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentPolicy(
      policy1.get(), /*header_policy=*/{}, sandboxed_origin);
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, sandboxed_origin));
  EXPECT_FALSE(
      policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_FALSE(policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                  sandboxed_origin));
}

TEST_F(PermissionsPolicyTest, TestSandboxedFramePolicyForAllOrigins) {
  // +----------------------------------------+
  // |(1)Origin A                             |
  // |No Policy                               |
  // |                                        |
  // |<iframe sandbox allow="default-self *"> |
  // | +-------------+                        |
  // | |(2)Sandboxed |                        |
  // | |No Policy    |                        |
  // | +-------------+                        |
  // +----------------------------------------+
  // Default-self feature should be enabled in child frame with opaque origin,
  // only for that origin, because container policy matches all origins.
  // However, it will not pass that on to any other origin
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  url::Origin sandboxed_origin = url::Origin();
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, sandboxed_origin);
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, sandboxed_origin));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                 sandboxed_origin));
}

TEST_F(PermissionsPolicyTest, TestSandboxedFramePolicyForSelf) {
  // +-------------------------------------------+
  // |(1)Origin A                                |
  // |No Policy                                  |
  // |                                           |
  // |<iframe sandbox allow="default-self self"> |
  // | +-------------+                           |
  // | |(2)Sandboxed |                           |
  // | |No Policy    |                           |
  // | +-------------+                           |
  // +-------------------------------------------+
  // Default-self feature should be enabled in child frame with opaque origin,
  // only for that origin, because container policy matches all origins.
  // However, it will not pass that on to any other origin
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  url::Origin sandboxed_origin = url::Origin();
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/sandboxed_origin,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, sandboxed_origin);
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, sandboxed_origin));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                 sandboxed_origin));
}

TEST_F(PermissionsPolicyTest, TestSandboxedFramePolicyForOpaqueSrcOrigin) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |No Policy                             |
  // |                                      |
  // |<iframe sandbox allow="default-self"> |
  // | +-------------+                      |
  // | |(2)Sandboxed |                      |
  // | |No Policy    |                      |
  // | +-------------+                      |
  // +--------------------------------------+
  // Default-self feature should be enabled in child frame with opaque origin,
  // only for that origin, because container policy matches the opaque src.
  // However, it will not pass that on to any other origin
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  url::Origin sandboxed_origin = url::Origin();
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, sandboxed_origin);
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(
      policy2->IsFeatureEnabledForOrigin(kDefaultOnFeature, sandboxed_origin));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                 sandboxed_origin));
}

TEST_F(PermissionsPolicyTest, TestSandboxedFrameFromHeaderPolicy) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |Permissions-Policy: default-self=*    |
  // |                                      |
  // | +-------------+                      |
  // | |(2)Sandboxed |                      |
  // | |No Policy    |                      |
  // | +-------------+                      |
  // +--------------------------------------+
  // Default-self feature should not be enabled in child frame with opaque
  // origin, as it is cross-origin with its parent, and there is no container
  // policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  url::Origin sandboxed_origin = url::Origin();
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, sandboxed_origin);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                  sandboxed_origin));
}

TEST_F(PermissionsPolicyTest, TestSandboxedPolicyIsNotInherited) {
  // +----------------------------------------+
  // |(1)Origin A                             |
  // |No Policy                               |
  // |                                        |
  // |<iframe sandbox allow="default-self *"> |
  // | +------------------------------------+ |
  // | |(2)Sandboxed                        | |
  // | |No Policy                           | |
  // | |                                    | |
  // | | +-------------+                    | |
  // | | |(3)Sandboxed |                    | |
  // | | |No Policy    |                    | |
  // | | +-------------+                    | |
  // | +------------------------------------+ |
  // +----------------------------------------+
  // Default-on feature should be enabled in frame 3 with opaque origin, but all
  // other features should be disabled.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  url::Origin sandboxed_origin_1 = url::Origin();
  url::Origin sandboxed_origin_2 = url::Origin();
  network::ParsedPermissionsPolicy frame_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, sandboxed_origin_1);
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentPolicy(
      policy2.get(), /*header_policy=*/{}, sandboxed_origin_2);
  EXPECT_TRUE(policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature,
                                                 sandboxed_origin_1));
  EXPECT_TRUE(policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature,
                                                 sandboxed_origin_2));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(
      policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_FALSE(policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                  sandboxed_origin_1));
  EXPECT_FALSE(policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                  sandboxed_origin_2));
}

TEST_F(PermissionsPolicyTest, TestSandboxedPolicyCanBePropagated) {
  // +--------------------------------------------+
  // |(1)Origin A                                 |
  // |No Policy                                   |
  // |                                            |
  // |<iframe sandbox allow="default-self *">     |
  // | +----------------------------------------+ |
  // | |(2)Sandboxed                            | |
  // | |No Policy                               | |
  // | |                                        | |
  // | |<iframe sandbox allow="default-self *"> | |
  // | | +-------------+                        | |
  // | | |(3)Sandboxed |                        | |
  // | | |No Policy    |                        | |
  // | | +-------------+                        | |
  // | +----------------------------------------+ |
  // +--------------------------------------------+
  // Default-self feature should be enabled in child frame with opaque origin,
  // only for that origin, because container policy matches all origins.
  // However, it will not pass that on to any other origin
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  url::Origin sandboxed_origin_1 = origin_a_.DeriveNewOpaqueOrigin();
  url::Origin sandboxed_origin_2 = sandboxed_origin_1.DeriveNewOpaqueOrigin();
  network::ParsedPermissionsPolicy frame_policy_1 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy_1, sandboxed_origin_1);
  network::ParsedPermissionsPolicy frame_policy_2 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy2.get(), /*header_policy=*/{}, frame_policy_2, sandboxed_origin_2);
  EXPECT_TRUE(policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_TRUE(policy3->IsFeatureEnabledForOrigin(kDefaultOnFeature,
                                                 sandboxed_origin_2));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabledForOrigin(kDefaultSelfFeature,
                                                 sandboxed_origin_2));
}

TEST_F(PermissionsPolicyTest, TestUndefinedFeaturesInFramePolicy) {
  // +---------------------------------------------------+
  // |(1)Origin A                                        |
  // |No Policy                                          |
  // |                                                   |
  // |<iframe allow="nosuchfeature; unavailablefeature"> |
  // | +-------------+                                   |
  // | |(2)Origin B  |                                   |
  // | |No Policy    |                                   |
  // | +-------------+                                   |
  // +---------------------------------------------------+
  // A feature which is not in the declared feature list should be ignored if
  // present in a container policy.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  network::ParsedPermissionsPolicy frame_policy = {
      {{network::mojom::PermissionsPolicyFeature::kNotFound,
        /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/true},
       {kUnavailableFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/true}}};
  std::unique_ptr<PermissionsPolicy> policy2 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy, origin_b_);
  EXPECT_FALSE(PolicyContainsInheritedValue(
      policy1.get(), network::mojom::PermissionsPolicyFeature::kNotFound));
  EXPECT_FALSE(
      PolicyContainsInheritedValue(policy1.get(), kUnavailableFeature));
  EXPECT_FALSE(PolicyContainsInheritedValue(
      policy2.get(), network::mojom::PermissionsPolicyFeature::kNotFound));
  EXPECT_FALSE(
      PolicyContainsInheritedValue(policy2.get(), kUnavailableFeature));
}

// For script features that act on behalf of an origin that may be
// x-origin to the calling context, allow the request if and only if
// a subframe for the origin with allow=feature would be allowed.
TEST_F(PermissionsPolicyTest, ProposedTestIsFeatureEnabledForOriginDefaultAll) {
  const mojom::PermissionsPolicyFeature kJoinFeature =
      network::mojom::PermissionsPolicyFeature::kSharedStorage;

  {
    // In these tests, we have a  x-origin js method, `joinAdInterestGroup` that
    // is backed by permission with default self. Since the method acts on an
    // owner origin that may be different from the caller, it must check the
    // destination origin with IsFeatureEnabledInOrigin, and set
    // override_default_policy_to_all to true since the owner has opted in.
    // +--------------------------------------------------------+
    // |(1)Origin A                                             |
    // |No Policy                                               |
    // |                                                        |
    // | joinAdInterestGroup({owner: origin-b})                 |
    // +--------------------------------------------------------+

    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);

    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_a_));
    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_a_,
        /*override_default_policy_to_all=*/true));

    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_b_));
    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_b_,
        /*override_default_policy_to_all=*/true));
  }

  {
    // +--------------------------------------------------------+
    // |(1)Origin A                                             |
    // |Permissions-Policy: join-ad-interest-group=(self)       |
    // |                                                        |
    // | joinAdInterestGroup({owner: origin-b})                 |
    // +--------------------------------------------------------+

    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr,
                               {{{kJoinFeature,
                                  /*allowed_origins=*/{},
                                  /*self_if_matches=*/origin_a_,
                                  /*matches_all_origins=*/false,
                                  /*matches_opaque_src=*/false}}},
                               origin_a_);

    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_a_));
    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_a_,
        /*override_default_policy_to_all=*/true));

    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_b_));
    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_b_,
        /*override_default_policy_to_all=*/true));
  }

  {
    // +--------------------------------------------------------+
    // |(1)Origin A                                             |
    // |Permissions-Policy: join-ad-interest-group=(none)       |
    // |                                                        |
    // | joinAdInterestGroup({owner: origin-b})                 |
    // +--------------------------------------------------------+

    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr,
                               {{{kJoinFeature,
                                  /*allowed_origins=*/{},
                                  /*self_if_matches=*/std::nullopt,
                                  /*matches_all_origins=*/false,
                                  /*matches_opaque_src=*/false}}},
                               origin_a_);

    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_a_));
    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_a_,
        /*override_default_policy_to_all=*/true));

    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_b_));
    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_b_,
        /*override_default_policy_to_all=*/true));
  }

  {
    // +--------------------------------------------------------+
    // |(1)Origin A                                             |
    // |Permissions-Policy: join-ad-interest-group=*            |
    // |                                                        |
    // | joinAdInterestGroup({owner: origin-b})                 |
    // +--------------------------------------------------------+

    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr,
                               {{{kJoinFeature,
                                  /*allowed_origins=*/{},
                                  /*self_if_matches=*/std::nullopt,
                                  /*matches_all_origins=*/true,
                                  /*matches_opaque_src=*/false}}},
                               origin_a_);

    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_a_));
    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_a_,
        /*override_default_policy_to_all=*/true));

    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_b_));
    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_b_,
        /*override_default_policy_to_all=*/true));
  }

  {
    // +--------------------------------------------------------+
    // |(1)Origin A                                             |
    // |Permissions-Policy: join-ad-interest-group=(Origin B)   |
    // |                                                        |
    // | joinAdInterestGroup({owner: origin-b})                 |
    // | joinAdInterestGroup({owner: origin-c})                 |
    // +--------------------------------------------------------+

    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr,
                               {{{kJoinFeature, /*allowed_origins=*/
                                  {*network::OriginWithPossibleWildcards::
                                       FromOriginAndWildcardsForTest(
                                           origin_b_,
                                           /*has_subdomain_wildcard=*/false)},
                                  /*self_if_matches=*/std::nullopt,
                                  /*matches_all_origins=*/false,
                                  /*matches_opaque_src=*/false}}},
                               origin_a_);

    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_a_));
    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_a_ /*override_default_policy_to_all=*/));

    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_b_));
    EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_b_,
        /*override_default_policy_to_all=*/true));

    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(kJoinFeature, origin_c_));
    EXPECT_FALSE(policy->IsFeatureEnabledForOrigin(
        kJoinFeature, origin_c_,
        /*override_default_policy_to_all=*/true));
  }
}

// Tests for proposed algorithm change. These tests construct policies in
// various embedding scenarios, and verify that the proposed value for "should
// feature be allowed in the child frame" matches what we expect. The points
// where this differs from the current feature policy algorithm are called
// out specifically. See https://crbug.com/937131 for additional context.

TEST_F(PermissionsPolicyTest, ProposedTestImplicitPolicy) {
  // +-----------------+
  // |(1)Origin A      |
  // |No Policy        |
  // | +-------------+ |
  // | |(2)Origin A  | |
  // | |No Policy    | |
  // | +-------------+ |
  // | +-------------+ |
  // | |(3)Origin B  | |
  // | |No Policy    | |
  // | +-------------+ |
  // +-----------------+
  // With no policy specified at all, Default-on and Default-self features
  // should be enabled at the top-level, and in a same-origin child frame.
  // Default-self features should be disabled in a cross-origin child frame.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));

  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));

  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, ProposedTestCompletelyBlockedPolicy) {
  // +------------------------------------+
  // |(1)Origin A                         |
  // |Permissions-Policy: default-self=() |
  // | +--------------+  +--------------+ |
  // | |(2)Origin A   |  |(3)Origin B   | |
  // | |No Policy     |  |No Policy     | |
  // | +--------------+  +--------------+ |
  // | <allow="default-self *">           |
  // | +--------------+                   |
  // | |(4)Origin B   |                   |
  // | |No Policy     |                   |
  // | +--------------+                   |
  // | <allow="default-self OriginB">     |
  // | +--------------+                   |
  // | |(5)Origin B   |                   |
  // | |No Policy     |                   |
  // | +--------------+                   |
  // | <allow="default-self OriginB">     |
  // | +--------------+                   |
  // | |(6)Origin C   |                   |
  // | |No Policy     |                   |
  // | +--------------+                   |
  // +------------------------------------+
  // When a feature is disabled in the parent frame, it should be disabled in
  // all child frames, regardless of any declared frame policies.
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));

  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy4 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy4 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy4, origin_b_);
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy5 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy5 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy5, origin_b_);
  EXPECT_FALSE(policy5->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy6 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_c_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy6 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy6, origin_b_);
  EXPECT_FALSE(policy6->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, ProposedTestDisallowedCrossOriginChildPolicy) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |Permissions-Policy: default-self=self |
  // | +--------------+  +--------------+   |
  // | |(2)Origin A   |  |(3)Origin B   |   |
  // | |No Policy     |  |No Policy     |   |
  // | +--------------+  +--------------+   |
  // | <allow="default-self *">             |
  // | +--------------+                     |
  // | |(4)Origin B   |                     |
  // | |No Policy     |                     |
  // | +--------------+                     |
  // | <allow="default-self OriginB">       |
  // | +--------------+                     |
  // | |(5)Origin B   |                     |
  // | |No Policy     |                     |
  // | +--------------+                     |
  // | <allow="default-self OriginB">       |
  // | +--------------+                     |
  // | |(6)Origin C   |                     |
  // | |No Policy     |                     |
  // | +--------------+                     |
  // +--------------------------------------+
  // When a feature is not explicitly enabled for an origin, it should be
  // disabled in any frame at that origin, regardless of the declared frame
  // policy. (This is different from the current algorithm, in the case where
  // the frame policy declares that the feature should be allowed.)
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/origin_a_,
                                /*matches_all_origins=*/false,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);

  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));

  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));

  // This is a critical change from the existing semantics.
  network::ParsedPermissionsPolicy frame_policy4 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy4 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy4, origin_b_);
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));

  // This is a critical change from the existing semantics.
  network::ParsedPermissionsPolicy frame_policy5 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy5 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy5, origin_b_);
  EXPECT_FALSE(policy5->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy6 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_c_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy6 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy6, origin_b_);
  EXPECT_FALSE(policy6->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, ProposedTestAllowedCrossOriginChildPolicy) {
  // +---------------------------------------------------+
  // |(1)Origin A                                        |
  // |Permissions-Policy: default-self=(self "OriginB")  |
  // | +--------------+  +--------------+                |
  // | |(2)Origin A   |  |(3)Origin B   |                |
  // | |No Policy     |  |No Policy     |                |
  // | +--------------+  +--------------+                |
  // | <allow="default-self *">                          |
  // | +--------------+                                  |
  // | |(4)Origin B   |                                  |
  // | |No Policy     |                                  |
  // | +--------------+                                  |
  // | <allow="default-self OriginB">                    |
  // | +--------------+                                  |
  // | |(5)Origin B   |                                  |
  // | |No Policy     |                                  |
  // | +--------------+                                  |
  // | <allow="default-self OriginB">                    |
  // | +--------------+                                  |
  // | |(6)Origin C   |                                  |
  // | |No Policy     |                                  |
  // | +--------------+                                  |
  // +---------------------------------------------------+
  // When a feature is explicitly enabled for an origin by the header in the
  // parent document, it still requires that the frame policy also grant it to
  // that frame in order to be enabled in the child. (This is different from the
  // current algorithm, in the case where the frame policy does not mention the
  // feature explicitly.)
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/true,
         /*matches_opaque_src=*/false}}},
      origin_a_);

  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));

  // This is a critical change from the existing semantics.
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy4 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy4 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy4, origin_b_);
  EXPECT_TRUE(policy4->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy5 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy5 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy5, origin_b_);
  EXPECT_TRUE(policy5->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy6 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_c_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy6 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy6, origin_b_);
  EXPECT_FALSE(policy6->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, ProposedTestAllAllowedCrossOriginChildPolicy) {
  // +------------------------------------+
  // |(1)Origin A                         |
  // |Permissions-Policy: default-self=*  |
  // | +--------------+  +--------------+ |
  // | |(2)Origin A   |  |(3)Origin B   | |
  // | |No Policy     |  |No Policy     | |
  // | +--------------+  +--------------+ |
  // | <allow="default-self *">           |
  // | +--------------+                   |
  // | |(4)Origin B   |                   |
  // | |No Policy     |                   |
  // | +--------------+                   |
  // | <allow="default-self OriginB">     |
  // | +--------------+                   |
  // | |(5)Origin B   |                   |
  // | |No Policy     |                   |
  // | +--------------+                   |
  // | <allow="default-self OriginB">     |
  // | +--------------+                   |
  // | |(6)Origin C   |                   |
  // | |No Policy     |                   |
  // | +--------------+                   |
  // +------------------------------------+
  // When a feature is explicitly enabled for all origins by the header in the
  // parent document, it still requires that the frame policy also grant it to
  // that frame in order to be enabled in the child. (This is different from the
  // current algorithm, in the case where the frame policy does not mention the
  // feature explicitly.)
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultSelfFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);

  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));

  // This is a critical change from the existing semantics.
  std::unique_ptr<PermissionsPolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy4 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy4 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy4, origin_b_);
  EXPECT_TRUE(policy4->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy5 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy5 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy5, origin_b_);
  EXPECT_TRUE(policy5->IsFeatureEnabled(kDefaultSelfFeature));

  network::ParsedPermissionsPolicy frame_policy6 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_c_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy6 = CreateFromParentWithFramePolicy(
      policy1.get(), /*header_policy=*/{}, frame_policy6, origin_b_);
  EXPECT_FALSE(policy6->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, ProposedTestNestedPolicyPropagates) {
  // +-------------------------------------------------+
  // |(1)Origin A                                      |
  // |Permissions-Policy: default-self=(self "OriginB")|
  // | +--------------------------------+              |
  // | |(2)Origin B                     |              |
  // | |No Policy                       |              |
  // | | <allow="default-self *">       |              |
  // | | +--------------+               |              |
  // | | |(3)Origin B   |               |              |
  // | | |No Policy     |               |              |
  // | | +--------------+               |              |
  // | +--------------------------------+              |
  // +-------------------------------------------------+
  // Ensures that a proposed policy change will propagate down the frame tree.
  // This is important so that we can tell when a change has happened, even if
  // the feature is tested in a different one than where the
  std::unique_ptr<PermissionsPolicy> policy1 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/origin_a_,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);

  // This is where the change first occurs.
  std::unique_ptr<PermissionsPolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), /*header_policy=*/{}, origin_b_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));

  // The proposed value in frame 2 should affect the proposed value in frame 3.
  network::ParsedPermissionsPolicy frame_policy3 = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  std::unique_ptr<PermissionsPolicy> policy3 = CreateFromParentWithFramePolicy(
      policy2.get(), /*header_policy=*/{}, frame_policy3, origin_b_);
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, CreateFlexibleForFencedFrame) {
  std::unique_ptr<PermissionsPolicy> policy1 =
      CreateFromParentPolicy(nullptr,
                             {{{kDefaultOnFeature, /*allowed_origins=*/{},
                                /*self_if_matches=*/std::nullopt,
                                /*matches_all_origins=*/true,
                                /*matches_opaque_src=*/false}}},
                             origin_a_);
  std::unique_ptr<PermissionsPolicy> policy = CreateFlexibleForFencedFrame(
      policy1.get(), /*header_policy=*/{}, origin_a_);
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kAttributionReporting));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kSharedStorage));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kSharedStorageSelectUrl));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kPrivateAggregation));
}

TEST_F(PermissionsPolicyTest, CreateForFledgeFencedFrame) {
  std::vector<network::mojom::PermissionsPolicyFeature>
      effective_enabled_permissions;
  effective_enabled_permissions.insert(
      effective_enabled_permissions.end(),
      std::begin(network::kFencedFrameFledgeDefaultRequiredFeatures),
      std::end(network::kFencedFrameFledgeDefaultRequiredFeatures));

  std::unique_ptr<PermissionsPolicy> policy = CreateFixedForFencedFrame(
      origin_a_, /*header_policy=*/{}, effective_enabled_permissions);
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kAttributionReporting));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kSharedStorage));
}

TEST_F(PermissionsPolicyTest, CreateForSharedStorageFencedFrame) {
  std::vector<network::mojom::PermissionsPolicyFeature>
      effective_enabled_permissions;
  effective_enabled_permissions.insert(
      effective_enabled_permissions.end(),
      std::begin(network::kFencedFrameSharedStorageDefaultRequiredFeatures),
      std::end(network::kFencedFrameSharedStorageDefaultRequiredFeatures));

  std::unique_ptr<PermissionsPolicy> policy = CreateFixedForFencedFrame(
      origin_a_, /*header_policy=*/{}, effective_enabled_permissions);
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kAttributionReporting));
  EXPECT_TRUE(policy->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kSharedStorage));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicy) {
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false),
         *network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_);
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicyExcludingSelf) {
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_);
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicyWithEmptyAllowlist) {
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_);
  EXPECT_FALSE(policy->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicyWithBasePolicy) {
  url::Origin origin_self = url::Origin::Create(GURL("https://example.edu/"));
  network::ParsedPermissionsPolicy base_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {
            *network::OriginWithPossibleWildcards::
                FromOriginAndWildcardsForTest(origin_a_,
                                              /*has_subdomain_wildcard=*/false),
            *network::OriginWithPossibleWildcards::
                FromOriginAndWildcardsForTest(origin_b_,
                                              /*has_subdomain_wildcard=*/false),
        },
        /*self_if_matches=*/origin_self,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {
            *network::OriginWithPossibleWildcards::
                FromOriginAndWildcardsForTest(origin_b_,
                                              /*has_subdomain_wildcard=*/false),
            *network::OriginWithPossibleWildcards::
                FromOriginAndWildcardsForTest(origin_c_,
                                              /*has_subdomain_wildcard=*/false),
        },
        /*self_if_matches=*/origin_self,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_self, base_policy);
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_self));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_c_));
}

TEST_F(PermissionsPolicyTest,
       CreateFromParsedPolicyWithBasePolicyExcludingSelf) {
  url::Origin origin_self = url::Origin::Create(GURL("https://example.edu/"));
  network::ParsedPermissionsPolicy base_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_a_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_, base_policy);
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicyWithoutSelfWithBasePolicy) {
  url::Origin origin_self = url::Origin::Create(GURL("https://example.edu/"));
  network::ParsedPermissionsPolicy base_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/origin_a_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_, base_policy);
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
}

TEST_F(PermissionsPolicyTest,
       CreateFromParsedPolicyWildcardWithMoreRestrictiveBasePolicy) {
  network::ParsedPermissionsPolicy base_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_b_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/origin_a_,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_, base_policy);
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_c_));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicyWithWildcardBasePolicy) {
  network::ParsedPermissionsPolicy base_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/
        {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
            origin_a_,
            /*has_subdomain_wildcard=*/false)},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/false,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_, base_policy);
  EXPECT_TRUE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_b_));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_c_));
}

TEST_F(PermissionsPolicyTest, CreateFromParsedPolicyWithMissingBasePolicy) {
  // Tests a parsed policy that includes an allowlist for a feature not
  // declared in the base policy.
  network::ParsedPermissionsPolicy base_policy = {
      {{kDefaultOnFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  network::ParsedPermissionsPolicy parsed_policy = {
      {{kDefaultSelfFeature, /*allowed_origins=*/{},
        /*self_if_matches=*/std::nullopt,
        /*matches_all_origins=*/true,
        /*matches_opaque_src=*/false}}};
  auto policy = CreateFromParsedPolicy(parsed_policy, origin_a_, base_policy);
  EXPECT_TRUE(policy->IsFeatureEnabledForOrigin(kDefaultOnFeature, origin_a_));
  EXPECT_FALSE(
      policy->IsFeatureEnabledForOrigin(kDefaultSelfFeature, origin_a_));
}

TEST_F(PermissionsPolicyTest, OverwriteHeaderPolicyForClientHints) {
  // We can construct a policy, set/overwrite the same header, and then check.
  auto policy1 = CreateFromParentPolicy(
      nullptr,
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR,
         /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  policy1 = policy1->WithClientHints(
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR,
         /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}});
  EXPECT_TRUE(policy1->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR));

  // If we overwrite an enabled header with a disabled header it's now disabled.
  auto policy2 = CreateFromParentPolicy(
      nullptr,
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR,
         /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  policy2 = policy2->WithClientHints(
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR,
         /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}});
  EXPECT_FALSE(policy2->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR));

  // We can construct a policy, set/overwrite different headers, and then check.
  auto policy3 = CreateFromParentPolicy(
      nullptr,
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_b_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  policy3 = policy3->WithClientHints(
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR,
         /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}});
  EXPECT_TRUE(policy3->IsFeatureEnabled(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR));

  // We can't overwrite a non-client-hint header.
  auto policy4 = CreateFromParentPolicy(nullptr, {}, origin_a_);
  EXPECT_DCHECK_DEATH(policy4->WithClientHints(
      {{{kDefaultSelfFeature, /*allowed_origins=*/
         {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
             origin_a_,
             /*has_subdomain_wildcard=*/false)},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}}));
}

TEST_F(PermissionsPolicyTest, GetAllowlistForFeatureIfExists) {
  const std::vector<network::OriginWithPossibleWildcards> origins1(
      {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
          origin_b_,
          /*has_subdomain_wildcard=*/false)});
  // If we set a policy, then we can extract it.
  auto policy1 = CreateFromParentPolicy(
      nullptr,
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR, origins1,
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  const auto& maybe_allow_list1 = policy1->GetAllowlistForFeatureIfExists(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR);
  EXPECT_TRUE(maybe_allow_list1.has_value());
  EXPECT_FALSE(maybe_allow_list1.value().MatchesAll());
  EXPECT_FALSE(maybe_allow_list1.value().MatchesOpaqueSrc());
  EXPECT_THAT(maybe_allow_list1.value().AllowedOrigins(),
              testing::ContainerEq(origins1));

  // If we don't set a policy, then we can't extract it.
  auto policy2 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  const auto& maybe_allow_list2 = policy2->GetAllowlistForFeatureIfExists(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR);
  EXPECT_FALSE(maybe_allow_list2.has_value());

  const std::vector<network::OriginWithPossibleWildcards> origins3(
      {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
          origin_a_,
          /*has_subdomain_wildcard=*/false)});
  // If we set a policy, then overwrite it, we can extract it.
  auto policy3 = CreateFromParentPolicy(
      nullptr,
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR,
         {},
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}},
      origin_a_);
  auto new_policy3 = policy3->WithClientHints(
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR, origins3,
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}});
  const auto& maybe_allow_list3 = new_policy3->GetAllowlistForFeatureIfExists(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR);
  EXPECT_TRUE(maybe_allow_list3.has_value());
  EXPECT_FALSE(maybe_allow_list3.value().MatchesAll());
  EXPECT_FALSE(maybe_allow_list3.value().MatchesOpaqueSrc());
  EXPECT_THAT(maybe_allow_list3.value().AllowedOrigins(),
              testing::ContainerEq(origins3));

  // If we don't set a policy, then overwrite it, we can extract it.
  auto policy4 =
      CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
  const std::vector<network::OriginWithPossibleWildcards> origins4(
      {*network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_a_,
           /*has_subdomain_wildcard=*/false),
       *network::OriginWithPossibleWildcards::FromOriginAndWildcardsForTest(
           origin_b_,
           /*has_subdomain_wildcard=*/false)});
  policy4 = policy4->WithClientHints(
      {{{network::mojom::PermissionsPolicyFeature::kClientHintDPR, origins4,
         /*self_if_matches=*/std::nullopt,
         /*matches_all_origins=*/false,
         /*matches_opaque_src=*/false}}});
  const auto& maybe_allow_list4 = policy4->GetAllowlistForFeatureIfExists(
      network::mojom::PermissionsPolicyFeature::kClientHintDPR);
  EXPECT_TRUE(maybe_allow_list4.has_value());
  EXPECT_FALSE(maybe_allow_list4.value().MatchesAll());
  EXPECT_FALSE(maybe_allow_list4.value().MatchesOpaqueSrc());
  EXPECT_THAT(maybe_allow_list4.value().AllowedOrigins(),
              testing::ContainerEq(origins4));
}

// Tests that "unload"'s default is controlled by the deprecation flag.
TEST_F(PermissionsPolicyTest, UnloadDefaultEnabledForAll) {
  {
    base::test::ScopedFeatureList scoped_feature_list;
    scoped_feature_list.InitWithFeatures({},
                                         {network::features::kDeprecateUnload});
    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
    EXPECT_EQ(network::PermissionsPolicyFeatureDefault::EnableForAll,
              network::GetPermissionsPolicyFeatureList(origin_a_)
                  .find(network::mojom::PermissionsPolicyFeature::kUnload)
                  ->second);
  }
}

// Tests that "unload"'s default is controlled by the deprecation flag.
TEST_F(PermissionsPolicyTest, UnloadDefaultEnabledForNone) {
  {
    base::test::ScopedFeatureList feature_list;
    feature_list.InitWithFeatures({network::features::kDeprecateUnload},
                                  /*disabled_features=*/{});
    std::unique_ptr<PermissionsPolicy> policy =
        CreateFromParentPolicy(nullptr, /*header_policy=*/{}, origin_a_);
    EXPECT_EQ(network::PermissionsPolicyFeatureDefault::EnableForNone,
              network::GetPermissionsPolicyFeatureList(origin_a_)
                  .find(network::mojom::PermissionsPolicyFeature::kUnload)
                  ->second);
  }
}

network::PermissionsPolicyFeatureDefault GetDefaultForUnload(
    const url::Origin& origin) {
  return network::GetPermissionsPolicyFeatureList(origin)
      .find(network::mojom::PermissionsPolicyFeature::kUnload)
      ->second;
}

// Test that for a given URL and rollout-percent, that all buckets get the
// correct fraction of EnabledForNone vs EnableForAll.
TEST_F(PermissionsPolicyTest, GetPermissionsPolicyFeatureListForUnload) {
  const url::Origin origin = url::Origin::Create(GURL("http://testing/"));
  int total_count = 0;
  for (int percent = 0; percent < 100; percent++) {
    SCOPED_TRACE(base::StringPrintf("percent=%d", percent));
    // Will count how many case result in EnableForNone.
    int count = 0;
    for (int bucket = 0; bucket < 100; bucket++) {
      SCOPED_TRACE(base::StringPrintf("bucket=%d", bucket));
      base::test::ScopedFeatureList feature_list;
      feature_list.InitWithFeaturesAndParameters(
          {{network::features::kDeprecateUnload,
            {{network::features::kDeprecateUnloadPercent.name,
              base::StringPrintf("%d", percent)},
             {network::features::kDeprecateUnloadBucket.name,
              base::StringPrintf("%d", bucket)}}}},
          /*disabled_features=*/{});
      const network::PermissionsPolicyFeatureDefault unload_default =
          GetDefaultForUnload(origin);
      ASSERT_EQ(GetDefaultForUnload(origin.DeriveNewOpaqueOrigin()),
                unload_default);
      if (unload_default ==
          network::PermissionsPolicyFeatureDefault::EnableForNone) {
        count++;
      } else {
        ASSERT_EQ(unload_default,
                  network::PermissionsPolicyFeatureDefault::EnableForAll);
      }
    }
    // Because the bucket is used as salt, the percentage of users who see
    // EnableForNone for a given site is not exactly equal to `percent`. All we
    // can do is make sure it is close.
    // If we change the hashing this might need updating but it should not be
    // different run-to-run.
    ASSERT_NEAR(count, percent, 6);
    total_count += count;
  }
  ASSERT_NEAR(total_count, 99 * 100 / 2, 71);
}

// Tests for the handling unload deprecation parameters.
class DeprecateUnloadTest : public PermissionsPolicyTest {
 protected:
  const url::Origin http_origin1_ =
      url::Origin::Create(GURL("http://testing1/"));
  const url::Origin https_origin1_ =
      url::Origin::Create(GURL("https://testing1/"));
  const url::Origin http_origin2_ =
      url::Origin::Create(GURL("http://testing2/"));
  const url::Origin https_origin2_ =
      url::Origin::Create(GURL("https://testing2/"));
  const url::Origin http_origin3_ =
      url::Origin::Create(GURL("http://testing3/"));
  const url::Origin https_origin3_ =
      url::Origin::Create(GURL("https://testing3/"));
};

// The parameter should default to "" and be parsed correctly.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedHosts_Empty) {
  // Make sure the default is the empty string.
  ASSERT_EQ(network::features::kDeprecateUnloadAllowlist.Get(), "");
  EXPECT_EQ(std::unordered_set<std::string>({}),
            network::UnloadDeprecationAllowedHosts());
}

// A simple list of hosts should be parsed correctly.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedHosts_Simple) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnloadByAllowList,
        {{network::features::kDeprecateUnloadAllowlist.name,
          "testing1,testing2"}}}},
      /*disabled_features=*/{});

  EXPECT_EQ(std::unordered_set<std::string>({"testing1", "testing2"}),
            network::UnloadDeprecationAllowedHosts());
}

// A messy list of hosts should be parsed correctly.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedHosts_Messy) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnloadByAllowList,
        {{network::features::kDeprecateUnloadAllowlist.name,
          "testing1,, testing2,testing1"}}}},
      /*disabled_features=*/{});

  EXPECT_EQ(std::unordered_set<std::string>({"testing1", "testing2"}),
            network::UnloadDeprecationAllowedHosts());
}

// Test that UnloadDeprecationAllowedForHost works correctly with an empty
// allowlist.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedForHost_EmptyAllowList) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnloadByAllowList,
        {{network::features::kDeprecateUnloadAllowlist.name, ""}}}},
      /*disabled_features=*/{});
  const auto hosts = network::UnloadDeprecationAllowedHosts();
  // With no allowlist, every origin is allowed.
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(http_origin1_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(https_origin1_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(http_origin2_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(https_origin2_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(http_origin3_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(https_origin3_.host(), hosts));
}

// Test that the UnloadDeprecationAllowedForHost works correctly with
// an non-empty allowlist.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedForHost_NonEmptyAllowList) {
  // Now set an allowlist and check that only the allowed domains see
  // deprecation.
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnloadByAllowList,
        {{network::features::kDeprecateUnloadAllowlist.name,
          "testing1,testing2"}}}},
      /*disabled_features=*/{});

  const auto hosts = network::UnloadDeprecationAllowedHosts();
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(http_origin1_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(https_origin1_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(http_origin2_.host(), hosts));
  EXPECT_TRUE(
      network::UnloadDeprecationAllowedForHost(https_origin2_.host(), hosts));
  EXPECT_FALSE(
      network::UnloadDeprecationAllowedForHost(http_origin3_.host(), hosts));
  EXPECT_FALSE(
      network::UnloadDeprecationAllowedForHost(https_origin3_.host(), hosts));
}

// Non-http(s) origins should never be included in the deprecation.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedForOrigin_NonHttp) {
  // Set to 100% deprecation.
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnload,
        {{network::features::kDeprecateUnloadPercent.name, "100"},
         {network::features::kDeprecateUnloadBucket.name, "0"}}}},
      /*disabled_features=*/{});
  const url::Origin chrome_origin =
      url::Origin::Create(GURL("chrome://settings"));
  EXPECT_FALSE(network::UnloadDeprecationAllowedForOrigin(chrome_origin));
  EXPECT_FALSE(network::UnloadDeprecationAllowedForOrigin(
      chrome_origin.DeriveNewOpaqueOrigin()));
}

// When the rollout is at 0%, no host should be allowed.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedForOrigin_0Percent) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnload,
        {{network::features::kDeprecateUnloadPercent.name, "0"},
         {network::features::kDeprecateUnloadBucket.name, "0"}}}},
      /*disabled_features=*/{});
  EXPECT_FALSE(network::UnloadDeprecationAllowedForOrigin(http_origin1_));
  EXPECT_FALSE(network::UnloadDeprecationAllowedForOrigin(
      http_origin1_.DeriveNewOpaqueOrigin()));
}

// When the rollout is at 100% all hosts should be allowed.
TEST_F(DeprecateUnloadTest, UnloadDeprecationAllowedForOrigin_100Percent) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnload,
        {{network::features::kDeprecateUnloadPercent.name, "100"},
         {network::features::kDeprecateUnloadBucket.name, "0"}}}},
      /*disabled_features=*/{});
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(http_origin1_));
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(
      http_origin1_.DeriveNewOpaqueOrigin()));
}

// When the rollout is at 0% with an allowlist, no host should be allowed,
// including the one on the list.
TEST_F(DeprecateUnloadTest,
       UnloadDeprecationAllowedForOrigin_0PercentAndAllowList) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnload,
        {{network::features::kDeprecateUnloadPercent.name, "0"},
         {network::features::kDeprecateUnloadBucket.name, "0"}}},
       {network::features::kDeprecateUnloadByAllowList,
        {{network::features::kDeprecateUnloadAllowlist.name,
          http_origin1_.host()}}}},
      /*disabled_features=*/{});
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(http_origin1_));
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(
      http_origin1_.DeriveNewOpaqueOrigin()));
  // http_origin2 is not on the allow list.
  EXPECT_FALSE(network::UnloadDeprecationAllowedForOrigin(http_origin2_));
}

// When the rollout is at 100% with an allowlist, the hosts on the list should
// be allowed.
TEST_F(DeprecateUnloadTest,
       UnloadDeprecationAllowedForOrigin_100PercentAndAllowList) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeaturesAndParameters(
      {{network::features::kDeprecateUnload,
        {{network::features::kDeprecateUnloadPercent.name, "100"},
         {network::features::kDeprecateUnloadBucket.name, "0"}}},
       {network::features::kDeprecateUnloadByAllowList,
        {{network::features::kDeprecateUnloadAllowlist.name,
          http_origin1_.host()}}}},
      /*disabled_features=*/{});
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(http_origin1_));
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(
      http_origin1_.DeriveNewOpaqueOrigin()));
  // http_origin2 is not on the allow list but we are at 100%.
  EXPECT_TRUE(network::UnloadDeprecationAllowedForOrigin(http_origin2_));
}

// Test that we recognize the known types of headerless documents.
TEST_F(DeprecateUnloadTest, Headerless) {
  EXPECT_FALSE(
      PermissionsPolicy::IsHeaderlessUrl(GURL("http://www.test.com/")));
  EXPECT_FALSE(
      PermissionsPolicy::IsHeaderlessUrl(GURL("https://www.test.com/")));
  EXPECT_TRUE(PermissionsPolicy::IsHeaderlessUrl(GURL("about:srcdoc")));
  EXPECT_TRUE(PermissionsPolicy::IsHeaderlessUrl(GURL("about:blank")));
  EXPECT_TRUE(PermissionsPolicy::IsHeaderlessUrl(GURL("data:abc")));
  EXPECT_TRUE(PermissionsPolicy::IsHeaderlessUrl(GURL("blob:abc")));
}

}  // namespace network
