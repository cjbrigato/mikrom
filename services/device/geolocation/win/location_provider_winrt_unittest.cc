// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/geolocation/win/location_provider_winrt.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/win/scoped_winrt_initializer.h"
#include "services/device/geolocation/win/fake_geocoordinate_winrt.h"
#include "services/device/geolocation/win/fake_geolocator_winrt.h"
#include "services/device/public/cpp/geolocation/geoposition.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace device {
namespace {

using ABI::Windows::Devices::Geolocation::IGeolocator;
using ABI::Windows::Devices::Geolocation::PositionStatus;
using ABI::Windows::Devices::Geolocation::AltitudeReferenceSystem::
    AltitudeReferenceSystem_Terrain;

}  // namespace

class TestingLocationProviderWinrt : public LocationProviderWinrt {
 public:
  TestingLocationProviderWinrt(
      std::unique_ptr<FakeGeocoordinateData> position_data,
      PositionStatus position_status)
      : position_data_(std::move(position_data)),
        position_status_(position_status) {}

  bool HasPermissionBeenGrantedForTest() { return permission_granted_; }

  bool IsHighAccuracyEnabled() { return enable_high_accuracy_; }

  std::optional<EventRegistrationToken> GetStatusChangedToken() {
    return status_changed_token_;
  }

  std::optional<EventRegistrationToken> GetPositionChangedToken() {
    return position_changed_token_;
  }

  HRESULT GetGeolocator(IGeolocator** geo_locator) override {
    *geo_locator = Microsoft::WRL::Make<FakeGeolocatorWinrt>(
                       std::move(position_data_), position_status_)
                       .Detach();
    return S_OK;
  }

 private:
  std::unique_ptr<FakeGeocoordinateData> position_data_;
  const PositionStatus position_status_;
};

class LocationProviderWinrtTest : public testing::Test {
 protected:
  LocationProviderWinrtTest() = default;

  void SetUp() override {
    winrt_initializer_.emplace();
    ASSERT_TRUE(winrt_initializer_->Succeeded());
  }

  void InitializeProvider(
      PositionStatus position_status = PositionStatus::PositionStatus_Ready) {
    auto test_data = FakeGeocoordinateData();
    test_data.longitude = 0;
    test_data.latitude = 0;
    test_data.accuracy = 0;
    InitializeProvider(test_data, position_status);
  }

  void InitializeProvider(
      FakeGeocoordinateData position_data,
      PositionStatus position_status = PositionStatus::PositionStatus_Ready) {
    provider_ = std::make_unique<TestingLocationProviderWinrt>(
        std::make_unique<FakeGeocoordinateData>(position_data),
        position_status);
    provider_->SetUpdateCallback(update_future_.GetRepeatingCallback());
  }

  mojom::GeolocationDiagnostics::ProviderState GetProviderState() {
    mojom::GeolocationDiagnostics diagnostics;
    provider_->FillDiagnostics(diagnostics);
    return diagnostics.provider_state;
  }

  base::test::TaskEnvironment task_environment_;
  base::test::TestFuture<const LocationProvider*, mojom::GeopositionResultPtr>
      update_future_;
  std::optional<base::win::ScopedWinrtInitializer> winrt_initializer_;
  std::unique_ptr<TestingLocationProviderWinrt> provider_;
};

TEST_F(LocationProviderWinrtTest, CreateDestroy) {
  InitializeProvider();
  EXPECT_TRUE(provider_);
  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kStopped);
  provider_.reset();
}

TEST_F(LocationProviderWinrtTest, OnPermissionGranted) {
  InitializeProvider();
  EXPECT_FALSE(provider_->HasPermissionBeenGrantedForTest());
  provider_->OnPermissionGranted();
  EXPECT_TRUE(provider_->HasPermissionBeenGrantedForTest());
}

TEST_F(LocationProviderWinrtTest, SetAccuracyOptions) {
  InitializeProvider();
  provider_->StartProvider(/*enable_high_accuracy=*/false);
  EXPECT_EQ(false, provider_->IsHighAccuracyEnabled());
  provider_->StartProvider(/*enable_high_accuracy=*/true);
  EXPECT_EQ(true, provider_->IsHighAccuracyEnabled());
}

// Tests when OnPermissionGranted() called location update is provided.
TEST_F(LocationProviderWinrtTest, HasPermissions) {
  auto test_data = FakeGeocoordinateData();
  test_data.longitude = 1;
  test_data.latitude = 2;
  test_data.accuracy = 3;
  test_data.speed = 4;

  InitializeProvider(test_data);
  provider_->OnPermissionGranted();
  provider_->StartProvider(/*enable_high_accuracy=*/false);

  EXPECT_FALSE(update_future_.IsReady());

  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kLowAccuracy);
  EXPECT_TRUE(provider_->GetStatusChangedToken().has_value());
  EXPECT_TRUE(provider_->GetPositionChangedToken().has_value());

  auto& last_result = update_future_.Get<1>();
  ASSERT_TRUE(last_result->is_position());
  auto& position = *last_result->get_position();
  EXPECT_TRUE(ValidateGeoposition(position));
  EXPECT_EQ(position.latitude, test_data.latitude);
  EXPECT_EQ(position.longitude, test_data.longitude);
  EXPECT_EQ(position.accuracy, test_data.accuracy);
  EXPECT_EQ(position.altitude, device::mojom::kBadAltitude);
  EXPECT_EQ(position.altitude_accuracy, device::mojom::kBadAccuracy);
  EXPECT_EQ(position.speed, test_data.speed.value());
  EXPECT_EQ(position.heading, device::mojom::kBadHeading);
}

