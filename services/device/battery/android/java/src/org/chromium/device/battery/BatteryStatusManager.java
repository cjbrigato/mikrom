// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.device.battery;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.task.AsyncTask;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskRunner;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.device.DeviceFeatureList;
import org.chromium.device.DeviceFeatureMap;
import org.chromium.device.mojom.BatteryStatus;

/**
 * Data source for battery status information. This class registers for battery status notifications
 * from the system and calls the callback passed on construction whenever a notification is
 * received.
 */
@NullMarked
class BatteryStatusManager {
    private static final String TAG = "BatteryStatusManager";
    private static final TaskRunner sSequencedTaskRunner =
            PostTask.createSequencedTaskRunner(TaskTraits.BEST_EFFORT_MAY_BLOCK);

    interface BatteryStatusCallback {
        void onBatteryStatusChanged(BatteryStatus batteryStatus);
    }

    private final BatteryStatusCallback mCallback;
    private final IntentFilter mFilter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
    private final BroadcastReceiver mReceiver =
            new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    BatteryStatusManager.this.onReceive(intent);
                }
            };

    private final @Nullable AndroidBatteryManagerWrapper mAndroidBatteryManager;
    private boolean mEnabled;

    @VisibleForTesting
    static class AndroidBatteryManagerWrapper {
        private final BatteryManager mBatteryManager;

        protected AndroidBatteryManagerWrapper(BatteryManager batteryManager) {
            mBatteryManager = batteryManager;
        }

        public int getIntProperty(int id) {
            return mBatteryManager.getIntProperty(id);
        }
    }

    private BatteryStatusManager(
            BatteryStatusCallback callback, @Nullable AndroidBatteryManagerWrapper batteryManager) {
        mCallback = callback;
        mAndroidBatteryManager = batteryManager;
    }

    BatteryStatusManager(BatteryStatusCallback callback) {
        this(
                callback,
                new AndroidBatteryManagerWrapper(
                        (BatteryManager)
                                ContextUtils.getApplicationContext()
                                        .getSystemService(Context.BATTERY_SERVICE)));
    }

    /**
     * Creates a BatteryStatusManager without the Galaxy Nexus workaround for consistency in
     * testing.
     */
    static BatteryStatusManager createBatteryStatusManagerForTesting(
            BatteryStatusCallback callback, @Nullable AndroidBatteryManagerWrapper batteryManager) {
        return new BatteryStatusManager(callback, batteryManager);
    }

    /** Starts listening for intents. */
    void start() {
        if (DeviceFeatureMap.isEnabled(
                DeviceFeatureList.BATTERY_STATUS_MANAGER_BROADCAST_RECEIVER_IN_BACKGROUND)) {
            startAsync();
            return;
        }

        if (mEnabled) return;
        if (ContextUtils.registerProtectedBroadcastReceiver(
                        ContextUtils.getApplicationContext(), mReceiver, mFilter)
                != null) {
            mEnabled = true;
        }
    }

    void startAsync() {
        if (mEnabled) return;

        sSequencedTaskRunner.execute(
                () -> {
                    ContextUtils.registerProtectedBroadcastReceiver(
                            ContextUtils.getApplicationContext(), mReceiver, mFilter);
                });
        mEnabled = true;
    }

    void stopAsync() {
        if (!mEnabled) return;

        sSequencedTaskRunner.execute(
                () -> {
                    ContextUtils.getApplicationContext().unregisterReceiver(mReceiver);
                });
        mEnabled = false;
    }

    /** Stops listening to intents. */
    void stop() {
        if (DeviceFeatureMap.isEnabled(
                DeviceFeatureList.BATTERY_STATUS_MANAGER_BROADCAST_RECEIVER_IN_BACKGROUND)) {
            stopAsync();
            return;
        }
        if (!mEnabled) return;

        ContextUtils.getApplicationContext().unregisterReceiver(mReceiver);
        mEnabled = false;
    }

    @VisibleForTesting
    void onReceive(Intent intent) {
        if (!Intent.ACTION_BATTERY_CHANGED.equals(intent.getAction())) {
            Log.e(TAG, "Unexpected intent.");
            return;
        }

        boolean present = intent.getBooleanExtra(BatteryManager.EXTRA_PRESENT, false);
        int pluggedStatus = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1);

        if (!present || pluggedStatus == -1) {
            // No battery or no plugged status: return default values.
            mCallback.onBatteryStatusChanged(new BatteryStatus());
            return;
        }

        int current = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int max = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
        double level = (double) current / (double) max;
        if (level < 0 || level > 1) {
            // Sanity check, assume default value in this case.
            level = 1.0;
        }

        // Currently Android (below L) does not provide charging/discharging time, as a work-around
        // we could compute it manually based on the evolution of level delta.
        // TODO(timvolodine): add proper projection for chargingTime, dischargingTime
        // (see crbug.com/401553).
        boolean charging = pluggedStatus != 0;
        int status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
        boolean batteryFull = status == BatteryManager.BATTERY_STATUS_FULL;
        double chargingTimeSeconds = (charging && batteryFull) ? 0 : Double.POSITIVE_INFINITY;
        double dischargingTimeSeconds = Double.POSITIVE_INFINITY;

        BatteryStatus batteryStatus = new BatteryStatus();
        batteryStatus.charging = charging;
        batteryStatus.chargingTime = chargingTimeSeconds;
        batteryStatus.dischargingTime = dischargingTimeSeconds;
        batteryStatus.level = level;

        if (mAndroidBatteryManager != null) {
            // Doing an AsyncTask since querying the BatteryManager might be slow. In the past, it
            // has caused ANRs when executed on the main thread - see crbug.com/1163401.
            new AsyncTask<BatteryStatus>() {
                @Override
                protected BatteryStatus doInBackground() {
                    updateBatteryStatus(batteryStatus);
                    return batteryStatus;
                }

                @Override
                protected void onPostExecute(BatteryStatus batteryStatus) {
                    mCallback.onBatteryStatusChanged(batteryStatus);
                }
            }.executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
        } else {
            mCallback.onBatteryStatusChanged(batteryStatus);
        }
    }

    private void updateBatteryStatus(BatteryStatus batteryStatus) {
        AndroidBatteryManagerWrapper batteryManager = assumeNonNull(mAndroidBatteryManager);
        double remainingCapacityRatio =
                batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY) / 100.0;
        double batteryCapacityMicroAh =
                batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CHARGE_COUNTER);
        double averageCurrentMicroA =
                batteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_AVERAGE);

        if (batteryStatus.charging) {
            if (batteryStatus.chargingTime == Double.POSITIVE_INFINITY
                    && averageCurrentMicroA > 0) {
                double chargeFromEmptyHours = batteryCapacityMicroAh / averageCurrentMicroA;
                batteryStatus.chargingTime =
                        Math.ceil((1 - remainingCapacityRatio) * chargeFromEmptyHours * 3600.0);
            }
        } else {
            if (averageCurrentMicroA < 0) {
                double dischargeFromFullHours = batteryCapacityMicroAh / -averageCurrentMicroA;
                batteryStatus.dischargingTime =
                        Math.floor(remainingCapacityRatio * dischargeFromFullHours * 3600.0);
            }
        }
    }
}
