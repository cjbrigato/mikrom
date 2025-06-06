// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.SparseArray;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.base.process_launcher.ChildConnectionAllocator;
import org.chromium.base.process_launcher.ChildProcessConnection;
import org.chromium.base.process_launcher.ChildProcessLauncher;
import org.chromium.base.process_launcher.IChildProcessService;
import org.chromium.base.process_launcher.IFileDescriptorInfo;

import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

import javax.annotation.concurrent.GuardedBy;

/** Helper class for launching test client processes for multiprocess unit tests. */
@JNINamespace("base::android")
public final class MultiprocessTestClientLauncher {
    private static final String TAG = "MProcTCLauncher";

    private static final int CONNECTION_TIMEOUT_MS = 10 * 1000;

    private static final SparseArray<MultiprocessTestClientLauncher> sPidToLauncher =
            new SparseArray<>();

    private static final SparseArray<Boolean> sPidToCleanExit = new SparseArray<>();

    private static final SparseArray<Integer> sPidToMainResult = new SparseArray<>();

    private static final Object sLauncherHandlerInitLock = new Object();
    private static Handler sLauncherHandler;

    private static ChildConnectionAllocator sConnectionAllocator;

    private final ITestCallback.Stub mCallback =
            new ITestCallback.Stub() {
                @Override
                public void childConnected(ITestController controller) {
                    mTestController = controller;
                    // This method can be called before onServiceConnected below has set the PID.
                    // Wait for mPid to be set before notifying.
                    try {
                        mPidReceived.await();
                    } catch (InterruptedException ie) {
                        Log.e(TAG, "Interrupted while waiting for connection PID.");
                        return;
                    }
                    // Now we are fully initialized, notify clients.
                    mConnectedLock.lock();
                    try {
                        mConnected = true;
                        mConnectedCondition.signal();
                    } finally {
                        mConnectedLock.unlock();
                    }
                }

                @Override
                public void mainReturned(int returnCode) {
                    mMainReturnCodeLock.lock();
                    try {
                        mMainReturnCode = returnCode;
                        mMainReturnCodeCondition.signal();
                    } finally {
                        mMainReturnCodeLock.unlock();
                    }

                    // Also store the return code in a map as the connection might get disconnected
                    // before waitForMainToReturn is called and then we would not have a way to
                    // retrieve the connection.
                    sPidToMainResult.put(mPid, returnCode);
                }
            };

    private final ChildProcessLauncher.Delegate mLauncherDelegate =
            new ChildProcessLauncher.Delegate() {
                @Override
                public void onConnectionEstablished(ChildProcessConnection connection) {
                    assert isRunningOnLauncherThread();
                    int pid = connection.getPid();
                    sPidToLauncher.put(pid, MultiprocessTestClientLauncher.this);
                    mPid = pid;
                    mPidReceived.countDown();
                }

                @Override
                public void onConnectionLost(ChildProcessConnection connection) {
                    assert isRunningOnLauncherThread();
                    assert sPidToLauncher.get(connection.getPid())
                            == MultiprocessTestClientLauncher.this;
                    sPidToCleanExit.put(connection.getPid(), connection.hasCleanExit());
                    sPidToLauncher.remove(connection.getPid());
                }
            };

    private final CountDownLatch mPidReceived = new CountDownLatch(1);

    private final ChildProcessLauncher mLauncher;

    private final ReentrantLock mConnectedLock = new ReentrantLock();
    private final Condition mConnectedCondition = mConnectedLock.newCondition();

    @GuardedBy("mConnectedLock")
    private boolean mConnected;

    private IChildProcessService mService;
    private int mPid;
    private ITestController mTestController;

    private final ReentrantLock mMainReturnCodeLock = new ReentrantLock();
    private final Condition mMainReturnCodeCondition = mMainReturnCodeLock.newCondition();

    // The return code returned by the service's main method.
    // null if the service has not sent it yet.
    @GuardedBy("mMainReturnCodeLock")
    private Integer mMainReturnCode;