// Tests when OnPermissionGranted() called location update is provided with all
// possible values populated.
TEST_F(LocationProviderWinrtTest, HasPermissionsAllValues) {
  auto test_data = FakeGeocoordinateData();
  test_data.longitude = 1;
  test_data.latitude = 2;
  test_data.accuracy = 3;
  test_data.altitude = 4;
  test_data.altitude_accuracy = 5;
  test_data.heading = 6;
  test_data.speed = 7;
  test_data.altitude_reference_system = ABI::Windows::Devices::Geolocation::
      AltitudeReferenceSystem::AltitudeReferenceSystem_Ellipsoid;

  InitializeProvider(test_data);
  provider_->OnPermissionGranted();
  provider_->StartProvider(/*enable_high_accuracy=*/false);

  EXPECT_FALSE(update_future_.IsReady());

  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kLowAccuracy);
  EXPECT_TRUE(provider_->GetStatusChangedToken().has_value());
  EXPECT_TRUE(provider_->GetPositionChangedToken().has_value());

  auto& last_result = update_future_.Get<1>();
  ASSERT_TRUE(last_result->is_position());
  auto& position = *last_result->get_position();
  EXPECT_TRUE(ValidateGeoposition(position));
  EXPECT_EQ(position.latitude, test_data.latitude);
  EXPECT_EQ(position.longitude, test_data.longitude);
  EXPECT_EQ(position.accuracy, test_data.accuracy);
  EXPECT_EQ(position.altitude, test_data.altitude.value());
  EXPECT_EQ(position.altitude_accuracy, test_data.altitude_accuracy.value());
  EXPECT_EQ(position.speed, test_data.speed.value());
  EXPECT_EQ(position.heading, test_data.heading.value());
}

// Tests when provider is stopped and started quickly access errors
// do not occur and location update is not called.
TEST_F(LocationProviderWinrtTest, StartStopProviderRunTasks) {
  InitializeProvider();
  provider_->OnPermissionGranted();
  provider_->StartProvider(/*enable_high_accuracy=*/false);
  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kLowAccuracy);
  provider_->StopProvider();
  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kStopped);

  EXPECT_FALSE(update_future_.IsReady());

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(update_future_.IsReady());
}

// Tests when OnPermissionGranted() has not been called location update
// is not provided.
TEST_F(LocationProviderWinrtTest, NoPermissions) {
  InitializeProvider();
  provider_->StartProvider(/*enable_high_accuracy=*/false);

  EXPECT_EQ(
      GetProviderState(),
      mojom::GeolocationDiagnostics::ProviderState::kBlockedBySystemPermission);
  EXPECT_FALSE(update_future_.IsReady());

  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(update_future_.IsReady());
}

// Tests when a PositionStatus_Disabled is returned from the OS indicating
// access to location on the OS is disabled, a permission denied is returned.
TEST_F(LocationProviderWinrtTest, PositionStatusDisabledOsPermissions) {
  InitializeProvider(PositionStatus::PositionStatus_Disabled);
  provider_->OnPermissionGranted();
  provider_->StartProvider(/*enable_high_accuracy=*/false);

  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kLowAccuracy);
  EXPECT_FALSE(update_future_.IsReady());

  EXPECT_TRUE(provider_->GetStatusChangedToken().has_value());
  EXPECT_TRUE(provider_->GetPositionChangedToken().has_value());

  auto& last_result = update_future_.Get<1>();
  ASSERT_TRUE(last_result->is_error());
  auto& last_error = *last_result->get_error();
  EXPECT_EQ(last_error.error_code,
            mojom::GeopositionErrorCode::kPermissionDenied);
}

// Tests when the altitude reference system is not Ellipsoid.
// In this case, the altitude should be set to device::mojom::kBadAltitude.
TEST_F(LocationProviderWinrtTest, NonEllipsoidAltitudeReferenceSystem) {
  auto test_data = FakeGeocoordinateData();
  test_data.longitude = 1;
  test_data.latitude = 2;
  test_data.accuracy = 3;
  test_data.altitude = 4;
  test_data.altitude_accuracy = 5;
  test_data.altitude_reference_system = AltitudeReferenceSystem_Terrain;

  InitializeProvider(test_data);
  provider_->OnPermissionGranted();
  provider_->StartProvider(/*enable_high_accuracy=*/false);

  EXPECT_FALSE(update_future_.IsReady());

  EXPECT_EQ(GetProviderState(),
            mojom::GeolocationDiagnostics::ProviderState::kLowAccuracy);
  EXPECT_TRUE(provider_->GetStatusChangedToken().has_value());
  EXPECT_TRUE(provider_->GetPositionChangedToken().has_value());

  auto& last_result = update_future_.Get<1>();
  ASSERT_TRUE(last_result->is_position());
  auto& position = *last_result->get_position();
  EXPECT_TRUE(ValidateGeoposition(position));
  EXPECT_EQ(position.latitude, test_data.latitude);
  EXPECT_EQ(position.longitude, test_data.longitude);
  EXPECT_EQ(position.accuracy, test_data.accuracy);
  EXPECT_EQ(position.altitude, device::mojom::kBadAltitude);
}

}  // namespace device
