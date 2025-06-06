// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/client_certificates/core/private_key_factory.h"

#include <array>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "components/enterprise/client_certificates/core/constants.h"
#include "components/enterprise/client_certificates/core/private_key.h"
#include "components/enterprise/client_certificates/core/private_key_types.h"

namespace client_certificates {

namespace {

// List of all the key sources ordered by security (i.e. first entry yields most
// secure key source).
constexpr std::array<PrivateKeySource, 3> kKeySourcesOrderedBySecurity = {
    PrivateKeySource::kUnexportableKey,
    PrivateKeySource::kOsSoftwareKey,
    PrivateKeySource::kSoftwareKey,
};

}  // namespace

PrivateKeyFactory::PrivateKeyFactory() = default;
PrivateKeyFactory::~PrivateKeyFactory() = default;

class PrivateKeyFactoryImpl : public PrivateKeyFactory {
 public:
  explicit PrivateKeyFactoryImpl(PrivateKeyFactoriesMap sub_factories);

  ~PrivateKeyFactoryImpl() override;

  // PrivateKeyFactory:
  void CreatePrivateKey(PrivateKeyCallback callback) override;
  void LoadPrivateKey(
      const client_certificates_pb::PrivateKey& serialized_private_key,
      PrivateKeyCallback callback) override;
  void LoadPrivateKeyFromDict(const base::Value::Dict& serialized_private_key,
                              PrivateKeyCallback callback) override;

 private:
  void OnPrivateKeyCreated(PrivateKeySource source,
                           PrivateKeyCallback callback,
                           scoped_refptr<PrivateKey> private_key);

  PrivateKeyFactoriesMap sub_factories_;

  base::WeakPtrFactory<PrivateKeyFactoryImpl> weak_factory_{this};
};

PrivateKeyFactoryImpl::PrivateKeyFactoryImpl(
    PrivateKeyFactory::PrivateKeyFactoriesMap sub_factories)
    : sub_factories_(std::move(sub_factories)) {}

PrivateKeyFactoryImpl::~PrivateKeyFactoryImpl() = default;

void PrivateKeyFactoryImpl::CreatePrivateKey(
    PrivateKeyFactory::PrivateKeyCallback callback) {
  // Go through the supported key sources in order of most secure to least, and
  // delegate the key creation to that sub factory.
  for (size_t i = 0U; i < kKeySourcesOrderedBySecurity.size(); i++) {
    PrivateKeySource source = kKeySourcesOrderedBySecurity[i];
    if (sub_factories_.contains(source)) {
      sub_factories_[source]->CreatePrivateKey(base::BindOnce(
          &PrivateKeyFactoryImpl::OnPrivateKeyCreated,
          weak_factory_.GetWeakPtr(), source, std::move(callback)));
      return;
    }
  }

  std::move(callback).Run(nullptr);
}

void PrivateKeyFactoryImpl::LoadPrivateKey(
    const client_certificates_pb::PrivateKey& serialized_private_key,
    PrivateKeyCallback callback) {
  auto private_key_source = ToPrivateKeySource(serialized_private_key.source());
  if (!private_key_source.has_value() ||
      !sub_factories_.contains(private_key_source.value())) {
    std::move(callback).Run(nullptr);
    return;
  }

  sub_factories_[private_key_source.value()]->LoadPrivateKey(
      std::move(serialized_private_key), std::move(callback));
}

void PrivateKeyFactoryImpl::LoadPrivateKeyFromDict(
    const base::Value::Dict& serialized_private_key,
    PrivateKeyCallback callback) {
  std::optional<int> source = serialized_private_key.FindInt(kKeySource);
  if (!source.has_value()) {
    std::move(callback).Run(nullptr);
    return;
  }

  auto private_key_source = ToPrivateKeySource(*source);
  if (!private_key_source.has_value() ||
      !sub_factories_.contains(private_key_source.value())) {
    std::move(callback).Run(nullptr);
    return;
  }

  sub_factories_[private_key_source.value()]->LoadPrivateKeyFromDict(
      serialized_private_key, std::move(callback));
}

void PrivateKeyFactoryImpl::OnPrivateKeyCreated(
    PrivateKeySource source,
    PrivateKeyCallback callback,
    scoped_refptr<PrivateKey> private_key) {
  if (!private_key && source != PrivateKeySource::kSoftwareKey &&
      sub_factories_.contains(PrivateKeySource::kSoftwareKey)) {
    // If a more secure key failed to be created, fallback to creating a
    // software key (which should always succeed).
    sub_factories_[PrivateKeySource::kSoftwareKey]->CreatePrivateKey(
        std::move(callback));
    return;
  }

  std::move(callback).Run(std::move(private_key));
}

// static
std::unique_ptr<PrivateKeyFactory> PrivateKeyFactory::Create(
    PrivateKeyFactoriesMap sub_factories) {
  return std::make_unique<PrivateKeyFactoryImpl>(std::move(sub_factories));
}

}  // namespace client_certificates
