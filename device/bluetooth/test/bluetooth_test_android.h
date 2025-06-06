// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_BLUETOOTH_TEST_BLUETOOTH_TEST_ANDROID_H_
#define DEVICE_BLUETOOTH_TEST_BLUETOOTH_TEST_ANDROID_H_

#include <stdint.h>

#include "base/android/scoped_java_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "device/bluetooth/bluetooth_common.h"
#include "device/bluetooth/bluetooth_socket.h"
#include "device/bluetooth/test/bluetooth_test.h"

namespace device {

// Android implementation of BluetoothTestBase.
class BluetoothTestAndroid : public BluetoothTestBase {
 public:
  BluetoothTestAndroid();
  explicit BluetoothTestAndroid(
      base::test::TaskEnvironment::TimeSource time_source);
  ~BluetoothTestAndroid() override;

  // Test overrides:
  void SetUp() override;
  void TearDown() override;

  // BluetoothTestBase overrides:
  bool PlatformSupportsLowEnergy() override;
  void InitWithDefaultAdapter() override;
  void InitWithoutDefaultAdapter() override;
  void InitWithFakeAdapter() override;
  bool DenyPermission() override;
  BluetoothDevice* SimulateLowEnergyDevice(int device_ordinal) override;
  BluetoothDevice* SimulateClassicDevice() override;
  void RememberDeviceForSubsequentAction(BluetoothDevice* device) override;
  void SimulateGattConnection(BluetoothDevice* device) override;
  void SimulateGattConnectionError(BluetoothDevice* device,
                                   BluetoothDevice::ConnectErrorCode) override;
  void SimulateGattDisconnection(BluetoothDevice* device) override;
  void SimulateGattServicesDiscovered(
      BluetoothDevice* device,
      const std::vector<std::string>& uuids,
      const std::vector<std::string>& blocked_uuids = {}) override;
  void SimulateGattServicesDiscoveryError(BluetoothDevice* device) override;
  void SimulateGattCharacteristic(BluetoothRemoteGattService* service,
                                  const std::string& uuid,
                                  int properties) override;
  void RememberCharacteristicForSubsequentAction(
      BluetoothRemoteGattCharacteristic* characteristic) override;
  void RememberCCCDescriptorForSubsequentAction(
      BluetoothRemoteGattCharacteristic* characteristic) override;
  void SimulateGattNotifySessionStarted(
      BluetoothRemoteGattCharacteristic* characteristic) override;
  void SimulateGattNotifySessionStartError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothGattService::GattErrorCode error_code) override;
  void SimulateGattNotifySessionStopped(
      BluetoothRemoteGattCharacteristic* characteristic) override;
  void SimulateGattNotifySessionStopError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothGattService::GattErrorCode error_code) override;
  void SimulateGattCharacteristicSetNotifyWillFailSynchronouslyOnce(
      BluetoothRemoteGattCharacteristic* characteristic) override;
  void SimulateGattCharacteristicChanged(
      BluetoothRemoteGattCharacteristic* characteristic,
      const std::vector<uint8_t>& value) override;

  void SimulateGattCharacteristicRead(
      BluetoothRemoteGattCharacteristic* characteristic,
      const std::vector<uint8_t>& value) override;
  void SimulateGattCharacteristicReadError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothGattService::GattErrorCode) override;
  void SimulateGattCharacteristicReadWillFailSynchronouslyOnce(
      BluetoothRemoteGattCharacteristic* characteristic) override;

  void SimulateGattCharacteristicWrite(
      BluetoothRemoteGattCharacteristic* characteristic) override;
  void SimulateGattCharacteristicWriteError(
      BluetoothRemoteGattCharacteristic* characteristic,
      BluetoothGattService::GattErrorCode) override;
  void SimulateGattCharacteristicWriteWillFailSynchronouslyOnce(
      BluetoothRemoteGattCharacteristic* characteristic) override;

  void SimulateGattDescriptor(BluetoothRemoteGattCharacteristic* characteristic,
                              const std::string& uuid) override;
  void RememberDescriptorForSubsequentAction(
      BluetoothRemoteGattDescriptor* descriptor) override;

  void SimulateGattDescriptorRead(BluetoothRemoteGattDescriptor* descriptor,
                                  const std::vector<uint8_t>& value) override;
  void SimulateGattDescriptorReadError(
      BluetoothRemoteGattDescriptor* descriptor,
      BluetoothGattService::GattErrorCode) override;
  void SimulateGattDescriptorReadWillFailSynchronouslyOnce(
      BluetoothRemoteGattDescriptor* descriptor) override;

  void SimulateGattDescriptorWrite(
      BluetoothRemoteGattDescriptor* descriptor) override;
  void SimulateGattDescriptorWriteError(
      BluetoothRemoteGattDescriptor* descriptor,
      BluetoothGattService::GattErrorCode) override;
  void SimulateGattDescriptorWriteWillFailSynchronouslyOnce(
      BluetoothRemoteGattDescriptor* descriptor) override;

  // Instruct the fake adapter to enable/disable BT classic and BLE.
  void SetEnabledDeviceTransport(BluetoothTransport transport);

  // Creates a fake paired classic device
  //
  // |device_ordinal| selects between multiple fake device data sets to produce:
  //   0: Name: kTestDeviceName
  //      Address: kTestDeviceAddress3
  //      UUID: <empty>
  //   1: Name: kTestDeviceNameEmpty
  //      Address: kTestDeviceAddress1
  //      UUID: kTestUUIDSerial
  //   2: Name: kTestDeviceName
  //      Address: kTestDeviceAddress2
  //      UUID: kTestUUIDSerial
  // Returns the address of the simulated device.
  // Notify DeviceBondStateReceiver.Callback if |notify_callback| is true.
  std::string SimulatePairedClassicDevice(int device_ordinal,
                                          bool notify_callback = false);

  // Simulates having unpaired a device of |address|.
  void UnpairDevice(std::string address);

  // Simulates a low level (ACL) connect state change for |device|.
  void SimulateAclConnectStateChange(BluetoothDevice* device,
                                     uint8_t transport,
                                     bool connected);

  // Instruct the fake adapter to claim that location services are off for the
  // device.
  void SimulateLocationServicesOff();

  // Instruct the fake adapter to throw an IllegalStateException for
  // startScan and stopScan.
  void ForceIllegalStateException();

  // Instruct the fake LE scanner to invoke the failure callback with
  // |error_code|.
  void FailCurrentLeScan(int error_code);

  // Instructs the fake device to throw an IOException with |error_message| next
  // time |connectToService| or |connectToServiceInsecurely| is called with it.
  void FailNextServiceConnection(BluetoothDevice* device,
                                 const std::string& error_message);

  // Gets all bytes the fake socket was requested to send since it was created.
  std::vector<uint8_t> GetSentBytes(BluetoothSocket* socket);

  // Feeds the fake socket |bytes| and serve them when it is requested to read.
  // Can't be longer than 8KB.
  void SetReceivedBytes(BluetoothSocket* socket,
                        const std::vector<uint8_t>& bytes);

  // Fails the next operation on the fake socket by throwing an IOException with
  // |error_message|.
  void FailNextOperation(BluetoothSocket* socket,
                         const std::string& error_message);

  // Records that Java FakeBluetoothDevice connectGatt was called.
  void OnFakeBluetoothDeviceConnectGattCalled(JNIEnv* env);

  // Records that Java FakeBluetoothGatt disconnect was called.
  void OnFakeBluetoothGattDisconnect(JNIEnv* env);

  // Records that Java FakeBluetoothGatt close was called.
  void OnFakeBluetoothGattClose(JNIEnv* env);

  // Records that Java FakeBluetoothGatt discoverServices was called.
  void OnFakeBluetoothGattDiscoverServices(JNIEnv* env);

  // Records that Java FakeBluetoothGatt setCharacteristicNotification was
  // called.
  void OnFakeBluetoothGattSetCharacteristicNotification(JNIEnv* env);

  // Records that Java FakeBluetoothGatt readCharacteristic was called.
  void OnFakeBluetoothGattReadCharacteristic(JNIEnv* env);

  // Records that Java FakeBluetoothGatt writeCharacteristic was called.
  void OnFakeBluetoothGattWriteCharacteristic(
      JNIEnv* env,
      const base::android::JavaParamRef<jbyteArray>& value);

  // Records that Java FakeBluetoothGatt readDescriptor was called.
  void OnFakeBluetoothGattReadDescriptor(JNIEnv* env);

  // Records that Java FakeBluetoothGatt writeDescriptor was called.
  void OnFakeBluetoothGattWriteDescriptor(
      JNIEnv* env,
      const base::android::JavaParamRef<jbyteArray>& value);

  // Records that Java FakeBluetoothAdapter onAdapterStateChanged was called.
  void OnFakeAdapterStateChanged(JNIEnv* env, const bool powered);

  // Posts a task to be run on the current message loop.
  void PostTaskFromJava(JNIEnv* env,
                        const base::android::JavaParamRef<jobject>& runnable);

  // Posts a delayed task to be run on the current message loop.
  void PostDelayedTaskFromJava(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& runnable,
      jlong delayMillis);

  base::android::ScopedJavaGlobalRef<jobject> j_default_bluetooth_adapter_;
  base::android::ScopedJavaGlobalRef<jobject> j_fake_bluetooth_adapter_;

  int gatt_open_connections_ = 0;
  raw_ptr<BluetoothRemoteGattDescriptor> remembered_ccc_descriptor_ = nullptr;
};

// Defines common test fixture name. Use TEST_F(BluetoothTest, YourTestName).
typedef BluetoothTestAndroid BluetoothTest;

}  // namespace device

#endif  // DEVICE_BLUETOOTH_TEST_BLUETOOTH_TEST_ANDROID_H_
