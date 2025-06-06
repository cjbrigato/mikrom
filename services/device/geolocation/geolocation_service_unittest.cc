// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_CHROMEOS)
#include "chromeos/ash/components/dbus/shill/shill_clients.h"
#include "chromeos/ash/components/network/geolocation_handler.h"
#endif
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/network_change_notifier.h"
#include "services/device/device_service_test_base.h"
#include "services/device/geolocation/geolocation_provider_impl.h"
#include "services/device/geolocation/mock_wifi_data_provider.h"
#include "services/device/geolocation/network_location_request.h"
#include "services/device/geolocation/wifi_data_provider_handle.h"
#include "services/device/public/cpp/device_features.h"
#include "services/device/public/mojom/geolocation.mojom.h"
#include "services/device/public/mojom/geolocation_client_id.mojom.h"
#include "services/device/public/mojom/geolocation_context.mojom.h"
#include "services/device/public/mojom/geolocation_control.mojom.h"

namespace device {

namespace {

class GeolocationServiceUnitTest : public DeviceServiceTestBase {
 public:
  GeolocationServiceUnitTest() = default;

  GeolocationServiceUnitTest(const GeolocationServiceUnitTest&) = delete;
  GeolocationServiceUnitTest& operator=(const GeolocationServiceUnitTest&) =
      delete;

  ~GeolocationServiceUnitTest() override = default;

 protected:
  void SetUp() override {
#if BUILDFLAG(IS_CHROMEOS)
    ash::shill_clients::InitializeFakes();
    ash::NetworkHandler::Initialize();
#endif
    network_change_notifier_ = net::NetworkChangeNotifier::CreateMockIfNeeded();
    // We need to initialize the above *before* the base fixture instantiates
    // the device service.
    DeviceServiceTestBase::SetUp();

    wifi_data_provider_ = MockWifiDataProvider::CreateInstance();
    WifiDataProviderHandle::SetFactoryForTesting(
        MockWifiDataProvider::GetInstance);

    device_service()->BindGeolocationControl(
        geolocation_control_.BindNewPipeAndPassReceiver());
    geolocation_control_->UserDidOptIntoLocationServices();

    device_service()->BindGeolocationContext(
        geolocation_context_.BindNewPipeAndPassReceiver());
    geolocation_context_->BindGeolocation(
        geolocation_.BindNewPipeAndPassReceiver(), GURL(),
        mojom::GeolocationClientId::kForTesting);
  }

  void TearDown() override {
    WifiDataProviderHandle::ResetFactoryForTesting();

    DeviceServiceTestBase::TearDown();

#if BUILDFLAG(IS_CHROMEOS)
    ash::NetworkHandler::Shutdown();
    ash::shill_clients::Shutdown();
#endif

    // Let the GeolocationImpl destruct earlier than GeolocationProviderImpl to
    // make sure the base::RepeatingCallbackList<> member in
    // GeolocationProviderImpl is empty.
    geolocation_.reset();
    GeolocationProviderImpl::GetInstance()
        ->clear_user_did_opt_into_location_services_for_testing();
    base::RunLoop().RunUntilIdle();
  }

  scoped_refptr<MockWifiDataProvider> wifi_data_provider_;
  std::unique_ptr<net::NetworkChangeNotifier> network_change_notifier_;
  mojo::Remote<mojom::GeolocationControl> geolocation_control_;
  mojo::Remote<mojom::GeolocationContext> geolocation_context_;
  mojo::Remote<mojom::Geolocation> geolocation_;
#if BUILDFLAG(IS_MAC)
  base::test::ScopedFeatureList scoped_feature_list_;
#endif  // BUILDFLAG(IS_MAC)
};

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
// ChromeOS fails to perform network geolocation when zero wifi networks are
// detected in a scan: https://crbug.com/767300.
#else
TEST_F(GeolocationServiceUnitTest, UrlWithApiKey) {
// To align with user expectation we do not make Network Location Requests
// unless the browser has location system permission from the supported
// operating systems.
#if BUILDFLAG(IS_APPLE)
  fake_geolocation_system_permission_manager_->SetSystemPermission(
      LocationSystemPermissionStatus::kAllowed);
#endif

// crrev.com/c/6378263 enabled the platform location provider by default on
// macOS. This explicit feature flag configuration ensures network location
// provider tests remain unaffected.
#if BUILDFLAG(IS_MAC)
  scoped_feature_list_.InitAndEnableFeatureWithParameters(
      features::kLocationProviderManager,
      {{"LocationProviderManagerMode", "NetworkOnly"}});
#endif  // BUILDFLAG(IS_MAC)

  base::RunLoop loop;
  test_url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
      [&loop](const network::ResourceRequest& request) {
        // Verify the full URL including a fake Google API key.
        std::string expected_url =
            "https://www.googleapis.com/geolocation/v1/geolocate?key=";
        expected_url.append(kTestGeolocationApiKey);

        if (request.url == expected_url)
          loop.Quit();
      }));

  geolocation_->SetHighAccuracyHint(/*high_accuracy=*/true);
  loop.Run();

  // Clearing interceptor callback to ensure it does not outlive this scope.
  test_url_loader_factory_.SetInterceptor(base::NullCallback());
}
#endif

}  // namespace

}  // namespace device
