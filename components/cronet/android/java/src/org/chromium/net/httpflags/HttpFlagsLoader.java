// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net.httpflags;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.metrics.ScopedSysTraceEvent;
import org.chromium.net.impl.CronetManifest;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * Utilities for loading HTTP flags.
 *
 * <p>HTTP flags are a generic mechanism by which the host system (i.e. the Android system image)
 * can provide values for a variety of configuration knobs to alter the behavior of the HTTP client
 * stack. The idea is that the host system can use some kind of centralized configuration mechanism
 * to remotely push changes to these settings while collecting data on the results. This in turn
 * enables A/B experiments, progressive configuration rollouts, etc.
 *
 * <p>Currently, the interface with the host system is defined as follows:
 * <ol>
 * <li>The Android system image must provide an Android app that exposes a service matching the
 *     {@link #FLAGS_FILE_PROVIDER_INTENT_ACTION} action.
 * <li>That Android app must expose a directory named after {@link #FLAGS_FILE_DIR_NAME} under the
 *     app's {@link ApplicationInfo#deviceProtectedDataDir}.
 * <li>That directory must contain a file named after {@link #FLAGS_FILE_NAME} that must be readable
 *     by the process running {@link #load}.
 * <li>The flag values are obtained from the contents of that file. The format is a binary proto
 *     that can be read through {@link Flags#parseDelimitedFrom} - see `flags.proto` for details.
 * </ol>
 *
 * @see HttpFlagsInterceptor
 */
public final class HttpFlagsLoader {
    private HttpFlagsLoader() {}

    @VisibleForTesting
    static final String FLAGS_FILE_PROVIDER_INTENT_ACTION = "android.net.http.FLAGS_FILE_PROVIDER";

    @VisibleForTesting static final String FLAGS_FILE_DIR_NAME = "app_httpflags";
    @VisibleForTesting static final String FLAGS_FILE_NAME = "flags.binarypb";

    @VisibleForTesting public static final String TAG = "HttpFlagsLoader";
    private static ResolvedFlags sHttpFlags;
    private static String sVersion;
    private static final Object sLock = new Object();
    @VisibleForTesting public static final String LOG_FLAG_NAME = "Cronet_log_me";

    /**
     * Locates and loads the HTTP flags file from the host system.
     *
     * <p>Note that this is an expensive call.
     *
     * @return The contents of the flags file, or null if the flags file could not be loaded for any
     *     reason. In the latter case, the callee will take care of logging the failure.
     * @see ResolvedFlags
     */
    @Nullable
    @VisibleForTesting
    public static Flags load(Context context) {
        try {
            ApplicationInfo providerApplicationInfo = getProviderApplicationInfo(context);
            if (providerApplicationInfo == null) return null;
            Log.d(
                    TAG,
                    String.format(
                            "Found application exporting HTTP flags: %s",
                            providerApplicationInfo.packageName));

            File flagsFile = getFlagsFileFromProvider(providerApplicationInfo);
            Log.d(TAG, String.format("HTTP flags file path: %s", flagsFile.getAbsolutePath()));

            Flags flags = loadFlagsFile(flagsFile);
            if (flags == null) return null;
            Log.d(TAG, String.format("Successfully loaded HTTP flags: %s", flags));

            return flags;
        } catch (RuntimeException exception) {
            Log.i(TAG, "Unable to load HTTP flags file", exception);
            return null;
        }
    }

    @VisibleForTesting
    public static void flushHttpFlags() {
        sHttpFlags = null;
        sVersion = null;
    }

    /**
     * Fetches and caches the available httpflags for the version provided
     *
     * <p>Never returns null: if HTTP flags were not loaded, will return an empty set of flags.
     *
     * <p>Changing the context will not invalidate the cache. Once the httpflags has been loaded
     * then it will be cached indefinitely.
     */
    public static ResolvedFlags getHttpFlags(
            Context context, String version, boolean isLoadedFromApi) {
        synchronized (sLock) {
            assert (sHttpFlags == null) == (sVersion == null);
            if (sVersion != null && !version.equals(sVersion)) {
                throw new IllegalStateException(
                        "getHttpFlags() called multiple times with different versions");
            }
            if (sHttpFlags != null) return sHttpFlags;
            sVersion = version;
            try (var loadingHttpFlagsTraceEvent =
                    ScopedSysTraceEvent.scoped("HttpFlagsLoader#getHttpFlags loading flags")) {
                Flags flags;
                if (!CronetManifest.shouldReadHttpFlags(context)) {
                    Log.d(TAG, "Not loading HTTP flags because they are disabled in the manifest");
                    flags = null;
                } else {
                    flags = HttpFlagsLoader.load(context);
                }
                sHttpFlags =
                        ResolvedFlags.resolve(
                                flags != null ? flags : Flags.newBuilder().build(),
                                context.getPackageName(),
                                version);
                ResolvedFlags.Value logMe = sHttpFlags.flags().get(LOG_FLAG_NAME);
                if (logMe != null) {
                    Log.i(
                            TAG,
                            String.format(
                                    "HTTP flags log line (%s): %s",
                                    isLoadedFromApi ? "API" : "Impl", logMe.getStringValue()));
                }
                return sHttpFlags;
            }
        }
    }

    @Nullable
    private static ApplicationInfo getProviderApplicationInfo(Context context) {
        try (var traceEvent =
                ScopedSysTraceEvent.scoped("HttpFlagsLoader#getProviderApplicationInfo")) {
            ResolveInfo resolveInfo =
                    context.getPackageManager()
                            .resolveService(
                                    new Intent(FLAGS_FILE_PROVIDER_INTENT_ACTION),
                                    // Make sure we only read flags files that are written by a
                                    // package from the system image. This prevents random
                                    // third-party apps from being able to inject flags into other
                                    // apps, which would be a security risk.
                                    PackageManager.MATCH_SYSTEM_ONLY);
            if (resolveInfo == null) {
                Log.i(
                        TAG,
                        "Unable to resolve the HTTP flags file provider package. This is expected"
                                + " if the host system is not set up to provide HTTP flags.");
                return null;
            }

            return resolveInfo.serviceInfo.applicationInfo;
        }
    }

    private static File getFlagsFileFromProvider(ApplicationInfo providerApplicationInfo) {
        return new File(
                new File(
                        new File(
                                Build.VERSION.SDK_INT >= 24
                                        ? providerApplicationInfo.deviceProtectedDataDir
                                        : providerApplicationInfo.dataDir),
                        FLAGS_FILE_DIR_NAME),
                FLAGS_FILE_NAME);
    }

    @Nullable
    private static Flags loadFlagsFile(File file) {
        try (var traceEvent = ScopedSysTraceEvent.scoped("HttpFlagsLoader#loadFlagsFile")) {
            try (FileInputStream fileInputStream = new FileInputStream(file)) {
                return Flags.parseDelimitedFrom(fileInputStream);
            } catch (FileNotFoundException exception) {
                Log.i(
                        TAG,
                        String.format(
                                "HTTP flags file `%s` is missing. This is expected if HTTP flags"
                                    + " functionality is currently disabled in the host system.",
                                file.getPath()));
                return null;
            } catch (IOException exception) {
                throw new RuntimeException("Unable to read HTTP flags file", exception);
            }
        }
    }
}