    private MultiprocessTestClientLauncher(
            String[] commandLine, IFileDescriptorInfo[] filesToMap, IBinder binderBox) {
        assert isRunningOnLauncherThread();

        if (sConnectionAllocator == null) {
            sConnectionAllocator =
                    ChildConnectionAllocator.create(
                            ContextUtils.getApplicationContext(),
                            sLauncherHandler,
                            null,
                            "org.chromium.native_test",
                            "org.chromium.base.MultiprocessTestClientService",
                            "org.chromium.native_test.NUM_TEST_CLIENT_SERVICES",
                            /* bindToCaller= */ false,
                            /* bindAsExternalService= */ false,
                            /* useStrongBinding= */ false,
                            /* fallbackToNextSlot= */ false,
                            /* isSandboxedForHistograms= */ false);
        }

        mLauncher =
                new ChildProcessLauncher(
                        sLauncherHandler,
                        mLauncherDelegate,
                        commandLine,
                        filesToMap,
                        sConnectionAllocator,
                        Arrays.asList(mCallback),
                        binderBox);
    }

    private boolean waitForConnection(long timeoutMs) {
        assert !isRunningOnLauncherThread();

        long timeoutNs = TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        mConnectedLock.lock();
        try {
            while (!mConnected) {
                if (timeoutNs <= 0L) {
                    return false;
                }
                try {
                    timeoutNs = mConnectedCondition.awaitNanos(timeoutNs);
                } catch (InterruptedException ie) {
                    Log.e(TAG, "Interrupted while waiting for connection.");
                }
            }
        } finally {
            mConnectedLock.unlock();
        }
        return true;
    }

    private Integer getMainReturnCode(long timeoutMs) {
        assert isRunningOnLauncherThread();

        long timeoutNs = TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        mMainReturnCodeLock.lock();
        try {
            while (mMainReturnCode == null) {
                if (timeoutNs <= 0L) {
                    return null;
                }
                try {
                    timeoutNs = mMainReturnCodeCondition.awaitNanos(timeoutNs);
                } catch (InterruptedException ie) {
                    Log.e(TAG, "Interrupted while waiting for main return code.");
                }
            }
            return mMainReturnCode;
        } finally {
            mMainReturnCodeLock.unlock();
        }
    }

    /**
     * Spawns and connects to a child process. May not be called from the main thread.
     *
     * @param commandLine the child process command line argv.
     * @return the PID of the started process or 0 if the process could not be started.
     */
    @CalledByNative
    private static int launchClient(
            final String[] commandLine,
            final IFileDescriptorInfo[] filesToMap,
            final IBinder binderBox) {
        assert Looper.myLooper() != Looper.getMainLooper();

        initLauncherThread();

        final MultiprocessTestClientLauncher launcher =
                runOnLauncherAndGetResult(
                        () ->
                                createAndStartLauncherOnLauncherThread(
                                        commandLine, filesToMap, binderBox));
        if (launcher == null) {
            return 0;
        }

        if (!launcher.waitForConnection(CONNECTION_TIMEOUT_MS)) {
            return 0; // Timed-out.
        }

        return runOnLauncherAndGetResult(
                () -> {
                    int pid = launcher.mLauncher.getPid();
                    assert pid > 0;
                    sPidToLauncher.put(pid, launcher);
                    return pid;
                });
    }

    private static MultiprocessTestClientLauncher createAndStartLauncherOnLauncherThread(
            String[] commandLine, IFileDescriptorInfo[] filesToMap, final IBinder binderBox) {
        assert isRunningOnLauncherThread();

        MultiprocessTestClientLauncher launcher =
                new MultiprocessTestClientLauncher(commandLine, filesToMap, binderBox);
        if (!launcher.mLauncher.start(
                /* setupConnection= */ true, /* queueIfNoFreeConnection= */ true)) {
            return null;
        }

        return launcher;
    }

    /**
     * Blocks until the main method invoked by a previous call to launchClient terminates or until
     * the specified time-out expires.
     * Returns immediately if main has already returned.
     * @param pid the process ID that was returned by the call to launchClient
     * @param timeoutMs the timeout in milliseconds after which the method returns even if main has
     *        not returned.
     * @return the return code returned by the main method or whether it timed-out.
     */
    @CalledByNative
    private static MainReturnCodeResult waitForMainToReturn(final int pid, final int timeoutMs) {
        return runOnLauncherAndGetResult(() -> waitForMainToReturnOnLauncherThread(pid, timeoutMs));
    }

