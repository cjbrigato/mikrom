// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.process_launcher;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import android.content.ComponentName;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.LooperMode;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Feature;

import java.util.HashSet;
import java.util.Set;

/** Unit tests for the ChildConnectionAllocator class. */
@Config(manifest = Config.NONE)
@RunWith(BaseRobolectricTestRunner.class)
@LooperMode(LooperMode.Mode.LEGACY)
public class ChildConnectionAllocatorTest {
    private static final String TEST_PACKAGE_NAME = "org.chromium.allocator_test";

    private static final int MAX_CONNECTION_NUMBER = 2;

    private static final int FREE_CONNECTION_TEST_CALLBACK_START_FAILED = 1;
    private static final int FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED = 2;

    @Mock private ChildProcessConnection.ServiceCallback mServiceCallback;

    static class TestConnectionFactory implements ChildConnectionAllocator.ConnectionFactory {
        private ComponentName mLastServiceName;
        private ComponentName mLastFallbackServiceName;
        private String mLastInstanceName;

        private ChildProcessConnection mConnection;

        private ChildProcessConnection.ServiceCallback mConnectionServiceCallback;

        @Override
        public ChildProcessConnection createConnection(
                Context context,
                ComponentName serviceName,
                ComponentName fallbackServiceName,
                boolean bindToCaller,
                boolean bindAsExternalService,
                Bundle serviceBundle,
                String instanceName,
                boolean independentFallback,
                boolean isSandboxedForHistograms) {
            mLastServiceName = serviceName;
            mLastFallbackServiceName = fallbackServiceName;
            mLastInstanceName = instanceName;
            if (mConnection == null) {
                mConnection = mock(ChildProcessConnection.class);
                // Retrieve the ServiceCallback so we can simulate the service process dying.
                doAnswer(
                                new Answer() {
                                    @Override
                                    public Object answer(InvocationOnMock invocation) {
                                        mConnectionServiceCallback =
                                                (ChildProcessConnection.ServiceCallback)
                                                        invocation.getArgument(1);
                                        return null;
                                    }
                                })
                        .when(mConnection)
                        .start(anyBoolean(), any(ChildProcessConnection.ServiceCallback.class));
            }
            return mConnection;
        }

        public ComponentName getAndResetLastServiceName() {
            ComponentName serviceName = mLastServiceName;
            mLastServiceName = null;
            return serviceName;
        }

        public ComponentName getAndResetLastFallbackServiceName() {
            ComponentName fallbackServiceName = mLastFallbackServiceName;
            mLastFallbackServiceName = null;
            return fallbackServiceName;
        }

        public String getAndResetLastInstanceName() {
            String instanceName = mLastInstanceName;
            mLastInstanceName = null;
            return instanceName;
        }

        // Use this method to have a callback invoked when the connection is started on the next
        // created connection.
        public void invokeCallbackOnConnectionStart(
                final boolean onChildStarted,
                final boolean onStartFailed,
                final boolean onChildProcessDied) {
            final ChildProcessConnection connection = mock(ChildProcessConnection.class);
            mConnection = connection;
            doAnswer(
                            new Answer() {
                                @Override
                                public Object answer(InvocationOnMock invocation) {
                                    ChildProcessConnection.ServiceCallback serviceCallback =
                                            (ChildProcessConnection.ServiceCallback)
                                                    invocation.getArgument(1);
                                    if (onChildStarted) {
                                        serviceCallback.onChildStarted();
                                    }
                                    if (onStartFailed) {
                                        serviceCallback.onChildStartFailed(connection);
                                    }
                                    if (onChildProcessDied) {
                                        serviceCallback.onChildProcessDied(connection);
                                    }
                                    return null;
                                }
                            })
                    .when(mConnection)
                    .start(anyBoolean(), any(ChildProcessConnection.ServiceCallback.class));
        }

        public void simulateServiceStartFailed() {
            mConnectionServiceCallback.onChildStartFailed(mConnection);
        }

