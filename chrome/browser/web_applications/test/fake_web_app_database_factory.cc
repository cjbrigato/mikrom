// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/test/fake_web_app_database_factory.h"

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "chrome/browser/web_applications/proto/web_app.pb.h"
#include "chrome/browser/web_applications/proto/web_app_database_metadata.pb.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_database.h"
#include "chrome/browser/web_applications/web_app_database_serialization.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "components/sync/model/data_type_store.h"
#include "components/sync/model/model_error.h"
#include "components/sync/test/data_type_store_test_util.h"
#include "components/webapps/common/web_app_id.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace web_app {

FakeWebAppDatabaseFactory::FakeWebAppDatabaseFactory() = default;

FakeWebAppDatabaseFactory::~FakeWebAppDatabaseFactory() = default;

syncer::DataTypeStore* FakeWebAppDatabaseFactory::GetStore() {
  // Lazily instantiate to avoid performing blocking operations in tests that
  // never use web apps at all.
  // Note InMemoryStore must be created after message_loop_. See class comment.
  if (!store_) {
    store_ = syncer::DataTypeStoreTestUtil::CreateInMemoryStoreForTest();
  }
  return store_.get();
}

syncer::OnceDataTypeStoreFactory FakeWebAppDatabaseFactory::GetStoreFactory() {
  return syncer::DataTypeStoreTestUtil::FactoryForForwardingStore(GetStore());
}

bool FakeWebAppDatabaseFactory::IsSyncingApps() {
  return is_syncing_apps_;
}

FakeWebAppDatabaseFactory*
FakeWebAppDatabaseFactory::AsFakeWebAppDatabaseFactory() {
  return this;
}

proto::DatabaseMetadata FakeWebAppDatabaseFactory::ReadMetadata() {
  base::test::TestFuture<proto::DatabaseMetadata> loaded_metadata;
  GetStore()->ReadData(
      {std::string(WebAppDatabase::kDatabaseMetadataKey)},
      base::BindLambdaForTesting(
          [&](const std::optional<syncer::ModelError>& error,
              std::unique_ptr<syncer::DataTypeStore::RecordList> data_records,
              std::unique_ptr<syncer::DataTypeStore::IdList> missing_id_list) {
            proto::DatabaseMetadata proto;
            EXPECT_EQ(error, std::nullopt);
            if (data_records && data_records->size() > 0) {
              proto.ParseFromString((*data_records)[0].value);
            }
            loaded_metadata.SetValue(std::move(proto));
          }));
  EXPECT_TRUE(loaded_metadata.Wait(base::RunLoop::Type::kNestableTasksAllowed));
  return loaded_metadata.Take();
}

Registry FakeWebAppDatabaseFactory::ReadRegistry(bool allow_invalid_protos) {
  Registry registry;
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);

  GetStore()->ReadAllData(base::BindLambdaForTesting(
      [&](const std::optional<syncer::ModelError>& error,
          std::unique_ptr<syncer::DataTypeStore::RecordList> data_records) {
        CHECK(!error);

        for (const syncer::DataTypeStore::Record& record : *data_records) {
          if (record.id == WebAppDatabase::kDatabaseMetadataKey) {
            continue;
          }

          auto app = ParseWebAppProtoForTesting(record.id, record.value);
          if (allow_invalid_protos && !app) {
            DLOG(ERROR) << "Failed to parse web app proto for id: "
                        << record.id;
            continue;
          }
          CHECK(app) << "Failed to parse web app proto for id: " << record.id;

          webapps::AppId app_id = app->app_id();
          registry.emplace(std::move(app_id), std::move(app));
        }
        run_loop.Quit();
      }));

  run_loop.Run();
  return registry;
}

std::set<webapps::AppId> FakeWebAppDatabaseFactory::ReadAllAppIds() {
  std::set<webapps::AppId> app_ids;

  Registry registry = ReadRegistry();
  for (Registry::value_type& kv : registry) {
    app_ids.insert(kv.first);
  }

  return app_ids;
}

void FakeWebAppDatabaseFactory::WriteMetadata(
    const proto::DatabaseMetadata& metadata) {
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);

  std::unique_ptr<syncer::DataTypeStore::WriteBatch> write_batch =
      GetStore()->CreateWriteBatch();
  write_batch->WriteData(std::string(WebAppDatabase::kDatabaseMetadataKey),
                         metadata.SerializeAsString());
  GetStore()->CommitWriteBatch(
      std::move(write_batch),
      base::BindLambdaForTesting(
          [&](const std::optional<syncer::ModelError>& error) {
            CHECK(!error);
            run_loop.Quit();
          }));

  run_loop.Run();
}

void FakeWebAppDatabaseFactory::WriteProtos(
    const std::vector<std::unique_ptr<proto::WebApp>>& protos) {
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);

  std::unique_ptr<syncer::DataTypeStore::WriteBatch> write_batch =
      GetStore()->CreateWriteBatch();

  for (const std::unique_ptr<proto::WebApp>& proto : protos) {
    GURL start_url(proto->sync_data().start_url());
    CHECK(start_url.is_valid());
    webapps::AppId app_id =
        GenerateAppId(proto->sync_data().relative_manifest_id(), start_url);
    write_batch->WriteData(app_id, proto->SerializeAsString());
  }

  GetStore()->CommitWriteBatch(
      std::move(write_batch),
      base::BindLambdaForTesting(
          [&](const std::optional<syncer::ModelError>& error) {
            DCHECK(!error);
            run_loop.Quit();
          }));

  run_loop.Run();
}

void FakeWebAppDatabaseFactory::WriteProtos(
    const std::vector<proto::WebApp>& protos) {
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);

  std::unique_ptr<syncer::DataTypeStore::WriteBatch> write_batch =
      GetStore()->CreateWriteBatch();

  for (const proto::WebApp& proto : protos) {
    GURL start_url(proto.sync_data().start_url());
    CHECK(start_url.is_valid());
    webapps::ManifestId manifest_id;
    if (proto.sync_data().has_relative_manifest_id()) {
      manifest_id = GenerateManifestId(proto.sync_data().relative_manifest_id(),
                                       start_url);
    } else {
      manifest_id = GenerateManifestIdFromStartUrlOnly(start_url);
    }
    webapps::AppId app_id = GenerateAppIdFromManifestId(manifest_id);
    write_batch->WriteData(app_id, proto.SerializeAsString());
  }

  GetStore()->CommitWriteBatch(
      std::move(write_batch),
      base::BindLambdaForTesting(
          [&](const std::optional<syncer::ModelError>& error) {
            CHECK(!error);
            run_loop.Quit();
          }));

  run_loop.Run();
}

void FakeWebAppDatabaseFactory::WriteRegistry(const Registry& registry) {
  std::vector<std::unique_ptr<proto::WebApp>> protos;
  for (const Registry::value_type& kv : registry) {
    const WebApp* app = kv.second.get();
    std::unique_ptr<proto::WebApp> proto = WebAppToProto(*app);
    protos.push_back(std::move(proto));
  }

  WriteProtos(protos);
}

}  // namespace web_app