    private static MainReturnCodeResult waitForMainToReturnOnLauncherThread(
            int pid, int timeoutMs) {
        assert isRunningOnLauncherThread();

        MultiprocessTestClientLauncher launcher = sPidToLauncher.get(pid);
        // The launcher can be null if it got cleaned-up (because the connection was lost) before
        // this gets called.
        if (launcher != null) {
            Integer mainResult = launcher.getMainReturnCode(timeoutMs);
            launcher.mLauncher.stop();
            return mainResult == null
                    ? MainReturnCodeResult.createTimeoutMainResult()
                    : MainReturnCodeResult.createMainResult(mainResult);
        }

        Integer mainResult = sPidToMainResult.get(pid);
        if (mainResult == null) {
            Log.e(TAG, "waitForMainToReturn called on unknown connection for pid " + pid);
            return null;
        }
        sPidToMainResult.remove(pid);
        return MainReturnCodeResult.createMainResult(mainResult);
    }

    @CalledByNative
    private static boolean terminate(final int pid, final int exitCode, final boolean wait) {
        return runOnLauncherAndGetResult(() -> terminateOnLauncherThread(pid, exitCode, wait));
    }

    private static boolean terminateOnLauncherThread(int pid, int exitCode, boolean wait) {
        assert isRunningOnLauncherThread();

        MultiprocessTestClientLauncher launcher = sPidToLauncher.get(pid);
        if (launcher == null) {
            Log.e(TAG, "terminate called on unknown launcher for pid " + pid);
            return false;
        }
        try {
            if (wait) {
                launcher.mTestController.forceStopSynchronous(exitCode);
            } else {
                launcher.mTestController.forceStop(exitCode);
            }
        } catch (RemoteException e) {
            // We expect this failure, since the forceStop's service implementation calls
            // System.exit().
        }
        return true;
    }

    private static void initLauncherThread() {
        synchronized (sLauncherHandlerInitLock) {
            if (sLauncherHandler != null) return;

            HandlerThread launcherThread = new HandlerThread("LauncherThread");
            launcherThread.start();
            sLauncherHandler = new Handler(launcherThread.getLooper());
        }
    }

    @CalledByNative
    private static boolean hasCleanExit(final int pid) {
        return runOnLauncherAndGetResult(() -> hasCleanExitOnLauncherThread(pid));
    }

    private static boolean hasCleanExitOnLauncherThread(int pid) {
        assert isRunningOnLauncherThread();

        MultiprocessTestClientLauncher launcher = sPidToLauncher.get(pid);
        if (launcher == null) {
            return sPidToCleanExit.get(pid, false);
        }
        return launcher.mLauncher.getConnection().hasCleanExit();
    }

    /** Does not take ownership of of fds. */
    @CalledByNative
    private static IFileDescriptorInfo[] makeFdInfoArray(int[] keys, int[] fds) {
        IFileDescriptorInfo[] fdInfos = new IFileDescriptorInfo[keys.length];
        for (int i = 0; i < keys.length; i++) {
            IFileDescriptorInfo fdInfo = makeFdInfo(keys[i], fds[i]);
            if (fdInfo == null) {
                Log.e(TAG, "Failed to make file descriptor (" + keys[i] + ", " + fds[i] + ").");
                return null;
            }
            fdInfos[i] = fdInfo;
        }
        return fdInfos;
    }

    private static IFileDescriptorInfo makeFdInfo(int id, int fd) {
        ParcelFileDescriptor parcelableFd = null;
        try {
            parcelableFd = ParcelFileDescriptor.fromFd(fd);
        } catch (IOException e) {
            Log.e(TAG, "Invalid FD provided for process connection, aborting connection.", e);
            return null;
        }
        IFileDescriptorInfo fdInfo = new IFileDescriptorInfo();
        fdInfo.id = id;
        fdInfo.fd = parcelableFd;
        return fdInfo;
    }

    private static boolean isRunningOnLauncherThread() {
        return sLauncherHandler.getLooper() == Looper.myLooper();
    }

    private static <RT> RT runOnLauncherAndGetResult(Callable<RT> callable) {
        if (isRunningOnLauncherThread()) {
            try {
                return callable.call();
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }
        try {
            FutureTask<RT> task = new FutureTask<RT>(callable);
            sLauncherHandler.post(task);
            return task.get();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