        public void simulateServiceProcessDying() {
            mConnectionServiceCallback.onChildProcessDied(mConnection);
        }
    }

    private final TestConnectionFactory mTestConnectionFactory = new TestConnectionFactory();

    private ChildConnectionAllocator.FixedSizeAllocatorImpl mAllocator;
    private ChildConnectionAllocator mVariableSizeAllocator;
    private ChildConnectionAllocator mWorkaroundAllocator;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);

        mAllocator =
                ChildConnectionAllocator.createFixedForTesting(
                        null,
                        TEST_PACKAGE_NAME,
                        "AllocatorTest",
                        MAX_CONNECTION_NUMBER,
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ false,
                        /* fallbackToNextSlot= */ false,
                        /* isSandboxedForHistograms= */ false);
        mAllocator.setConnectionFactoryForTesting(mTestConnectionFactory);

        mVariableSizeAllocator =
                ChildConnectionAllocator.createVariableSizeForTesting(
                        new Handler(),
                        TEST_PACKAGE_NAME,
                        /* freeSlotCallback= */ null,
                        "AllocatorTest",
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ false,
                        10);
        mVariableSizeAllocator.setConnectionFactoryForTesting(mTestConnectionFactory);

        mWorkaroundAllocator =
                ChildConnectionAllocator.createWorkaroundForTesting(
                        new Handler(),
                        TEST_PACKAGE_NAME,
                        /* freeSlotCallback= */ null,
                        "AllocatorTest",
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ false,
                        10);
        mWorkaroundAllocator.setConnectionFactoryForTesting(mTestConnectionFactory);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testPlainAllocate() {
        assertFalse(mAllocator.anyConnectionAllocated());
        assertEquals(MAX_CONNECTION_NUMBER, mAllocator.getMaxNumberOfAllocations());

        ChildProcessConnection connection =
                mAllocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
        assertNotNull(connection);

        verify(connection, times(1))
                .start(
                        eq(false)
                        /* useStrongBinding= */ ,
                        any(ChildProcessConnection.ServiceCallback.class));
        assertTrue(mAllocator.anyConnectionAllocated());
    }

    /** Tests that different services are created until we reach the max number specified. */
    @Test
    @Feature({"ProcessManagement"})
    public void testAllocateMaxNumber() {
        assertTrue(mAllocator.isFreeConnectionAvailable());
        Set<ComponentName> serviceNames = new HashSet<>();
        for (int i = 0; i < MAX_CONNECTION_NUMBER; i++) {
            ChildProcessConnection connection =
                    mAllocator.allocate(
                            /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
            assertNotNull(connection);
            ComponentName serviceName = mTestConnectionFactory.getAndResetLastServiceName();
            assertFalse(serviceNames.contains(serviceName));
            serviceNames.add(serviceName);
        }
        assertFalse(mAllocator.isFreeConnectionAvailable());
        assertNull(
                mAllocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback));
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testQueueAllocation() {
        Runnable freeConnectionCallback = mock(Runnable.class);
        mAllocator =
                ChildConnectionAllocator.createFixedForTesting(
                        freeConnectionCallback,
                        TEST_PACKAGE_NAME,
                        "AllocatorTest",
                        1,
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ false,
                        /* fallbackToNextSlot= */ false,
                        /* isSandboxedForHistograms= */ false);
        doTestQueueAllocation(mAllocator, freeConnectionCallback);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testQueueAllocationVariableSize() {
        Runnable freeConnectionCallback = mock(Runnable.class);
        mVariableSizeAllocator =
                ChildConnectionAllocator.createVariableSizeForTesting(
                        new Handler(),
                        TEST_PACKAGE_NAME,
                        freeConnectionCallback,
                        "AllocatorTest",
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ false,
                        1);
        doTestQueueAllocation(mVariableSizeAllocator, freeConnectionCallback);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testQueueAllocationWorkaround() {
        Runnable freeConnectionCallback = mock(Runnable.class);
        mWorkaroundAllocator =
                ChildConnectionAllocator.createWorkaroundForTesting(
                        new Handler(),
                        TEST_PACKAGE_NAME,
                        freeConnectionCallback,
                        "AllocatorTest",
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ false,
                        1);
        doTestQueueAllocation(mWorkaroundAllocator, freeConnectionCallback);
    }

    private void doTestQueueAllocation(
            ChildConnectionAllocator allocator, Runnable freeConnectionCallback) {
        allocator.setConnectionFactoryForTesting(mTestConnectionFactory);
        // Occupy all slots.
        ChildProcessConnection connection =
                allocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
        assertNotNull(connection);
        assertEquals(1, allocator.allocatedConnectionsCountForTesting());

        final ChildProcessConnection newConnection[] = new ChildProcessConnection[2];
        Runnable allocate1 =
                () -> {
                    newConnection[0] =
                            allocator.allocate(
                                    /* context= */ null,
                                    /* serviceBundle= */ null,
                                    mServiceCallback);
                };
        Runnable allocate2 =
                () -> {
                    newConnection[1] =
                            allocator.allocate(
                                    /* context= */ null,
                                    /* serviceBundle= */ null,
                                    mServiceCallback);
                };
        allocator.queueAllocation(allocate1);
        allocator.queueAllocation(allocate2);
        verify(freeConnectionCallback, times(1)).run();
        assertNull(newConnection[0]);

        mTestConnectionFactory.simulateServiceProcessDying();
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertNotNull(newConnection[0]);
        assertNull(newConnection[1]);

        mTestConnectionFactory.simulateServiceProcessDying();
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertNotNull(newConnection[1]);
    }

    /**
     * Tests that the connection is created with the useStrongBinding parameter specified in the
     * allocator.
     */
    @Test
    @Feature({"ProcessManagement"})
    public void testStrongBindingParam() {
        for (boolean useStrongBinding : new boolean[] {true, false}) {
            ChildConnectionAllocator allocator =
                    ChildConnectionAllocator.createFixedForTesting(
                            null,
                            TEST_PACKAGE_NAME,
                            "AllocatorTest",
                            MAX_CONNECTION_NUMBER,
                            /* bindToCaller= */ true,
                            /* bindAsExternalService= */ false,
                            useStrongBinding,
                            /* fallbackToNextSlot= */ false,
                            /* isSandboxedForHistograms= */ false);
            allocator.setConnectionFactoryForTesting(mTestConnectionFactory);
            ChildProcessConnection connection =
                    allocator.allocate(
                            /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
            verify(connection, times(0)).start(useStrongBinding, mServiceCallback);
        }
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFixedSizeAllocatorFallback() {
        ChildConnectionAllocator.FixedSizeAllocatorImpl allocator =
                ChildConnectionAllocator.createFixedForTesting(
                        null,
                        TEST_PACKAGE_NAME,
                        "AllocatorTest",
                        /* serviceCount= */ 4,
                        /* bindToCaller= */ true,
                        /* bindAsExternalService= */ false,
                        /* useStrongBinding= */ true,
                        /* fallbackToNextSlot= */ true,
                        /* isSandboxedForHistograms= */ false);

        assertEquals(2, allocator.getMaxNumberOfAllocations());
        assertEquals(0, allocator.allocatedConnectionsCountForTesting());
        assertEquals(true, allocator.isFreeConnectionAvailable());
        assertEquals(false, allocator.anyConnectionAllocated());

        TestConnectionFactory connection1Factory = new TestConnectionFactory();
        allocator.setConnectionFactoryForTesting(connection1Factory);
        ChildProcessConnection connection1 =
                allocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
        assertEquals(1, allocator.allocatedConnectionsCountForTesting());
        assertEquals(true, allocator.isFreeConnectionAvailable());
        assertEquals(true, allocator.anyConnectionAllocated());
        assertEquals(connection1, allocator.getChildProcessConnectionAtSlotForTesting(0));
        assertNotNull(connection1Factory.getAndResetLastFallbackServiceName());

        TestConnectionFactory connection2Factory = new TestConnectionFactory();
        allocator.setConnectionFactoryForTesting(connection2Factory);
        ChildProcessConnection connection2 =
                allocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
        assertEquals(2, allocator.allocatedConnectionsCountForTesting());
        assertEquals(false, allocator.isFreeConnectionAvailable());
        assertEquals(true, allocator.anyConnectionAllocated());
        assertEquals(connection2, allocator.getChildProcessConnectionAtSlotForTesting(2));
        assertNotNull(connection2Factory.getAndResetLastFallbackServiceName());

        connection2Factory.simulateServiceProcessDying();
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(1, allocator.allocatedConnectionsCountForTesting());
        assertEquals(true, allocator.isFreeConnectionAvailable());
        assertEquals(true, allocator.anyConnectionAllocated());

        connection1Factory.simulateServiceProcessDying();
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertEquals(2, allocator.getMaxNumberOfAllocations());
        assertEquals(0, allocator.allocatedConnectionsCountForTesting());
        assertEquals(true, allocator.isFreeConnectionAvailable());
        assertEquals(false, allocator.anyConnectionAllocated());
    }

    /**
     * Tests that the various ServiceCallbacks are propagated and posted, so they happen after the
     * ChildProcessAllocator,allocate() method has returned.
     */
    public void runTestWithConnectionCallbacks(
            ChildConnectionAllocator allocator,
            boolean onChildStarted,
            boolean onChildStartFailed,
            boolean onChildProcessDied) {
        // We have to pause the Roboletric looper or it'll execute the posted tasks synchronoulsy.
        ShadowLooper.pauseMainLooper();
        mTestConnectionFactory.invokeCallbackOnConnectionStart(
                onChildStarted, onChildStartFailed, onChildProcessDied);
        ChildProcessConnection connection =
                allocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
        assertNotNull(connection);

        // Callbacks are posted.
        verify(mServiceCallback, never()).onChildStarted();
        verify(mServiceCallback, never()).onChildStartFailed(any());
        verify(mServiceCallback, never()).onChildProcessDied(any());
        ShadowLooper.unPauseMainLooper();
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        verify(mServiceCallback, times(onChildStarted ? 1 : 0)).onChildStarted();
        verify(mServiceCallback, times(onChildStartFailed ? 1 : 0)).onChildStartFailed(any());
        verify(mServiceCallback, times(onChildProcessDied ? 1 : 0)).onChildProcessDied(any());
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartedCallback() {
        runTestWithConnectionCallbacks(
                mAllocator,
                /* onChildStarted= */ true,
                /* onChildStartFailed= */ false,
                /* onChildProcessDied= */ false);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartedCallbackVariableSize() {
        runTestWithConnectionCallbacks(
                mVariableSizeAllocator,
                /* onChildStarted= */ true,
                /* onChildStartFailed= */ false,
                /* onChildProcessDied= */ false);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartedCallbackWorkaround() {
        runTestWithConnectionCallbacks(
                mWorkaroundAllocator,
                /* onChildStarted= */ true,
                /* onChildStartFailed= */ false,
                /* onChildProcessDied= */ false);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartFailedCallback() {
        runTestWithConnectionCallbacks(
                mAllocator,
                /* onChildStarted= */ false,
                /* onChildStartFailed= */ true,
                /* onChildProcessDied= */ false);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartFailedCallbackVariableSize() {
        runTestWithConnectionCallbacks(
                mVariableSizeAllocator,
                /* onChildStarted= */ false,
                /* onChildStartFailed= */ true,
                /* onChildProcessDied= */ false);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildStartFailedCallbackWorkaround() {
        runTestWithConnectionCallbacks(
                mWorkaroundAllocator,
                /* onChildStarted= */ false,
                /* onChildStartFailed= */ true,
                /* onChildProcessDied= */ false);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildProcessDiedCallback() {
        runTestWithConnectionCallbacks(
                mAllocator,
                /* onChildStarted= */ false,
                /* onChildStartFailed= */ false,
                /* onChildProcessDied= */ true);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildProcessDiedCallbackWithVariableSize() {
        runTestWithConnectionCallbacks(
                mVariableSizeAllocator,
                /* onChildStarted= */ false,
                /* onChildStartFailed= */ false,
                /* onChildProcessDied= */ true);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testOnChildProcessDiedCallbackWorkaround() {
        runTestWithConnectionCallbacks(
                mWorkaroundAllocator,
                /* onChildStarted= */ false,
                /* onChildStartFailed= */ false,
                /* onChildProcessDied= */ true);
    }

    /** Tests that the allocator clears the connection when it fails to bind/process dies. */
    private void testFreeConnection(ChildConnectionAllocator allocator, int callbackType) {
        ChildProcessConnection connection =
                allocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);

        assertNotNull(connection);
        ComponentName serviceName = mTestConnectionFactory.getAndResetLastServiceName();
        String instanceName = mTestConnectionFactory.getAndResetLastInstanceName();
        verify(connection, times(1))
                .start(
                        eq(false)
                        /* useStrongBinding= */ ,
                        any(ChildProcessConnection.ServiceCallback.class));
        assertTrue(allocator.anyConnectionAllocated());
        int onChildStartFailedExpectedCount = 0;
        int onChildProcessDiedExpectedCount = 0;
        switch (callbackType) {
            case FREE_CONNECTION_TEST_CALLBACK_START_FAILED:
                mTestConnectionFactory.simulateServiceStartFailed();
                onChildStartFailedExpectedCount = 1;
                break;
            case FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED:
                mTestConnectionFactory.simulateServiceProcessDying();
                onChildProcessDiedExpectedCount = 1;
                break;
            default:
                fail();
                break;
        }
        ShadowLooper.runUiThreadTasksIncludingDelayedTasks();
        assertFalse(allocator.anyConnectionAllocated());
        verify(mServiceCallback, never()).onChildStarted();
        verify(mServiceCallback, times(onChildStartFailedExpectedCount))
                .onChildStartFailed(connection);
        verify(mServiceCallback, times(onChildProcessDiedExpectedCount))
                .onChildProcessDied(connection);

        // Allocate a new connection to make sure we are not getting the same connection.
        connection =
                allocator.allocate(
                        /* context= */ null, /* serviceBundle= */ null, mServiceCallback);
        assertNotNull(connection);
        if (instanceName == null) {
            assertNotEquals(mTestConnectionFactory.getAndResetLastServiceName(), serviceName);
        } else {
            assertNotEquals(mTestConnectionFactory.getAndResetLastInstanceName(), instanceName);
        }
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildStartFailed() {
        testFreeConnection(mAllocator, FREE_CONNECTION_TEST_CALLBACK_START_FAILED);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildStartFailedVariableSize() {
        testFreeConnection(mVariableSizeAllocator, FREE_CONNECTION_TEST_CALLBACK_START_FAILED);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildStartFailedWorkaround() {
        testFreeConnection(mWorkaroundAllocator, FREE_CONNECTION_TEST_CALLBACK_START_FAILED);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildProcessDied() {
        testFreeConnection(mAllocator, FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildProcessDiedVariableSize() {
        testFreeConnection(mVariableSizeAllocator, FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED);
    }

    @Test
    @Feature({"ProcessManagement"})
    public void testFreeConnectionOnChildProcessDiedWorkaround() {
        testFreeConnection(mWorkaroundAllocator, FREE_CONNECTION_TEST_CALLBACK_PROCESS_DIED);
    }
}
