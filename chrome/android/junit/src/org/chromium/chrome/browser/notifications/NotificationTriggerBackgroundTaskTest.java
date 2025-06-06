// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.notifications;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.os.PersistableBundle;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.background_task_scheduler.ChromeBackgroundTaskFactory;
import org.chromium.components.background_task_scheduler.BackgroundTask;
import org.chromium.components.background_task_scheduler.BackgroundTaskScheduler;
import org.chromium.components.background_task_scheduler.BackgroundTaskSchedulerFactory;
import org.chromium.components.background_task_scheduler.NativeBackgroundTask;
import org.chromium.components.background_task_scheduler.TaskIds;
import org.chromium.components.background_task_scheduler.TaskParameters;

/** Unit tests for NotificationTriggerBackgroundTask. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class NotificationTriggerBackgroundTaskTest {
    @Rule public final MockitoRule mMockitoRule = MockitoJUnit.rule();
    @Mock private BackgroundTaskScheduler mTaskScheduler;
    @Mock private NotificationTriggerScheduler mTriggerScheduler;
    @Mock private BackgroundTask.TaskFinishedCallback mTaskFinishedCallback;

    @Before
    public void setUp() {
        BackgroundTaskSchedulerFactory.setSchedulerForTesting(mTaskScheduler);
        ChromeBackgroundTaskFactory.setAsDefault();
        NotificationTriggerScheduler.setInstanceForTests(mTriggerScheduler);
    }

    private static TaskParameters createTaskParameters(long timestamp) {
        PersistableBundle extras = new PersistableBundle();
        extras.putLong(NotificationTriggerBackgroundTask.KEY_TIMESTAMP, timestamp);
        return TaskParameters.create(TaskIds.NOTIFICATION_TRIGGER_JOB_ID).addExtras(extras).build();
    }

    @Test
    public void testCancelCancelsTask() {
        NotificationTriggerBackgroundTask.cancel();
        verify(mTaskScheduler).cancel(any(), eq(TaskIds.NOTIFICATION_TRIGGER_JOB_ID));
    }

    @Test
    public void testStartBeforeNative_ValidTrigger() {
        long timestamp = System.currentTimeMillis() + 1000;
        doReturn(true).when(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));

        int result =
                new NotificationTriggerBackgroundTask()
                        .onStartTaskBeforeNativeLoaded(
                                RuntimeEnvironment.application,
                                createTaskParameters(timestamp),
                                mTaskFinishedCallback);

        assertEquals(NativeBackgroundTask.StartBeforeNativeResult.LOAD_NATIVE, result);
        verify(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));
        verify(mTaskFinishedCallback, never()).taskFinished(anyBoolean());
    }

    @Test
    public void testStartBeforeNative_InvalidTrigger() {
        long timestamp = System.currentTimeMillis() + 1000;
        doReturn(false).when(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));

        int result =
                new NotificationTriggerBackgroundTask()
                        .onStartTaskBeforeNativeLoaded(
                                RuntimeEnvironment.application,
                                createTaskParameters(timestamp),
                                mTaskFinishedCallback);

        assertEquals(NativeBackgroundTask.StartBeforeNativeResult.DONE, result);
        verify(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));
        verify(mTaskFinishedCallback, never()).taskFinished(anyBoolean());
    }

    @Test
    public void testStartWithNativeCallsTriggerNotifications() {
        long timestamp = System.currentTimeMillis() + 1000;
        new NotificationTriggerBackgroundTask()
                .onStartTaskWithNative(
                        RuntimeEnvironment.application,
                        createTaskParameters(timestamp),
                        mTaskFinishedCallback);

        verify(mTriggerScheduler).triggerNotifications();
        verify(mTaskFinishedCallback).taskFinished(eq(false));
    }

    @Test
    public void testReschedule_BeforeNative() {
        long timestamp = System.currentTimeMillis() + 1000;
        boolean shouldReschedule =
                new NotificationTriggerBackgroundTask()
                        .onStopTaskBeforeNativeLoaded(
                                RuntimeEnvironment.application, createTaskParameters(timestamp));
        assertTrue(shouldReschedule);
    }

    @Test
    public void testReschedule_WithNative() {
        long timestamp = System.currentTimeMillis() + 1000;
        boolean shouldReschedule =
                new NotificationTriggerBackgroundTask()
                        .onStopTaskWithNative(
                                RuntimeEnvironment.application, createTaskParameters(timestamp));
        assertTrue(shouldReschedule);
    }

    @Test
    public void testReschedule_AfterTriggerBeforeNative() {
        long timestamp = System.currentTimeMillis() + 1000;

        NotificationTriggerBackgroundTask task = new NotificationTriggerBackgroundTask();
        TaskParameters params = createTaskParameters(timestamp);
        task.onStartTaskWithNative(RuntimeEnvironment.application, params, mTaskFinishedCallback);
        boolean shouldReschedule =
                task.onStopTaskBeforeNativeLoaded(RuntimeEnvironment.application, params);

        assertFalse(shouldReschedule);
    }

    @Test
    public void testReschedule_AfterTriggerWithNative() {
        long timestamp = System.currentTimeMillis() + 1000;

        NotificationTriggerBackgroundTask task = new NotificationTriggerBackgroundTask();
        TaskParameters params = createTaskParameters(timestamp);
        task.onStartTaskWithNative(RuntimeEnvironment.application, params, mTaskFinishedCallback);
        boolean shouldReschedule =
                task.onStopTaskWithNative(RuntimeEnvironment.application, params);

        assertFalse(shouldReschedule);
    }

    @Test
    public void testReschedule_ValidTriggerBeforeNative() {
        long timestamp = System.currentTimeMillis() + 1000;
        doReturn(true).when(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));

        NotificationTriggerBackgroundTask task = new NotificationTriggerBackgroundTask();
        TaskParameters params = createTaskParameters(timestamp);
        task.onStartTaskBeforeNativeLoaded(
                RuntimeEnvironment.application, params, mTaskFinishedCallback);
        boolean shouldReschedule =
                task.onStopTaskBeforeNativeLoaded(RuntimeEnvironment.application, params);

        assertTrue(shouldReschedule);
    }

    @Test
    public void testReschedule_ValidTriggerWithNative() {
        long timestamp = System.currentTimeMillis() + 1000;
        doReturn(true).when(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));

        NotificationTriggerBackgroundTask task = new NotificationTriggerBackgroundTask();
        TaskParameters params = createTaskParameters(timestamp);
        task.onStartTaskBeforeNativeLoaded(
                RuntimeEnvironment.application, params, mTaskFinishedCallback);
        boolean shouldReschedule =
                task.onStopTaskWithNative(RuntimeEnvironment.application, params);

        assertTrue(shouldReschedule);
    }

    @Test
    public void testReschedule_InvalidTriggerBeforeNative() {
        long timestamp = System.currentTimeMillis() + 1000;
        doReturn(false).when(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));

        NotificationTriggerBackgroundTask task = new NotificationTriggerBackgroundTask();
        TaskParameters params = createTaskParameters(timestamp);
        task.onStartTaskBeforeNativeLoaded(
                RuntimeEnvironment.application, params, mTaskFinishedCallback);
        boolean shouldReschedule =
                task.onStopTaskBeforeNativeLoaded(RuntimeEnvironment.application, params);

        assertFalse(shouldReschedule);
    }

    @Test
    public void testReschedule_InvalidTriggerWithNative() {
        long timestamp = System.currentTimeMillis() + 1000;
        doReturn(false).when(mTriggerScheduler).checkAndResetTrigger(eq(timestamp));

        NotificationTriggerBackgroundTask task = new NotificationTriggerBackgroundTask();
        TaskParameters params = createTaskParameters(timestamp);
        task.onStartTaskBeforeNativeLoaded(
                RuntimeEnvironment.application, params, mTaskFinishedCallback);
        boolean shouldReschedule =
                task.onStopTaskWithNative(RuntimeEnvironment.application, params);

        assertFalse(shouldReschedule);
    }
}
