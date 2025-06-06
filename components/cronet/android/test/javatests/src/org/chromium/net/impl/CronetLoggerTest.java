// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net.impl;

import static android.os.Process.THREAD_PRIORITY_DEFAULT;

import static com.google.common.truth.Truth.assertThat;

import static org.chromium.net.CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP;
import static org.chromium.net.truth.UrlResponseInfoSubject.assertThat;

import android.content.Context;
import android.os.Build;

import androidx.test.filters.SmallTest;

import org.json.JSONException;
import org.json.JSONObject;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.RuleChain;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import org.chromium.base.test.util.DoNotBatch;
import org.chromium.net.ConnectionCloseSource;
import org.chromium.net.CronetEngine;
import org.chromium.net.CronetLoggerTestRule;
import org.chromium.net.CronetTestRule;
import org.chromium.net.CronetTestRule.CronetImplementation;
import org.chromium.net.CronetTestRule.IgnoreFor;
import org.chromium.net.CronetTestRule.RequiresMinAndroidApi;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.Http2TestServer;
import org.chromium.net.NativeTestServer;
import org.chromium.net.TestBidirectionalStreamCallback;
import org.chromium.net.TestUploadDataProvider;
import org.chromium.net.TestUrlRequestCallback;
import org.chromium.net.UrlRequest;
import org.chromium.net.impl.CronetEngineBuilderImpl.HttpCacheMode;
import org.chromium.net.impl.CronetLogger.CronetEngineBuilderInfo;
import org.chromium.net.impl.CronetLogger.CronetSource;
import org.chromium.net.impl.CronetLogger.CronetTrafficInfo;
import org.chromium.net.impl.CronetLogger.CronetVersion;

import java.time.Duration;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Map.Entry;

/** Test logging functionalities. */
@DoNotBatch(reason = "Some logging is done from one-time static initialization")
@RunWith(JUnit4.class)
@RequiresMinAndroidApi(Build.VERSION_CODES.O)
@IgnoreFor(
        implementations = {CronetImplementation.FALLBACK, CronetImplementation.AOSP_PLATFORM},
        reason = "CronetLoggerTestRule is supported only by the native implementation.")
public final class CronetLoggerTest {
    private final CronetTestRule mTestRule = CronetTestRule.withManualEngineStartup();
    private final CronetLoggerTestRule<TestLogger> mLoggerTestRule =
            new CronetLoggerTestRule<>(TestLogger.class);

    @Rule public final RuleChain chain = RuleChain.outerRule(mTestRule).around(mLoggerTestRule);

    private TestLogger mTestLogger;
    private Context mContext;

    @Before
    public void setUp() {
        mContext = mTestRule.getTestFramework().getContext();
        mTestLogger = mLoggerTestRule.mTestLogger;
        assertThat(NativeTestServer.startNativeTestServer(mContext)).isTrue();
    }

    @After
    public void tearDown() {
        mTestLogger = null;
        NativeTestServer.shutdownNativeTestServer();
    }

    @Test
    @SmallTest
    public void testCronetEngineInfoCreation() {
        CronetEngineBuilderImpl builder = new NativeCronetEngineBuilderImpl(mContext);
        CronetEngineBuilderInfo builderInfo = builder.toLoggerInfo();
        assertThat(builderInfo.isPublicKeyPinningBypassForLocalTrustAnchorsEnabled())
                .isEqualTo(builder.publicKeyPinningBypassForLocalTrustAnchorsEnabled());
        assertThat(builderInfo.getUserAgent()).isEqualTo(builder.getUserAgent());
        assertThat(builderInfo.getStoragePath()).isEqualTo(builder.storagePath());
        assertThat(builderInfo.isQuicEnabled()).isEqualTo(builder.quicEnabled());
        assertThat(builderInfo.isHttp2Enabled()).isEqualTo(builder.http2Enabled());
        assertThat(builderInfo.isBrotliEnabled()).isEqualTo(builder.brotliEnabled());
        assertThat(builderInfo.getHttpCacheMode()).isEqualTo(builder.publicBuilderHttpCacheMode());
        assertThat(builderInfo.getExperimentalOptions()).isEqualTo(builder.experimentalOptions());
        assertThat(builderInfo.isNetworkQualityEstimatorEnabled())
                .isEqualTo(builder.networkQualityEstimatorEnabled());
        assertThat(builderInfo.getThreadPriority())
                .isEqualTo(CronetEngineBuilderImpl.NETWORK_THREAD_PRIORITY);
    }

    @Test
    @SmallTest
    public void testCronetVersionCreation() {
        final int major = 100;
        final int minor = 0;
        final int build = 1;
        final int patch = 33;
        final String version = String.format(Locale.US, "%d.%d.%d.%d", major, minor, build, patch);
        final CronetVersion parsedVersion = new CronetVersion(version);
        assertThat(parsedVersion.getMajorVersion()).isEqualTo(major);
        assertThat(parsedVersion.getMinorVersion()).isEqualTo(minor);
        assertThat(parsedVersion.getBuildVersion()).isEqualTo(build);
        assertThat(parsedVersion.getPatchVersion()).isEqualTo(patch);
    }

    @Test
    @SmallTest
    public void testHttpCacheModeEnum() {
        final int[] publicBuilderHttpCacheModes = {
            CronetEngine.Builder.HTTP_CACHE_DISABLED,
            CronetEngine.Builder.HTTP_CACHE_IN_MEMORY,
            CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP,
            CronetEngine.Builder.HTTP_CACHE_DISK
        };
        for (int publicBuilderHttpCacheMode : publicBuilderHttpCacheModes) {
            HttpCacheMode cacheModeEnum =
                    HttpCacheMode.fromPublicBuilderCacheMode(publicBuilderHttpCacheMode);
            assertThat(cacheModeEnum.toPublicBuilderCacheMode())
                    .isEqualTo(publicBuilderHttpCacheMode);
        }
    }

    @Test
    @SmallTest
    public void testSetLoggerForTesting() {
        CronetLogger logger = CronetLoggerFactory.createLogger(mContext, null);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(0);
        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(0);

        // We don't care about what's being logged.
        logger.logCronetTrafficInfo(0, null);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(0);
        logger.logCronetEngineCreation(0, null, null, null);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
    }

    @Test
    @SmallTest
    public void testTelemetryDefaultEnabled() throws JSONException {
        final String url = NativeTestServer.getEchoBodyURL();

        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        CronetEngine engine = mTestRule.getTestFramework().startEngine();
        UrlRequest.Builder requestBuilder =
                engine.newUrlRequestBuilder(url, callback, callback.getExecutor());
        UrlRequest request = requestBuilder.build();
        request.start();
        callback.blockForDone();
        assertThat(callback.mOnCanceledCalled).isFalse();
        assertThat(callback.mOnErrorCalled).isFalse();
        mTestLogger.waitForLogCronetTrafficInfo();

        // Test-logger should be bypassed.
        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
    }

    @Test
    @SmallTest
    public void testCronetEngineBuilderInitializedNotLoggedFromImpl() {
        // The test framework bypasses the logic in CronetEngine.Builder, and the logic in
        // CronetEngineBuilderImpl should not log anything since it should see the API code is up to
        // date.
        assertThat(mTestLogger.callsToLogCronetEngineBuilderInitializedInfo()).isEqualTo(0);
        mTestRule
                .getTestFramework()
                .createNewSecondaryBuilder(mTestRule.getTestFramework().getContext());
        assertThat(mTestLogger.callsToLogCronetEngineBuilderInitializedInfo()).isEqualTo(0);
    }

    @Test
    @SmallTest
    public void testCronetEngineBuilderInitializedLoggedFromImplIfApiIsTooOld() {
        var originalApiLevel = CronetEngineBuilderImpl.sApiLevel;
        try {
            CronetEngineBuilderImpl.sApiLevel = 29;
            assertThat(mTestLogger.callsToLogCronetEngineBuilderInitializedInfo()).isEqualTo(0);
            var builder =
                    mTestRule
                            .getTestFramework()
                            .createNewSecondaryBuilder(mTestRule.getTestFramework().getContext());
            // The test framework bypasses the logic in CronetEngine.Builder, so we know this is
            // coming from the impl.
            assertThat(mTestLogger.callsToLogCronetEngineBuilderInitializedInfo()).isEqualTo(1);
            var info = mTestLogger.getLastCronetEngineBuilderInitializedInfo();
            assertThat(info).isNotNull();
            assertThat(info.cronetInitializationRef).isNotEqualTo(0);
            assertThat(info.author)
                    .isEqualTo(CronetLogger.CronetEngineBuilderInitializedInfo.Author.IMPL);
            assertThat(info.engineBuilderCreatedLatencyMillis).isAtLeast(0);
            assertThat(info.source).isNotEqualTo(CronetSource.CRONET_SOURCE_UNSPECIFIED);
            assertThat(info.creationSuccessful).isTrue();
            assertThat(info.apiVersion.getMajorVersion()).isGreaterThan(0);
            assertThat(info.implVersion.getMajorVersion()).isGreaterThan(0);
            assertThat(info.uid).isGreaterThan(0);

            builder.build();
            final CronetEngineBuilderInfo builderInfo =
                    mTestLogger.getLastCronetEngineBuilderInfo();
            assertThat(builderInfo).isNotNull();
            assertThat(builderInfo.getCronetInitializationRef())
                    .isEqualTo(info.cronetInitializationRef);

            mTestLogger.waitForCronetInitializedInfo();
            var cronetInitializedInfo = mTestLogger.getLastCronetInitializedInfo();
            assertThat(cronetInitializedInfo).isNotNull();
            assertThat(cronetInitializedInfo.cronetInitializationRef)
                    .isEqualTo(info.cronetInitializationRef);
        } finally {
            CronetEngineBuilderImpl.sApiLevel = originalApiLevel;
        }
    }

    @Test
    @SmallTest
    public void testCronetEngineBuilderInitializedLoggedFromApi() {
        assertThat(mTestLogger.callsToLogCronetEngineBuilderInitializedInfo()).isEqualTo(0);
        // The test framework bypasses the logic in CronetEngine.Builder, so we have to call it
        // directly. We want to use the test framework context though for things like
        // intercepting manifest reads.
        // TODO(crbug.com/41494362): this is ugly. Ideally the test framework should be
        // refactored to stop violating the Single Responsibility Principle (e.g. Context
        // management and implementation selection should be separated)
        var builder = new CronetEngine.Builder(mTestRule.getTestFramework().getContext());
        assertThat(mTestLogger.callsToLogCronetEngineBuilderInitializedInfo()).isEqualTo(1);
        var info = mTestLogger.getLastCronetEngineBuilderInitializedInfo();
        assertThat(info).isNotNull();
        assertThat(info.cronetInitializationRef).isNotEqualTo(0);
        assertThat(info.author)
                .isEqualTo(CronetLogger.CronetEngineBuilderInitializedInfo.Author.API);
        assertThat(info.engineBuilderCreatedLatencyMillis).isAtLeast(0);
        assertThat(info.source).isNotEqualTo(CronetSource.CRONET_SOURCE_UNSPECIFIED);
        assertThat(info.creationSuccessful).isTrue();
        assertThat(info.apiVersion.getMajorVersion()).isGreaterThan(0);
        assertThat(info.implVersion.getMajorVersion()).isGreaterThan(0);
        assertThat(info.uid).isGreaterThan(0);

        builder.build();
        final CronetEngineBuilderInfo builderInfo = mTestLogger.getLastCronetEngineBuilderInfo();
        assertThat(builderInfo).isNotNull();
        assertThat(builderInfo.getCronetInitializationRef())
                .isEqualTo(info.cronetInitializationRef);

        mTestLogger.waitForCronetInitializedInfo();
        var cronetInitializedInfo = mTestLogger.getLastCronetInitializedInfo();
        assertThat(cronetInitializedInfo).isNotNull();
        assertThat(cronetInitializedInfo.cronetInitializationRef)
                .isEqualTo(info.cronetInitializationRef);
    }

    @Test
    @SmallTest
    public void testEngineCreation() throws JSONException {
        JSONObject staleDns =
                new JSONObject()
                        .put("enable", true)
                        .put("delay_ms", 0)
                        .put("allow_other_network", true)
                        .put("persist_to_disk", true)
                        .put("persist_delay_ms", 0);
        final JSONObject jsonExperimentalOptions = new JSONObject().put("StaleDNS", staleDns);
        final String experimentalOptions = jsonExperimentalOptions.toString();
        final boolean isPublicKeyPinningBypassForLocalTrustAnchorsEnabled = false;
        final String userAgent = "myUserAgent";
        final String storagePath = CronetTestRule.getTestStorage(mContext);
        final boolean isQuicEnabled = true;
        final boolean isHttp2Enabled = false;
        final boolean isBrotliEnabled = true;
        final int cacheMode = HTTP_CACHE_DISK_NO_HTTP;
        final boolean isNetworkQualityEstimatorEnabled = true;
        final int threadPriority = THREAD_PRIORITY_DEFAULT;

        mTestRule
                .getTestFramework()
                .applyEngineBuilderPatch(
                        (builder) -> {
                            builder.setExperimentalOptions(experimentalOptions);
                            builder.enablePublicKeyPinningBypassForLocalTrustAnchors(
                                    isPublicKeyPinningBypassForLocalTrustAnchorsEnabled);
                            builder.setUserAgent(userAgent);
                            builder.setStoragePath(storagePath);
                            builder.enableQuic(isQuicEnabled);
                            builder.enableHttp2(isHttp2Enabled);
                            builder.enableBrotli(isBrotliEnabled);
                            builder.enableHttpCache(cacheMode, 0);
                            builder.enableNetworkQualityEstimator(isNetworkQualityEstimatorEnabled);
                            builder.setThreadPriority(threadPriority);
                        });

        mTestRule.getTestFramework().startEngine();
        final CronetEngineBuilderInfo builderInfo = mTestLogger.getLastCronetEngineBuilderInfo();
        final CronetVersion version = mTestLogger.getLastCronetVersion();
        final CronetSource source = mTestLogger.getLastCronetSource();

        assertThat(builderInfo.isPublicKeyPinningBypassForLocalTrustAnchorsEnabled())
                .isEqualTo(isPublicKeyPinningBypassForLocalTrustAnchorsEnabled);
        assertThat(builderInfo.getUserAgent()).isEqualTo(userAgent);
        assertThat(builderInfo.getStoragePath()).isEqualTo(storagePath);
        assertThat(builderInfo.isQuicEnabled()).isEqualTo(isQuicEnabled);
        assertThat(builderInfo.isHttp2Enabled()).isEqualTo(isHttp2Enabled);
        assertThat(builderInfo.isBrotliEnabled()).isEqualTo(isBrotliEnabled);
        assertThat(builderInfo.getHttpCacheMode()).isEqualTo(cacheMode);
        assertThat(builderInfo.getExperimentalOptions()).isEqualTo(experimentalOptions);
        assertThat(builderInfo.isNetworkQualityEstimatorEnabled())
                .isEqualTo(isNetworkQualityEstimatorEnabled);
        assertThat(builderInfo.getThreadPriority()).isEqualTo(threadPriority);
        assertThat(version.toString()).isEqualTo(ImplVersion.getCronetVersion());
        if (mTestRule.testingJavaImpl()) {
            assertThat(source).isEqualTo(CronetSource.CRONET_SOURCE_FALLBACK);
        } else {
            assertThat(source).isEqualTo(CronetSource.CRONET_SOURCE_STATICALLY_LINKED);
        }

        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(0);
    }

    @Test
    @SmallTest
    public void testCronetInitializedInfo() {
        // Creating another builder to ensure the cronet initialization ref allocation goes through
        // TestLogger.
        mTestRule
                .getTestFramework()
                .createNewSecondaryBuilder(mTestRule.getTestFramework().getContext())
                .build();
        mTestLogger.waitForCronetInitializedInfo();
        var cronetInitializedInfo = mTestLogger.getLastCronetInitializedInfo();
        assertThat(cronetInitializedInfo).isNotNull();
        assertThat(cronetInitializedInfo.cronetInitializationRef).isNotEqualTo(0);
        assertThat(cronetInitializedInfo.engineCreationLatencyMillis).isAtLeast(0);
        assertThat(cronetInitializedInfo.engineAsyncLatencyMillis).isAtLeast(0);
    }

    @Test
    @SmallTest
    public void testEngineCreationAndTrafficInfoEngineId() throws Exception {
        final String url = "www.example.com";
        CronetEngine engine = mTestRule.getTestFramework().startEngine();
        final long engineId = mTestLogger.getLastCronetEngineId();

        TestUrlRequestCallback callback1 = new TestUrlRequestCallback();
        UrlRequest.Builder requestBuilder1 =
                engine.newUrlRequestBuilder(url, callback1, callback1.getExecutor());
        UrlRequest request1 = requestBuilder1.build();
        TestUrlRequestCallback callback2 = new TestUrlRequestCallback();
        UrlRequest.Builder requestBuilder2 =
                engine.newUrlRequestBuilder(url, callback2, callback2.getExecutor());
        UrlRequest request2 = requestBuilder2.build();

        request1.start();
        callback1.blockForDone();
        mTestLogger.waitForLogCronetTrafficInfo();
        final long request1Id = mTestLogger.getLastCronetRequestId();

        request2.start();
        callback2.blockForDone();
        mTestLogger.waitForLogCronetTrafficInfo();
        final long request2Id = mTestLogger.getLastCronetRequestId();

        assertThat(request1Id).isEqualTo(engineId);
        assertThat(request2Id).isEqualTo(engineId);

        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(2);
    }

    @Test
    @SmallTest
    public void testMultipleEngineCreationAndTrafficInfoEngineId() throws Exception {
        final String url = "www.example.com";
        ExperimentalCronetEngine.Builder engineBuilder =
                mTestRule
                        .getTestFramework()
                        .createNewSecondaryBuilder(mTestRule.getTestFramework().getContext());

        CronetEngine engine1 = engineBuilder.build();
        final long engine1Id = mTestLogger.getLastCronetEngineId();
        CronetEngine engine2 = engineBuilder.build();
        final long engine2Id = mTestLogger.getLastCronetEngineId();

        try {
            TestUrlRequestCallback callback1 = new TestUrlRequestCallback();
            UrlRequest.Builder requestBuilder1 =
                    engine1.newUrlRequestBuilder(url, callback1, callback1.getExecutor());
            UrlRequest request1 = requestBuilder1.build();
            TestUrlRequestCallback callback2 = new TestUrlRequestCallback();
            UrlRequest.Builder requestBuilder2 =
                    engine2.newUrlRequestBuilder(url, callback2, callback2.getExecutor());
            UrlRequest request2 = requestBuilder2.build();

            request1.start();
            callback1.blockForDone();
            mTestLogger.waitForLogCronetTrafficInfo();
            final long request1Id = mTestLogger.getLastCronetRequestId();

            request2.start();
            callback2.blockForDone();
            mTestLogger.waitForLogCronetTrafficInfo();
            final long request2Id = mTestLogger.getLastCronetRequestId();

            assertThat(request1Id).isEqualTo(engine1Id);
            assertThat(request2Id).isEqualTo(engine2Id);

            assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(2);
            assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(2);
        } finally {
            engine1.shutdown();
            engine2.shutdown();
        }
    }

    @Test
    @SmallTest
    public void testSuccessfulRequestNative() throws Exception {
        final String url = NativeTestServer.getEchoMethodURL();
        CronetEngine engine = mTestRule.getTestFramework().startEngine();

        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder requestBuilder =
                engine.newUrlRequestBuilder(url, callback, callback.getExecutor());
        UrlRequest request = requestBuilder.build();
        request.start();
        callback.blockForDone();
        assertThat(callback.mOnCanceledCalled).isFalse();
        assertThat(callback.mOnErrorCalled).isFalse();
        mTestLogger.waitForLogCronetTrafficInfo();

        final CronetTrafficInfo trafficInfo = mTestLogger.getLastCronetTrafficInfo();
        assertThat(trafficInfo.getRequestHeaderSizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getRequestBodySizeInBytes()).isNotEqualTo(0);
        assertThat(trafficInfo.getResponseHeaderSizeInBytes()).isNotEqualTo(0);
        assertThat(trafficInfo.getResponseBodySizeInBytes()).isNotEqualTo(0);
        assertThat(trafficInfo.getResponseStatusCode()).isEqualTo(200);
        assertThat(trafficInfo.getHeadersLatency()).isNotEqualTo(Duration.ofSeconds(0));
        assertThat(trafficInfo.getTotalLatency()).isNotEqualTo(Duration.ofSeconds(0));
        assertThat(trafficInfo.getNegotiatedProtocol()).isNotNull();
        assertThat(trafficInfo.wasConnectionMigrationAttempted()).isFalse();
        assertThat(trafficInfo.didConnectionMigrationSucceed()).isFalse();
        assertThat(trafficInfo.getTerminalState())
                .isEqualTo(CronetTrafficInfo.RequestTerminalState.SUCCEEDED);
        assertThat(trafficInfo.getNonfinalUserCallbackExceptionCount()).isEqualTo(0);
        assertThat(trafficInfo.getReadCount()).isGreaterThan(0);
        assertThat(trafficInfo.getOnUploadReadCount()).isEqualTo(0);
        assertThat(trafficInfo.getIsBidiStream()).isFalse();
        assertThat(trafficInfo.getFinalUserCallbackThrew()).isFalse();

        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
    }

    @Test
    @SmallTest
    public void testFailedRequestNative() throws Exception {
        final String url = "www.unreachable-url.com";
        CronetEngine engine = mTestRule.getTestFramework().startEngine();

        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder requestBuilder =
                engine.newUrlRequestBuilder(url, callback, callback.getExecutor());
        UrlRequest request = requestBuilder.build();
        request.start();
        callback.blockForDone();
        assertThat(callback.mOnCanceledCalled).isFalse();
        assertThat(callback.mOnErrorCalled).isTrue();
        mTestLogger.waitForLogCronetTrafficInfo();

        final CronetTrafficInfo trafficInfo = mTestLogger.getLastCronetTrafficInfo();
        assertThat(trafficInfo.getRequestHeaderSizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getRequestBodySizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getResponseHeaderSizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getResponseBodySizeInBytes()).isEqualTo(0);
        // When a request fails before hitting the server all these values won't be populated in
        // the actual code. Check that the logger sets them to some known defaults before
        // logging.
        assertThat(trafficInfo.getResponseStatusCode()).isEqualTo(0);
        assertThat(trafficInfo.getNegotiatedProtocol()).isEmpty();
        assertThat(trafficInfo.wasConnectionMigrationAttempted()).isFalse();
        assertThat(trafficInfo.didConnectionMigrationSucceed()).isFalse();
        assertThat(trafficInfo.getTerminalState())
                .isEqualTo(CronetTrafficInfo.RequestTerminalState.ERROR);
        assertThat(trafficInfo.getNonfinalUserCallbackExceptionCount()).isEqualTo(0);
        assertThat(trafficInfo.getReadCount()).isEqualTo(0);
        assertThat(trafficInfo.getOnUploadReadCount()).isEqualTo(0);
        assertThat(trafficInfo.getIsBidiStream()).isFalse();
        assertThat(trafficInfo.getFinalUserCallbackThrew()).isFalse();

        assertThat(trafficInfo.getConnectionCloseSource()).isEqualTo(ConnectionCloseSource.UNKNOWN);
        assertThat(trafficInfo.getNetworkInternalErrorCode()).isEqualTo(-300);
        assertThat(trafficInfo.getFailureReason())
                .isEqualTo(CronetTrafficInfo.RequestFailureReason.NETWORK);
        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
    }

    @Test
    @SmallTest
    public void testNonfinalUserCallbackExceptionNative() throws Exception {
        var callback = new TestUrlRequestCallback();
        callback.setFailure(
                TestUrlRequestCallback.FailureType.THROW_SYNC,
                TestUrlRequestCallback.ResponseStep.ON_RESPONSE_STARTED);
        mTestRule
                .getTestFramework()
                .startEngine()
                .newUrlRequestBuilder(
                        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor())
                .build()
                .start();
        callback.blockForDone();
        mTestLogger.waitForLogCronetTrafficInfo();

        final CronetTrafficInfo trafficInfo = mTestLogger.getLastCronetTrafficInfo();
        assertThat(trafficInfo.getNonfinalUserCallbackExceptionCount()).isEqualTo(1);
        assertThat(trafficInfo.getFinalUserCallbackThrew()).isFalse();
        assertThat(trafficInfo.getConnectionCloseSource()).isEqualTo(ConnectionCloseSource.UNKNOWN);
        assertThat(trafficInfo.getNetworkInternalErrorCode()).isEqualTo(0);
        assertThat(trafficInfo.getFailureReason())
                .isEqualTo(CronetTrafficInfo.RequestFailureReason.OTHER);
    }

    @Test
    @SmallTest
    public void testFinalUserCallbackExceptionNative() throws Exception {
        var callback = new TestUrlRequestCallback();
        callback.setFailure(
                TestUrlRequestCallback.FailureType.THROW_SYNC,
                TestUrlRequestCallback.ResponseStep.ON_SUCCEEDED);
        mTestRule
                .getTestFramework()
                .startEngine()
                .newUrlRequestBuilder(
                        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor())
                .build()
                .start();
        callback.blockForDone();
        mTestLogger.waitForLogCronetTrafficInfo();

        final CronetTrafficInfo trafficInfo = mTestLogger.getLastCronetTrafficInfo();
        assertThat(trafficInfo.getNonfinalUserCallbackExceptionCount()).isEqualTo(0);
        assertThat(trafficInfo.getFinalUserCallbackThrew()).isTrue();
    }

    @Test
    @SmallTest
    public void testCanceledRequestNative() throws Exception {
        final String url = NativeTestServer.getEchoBodyURL();
        CronetEngine engine = mTestRule.getTestFramework().startEngine();

        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        callback.setAutoAdvance(false);
        UrlRequest.Builder requestBuilder =
                engine.newUrlRequestBuilder(url, callback, callback.getExecutor());
        UrlRequest request = requestBuilder.build();
        request.start();
        request.cancel();
        callback.blockForDone();
        assertThat(callback.mOnCanceledCalled).isTrue();
        assertThat(callback.mOnErrorCalled).isFalse();
        mTestLogger.waitForLogCronetTrafficInfo();

        final CronetTrafficInfo trafficInfo = mTestLogger.getLastCronetTrafficInfo();
        assertThat(trafficInfo.getRequestHeaderSizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getRequestBodySizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getResponseHeaderSizeInBytes()).isEqualTo(0);
        assertThat(trafficInfo.getResponseBodySizeInBytes()).isEqualTo(0);
        // When a request fails before hitting the server all these values won't be populated in
        // the actual code. Check that the logger sets them to some known defaults before
        // logging.
        assertThat(trafficInfo.getResponseStatusCode()).isEqualTo(0);
        assertThat(trafficInfo.getNegotiatedProtocol()).isEmpty();
        assertThat(trafficInfo.wasConnectionMigrationAttempted()).isFalse();
        assertThat(trafficInfo.didConnectionMigrationSucceed()).isFalse();
        assertThat(trafficInfo.getTerminalState())
                .isEqualTo(CronetTrafficInfo.RequestTerminalState.CANCELLED);
        assertThat(trafficInfo.getNonfinalUserCallbackExceptionCount()).isEqualTo(0);
        assertThat(trafficInfo.getReadCount()).isEqualTo(0);
        assertThat(trafficInfo.getOnUploadReadCount()).isEqualTo(0);
        assertThat(trafficInfo.getIsBidiStream()).isFalse();
        assertThat(trafficInfo.getFinalUserCallbackThrew()).isFalse();
        assertThat(trafficInfo.getConnectionCloseSource()).isEqualTo(ConnectionCloseSource.UNKNOWN);
        assertThat(trafficInfo.getNetworkInternalErrorCode()).isEqualTo(0);
        assertThat(trafficInfo.getFailureReason())
                .isEqualTo(CronetTrafficInfo.RequestFailureReason.UNKNOWN);
        assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
        assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
    }

    @Test
    @SmallTest
    public void testUploadNative() throws Exception {
        var callback = new TestUrlRequestCallback();
        var dataProvider =
                new TestUploadDataProvider(
                        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
        dataProvider.addRead("test".getBytes());
        mTestRule
                .getTestFramework()
                .startEngine()
                .newUrlRequestBuilder(
                        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor())
                .setUploadDataProvider(dataProvider, callback.getExecutor())
                .addHeader("Content-Type", "useless/string")
                .build()
                .start();
        callback.blockForDone();

        mTestLogger.waitForLogCronetTrafficInfo();
        assertThat(mTestLogger.getLastCronetTrafficInfo().getOnUploadReadCount()).isGreaterThan(0);
    }

    @Test
    @SmallTest
    public void testBidirectionalStream() throws Exception {
        assertThat(Http2TestServer.startHttp2TestServer(mTestRule.getTestFramework().getContext()))
                .isTrue();
        try {
            var callback = new TestBidirectionalStreamCallback();
            callback.addWriteData(
                    ("test long write data which has to be long so that the response "
                                    + "body size is non-zero; see b/328737465")
                            .getBytes());
            var stream =
                    mTestRule
                            .getTestFramework()
                            .startEngine()
                            .newBidirectionalStreamBuilder(
                                    Http2TestServer.getEchoStreamUrl(),
                                    callback,
                                    callback.getExecutor())
                            .addHeader("Content-Type", "test/contenttype")
                            .build();
            stream.start();
            callback.blockForDone();
            assertThat(stream.isDone()).isTrue();
            assertThat(callback.getResponseInfoWithChecks()).hasHttpStatusCodeThat().isEqualTo(200);
            mTestLogger.waitForLogCronetTrafficInfo();

            var trafficInfo = mTestLogger.getLastCronetTrafficInfo();
            assertThat(trafficInfo.getRequestHeaderSizeInBytes()).isNotEqualTo(0);
            assertThat(trafficInfo.getRequestBodySizeInBytes()).isNotEqualTo(0);
            assertThat(trafficInfo.getResponseHeaderSizeInBytes()).isNotEqualTo(0);
            assertThat(trafficInfo.getResponseBodySizeInBytes()).isNotEqualTo(0);
            assertThat(trafficInfo.getResponseStatusCode()).isEqualTo(200);
            assertThat(trafficInfo.getHeadersLatency()).isNotEqualTo(Duration.ofSeconds(0));
            assertThat(trafficInfo.getTotalLatency()).isNotEqualTo(Duration.ofSeconds(0));
            assertThat(trafficInfo.getNegotiatedProtocol()).isNotNull();
            assertThat(trafficInfo.wasConnectionMigrationAttempted()).isFalse();
            assertThat(trafficInfo.didConnectionMigrationSucceed()).isFalse();
            assertThat(trafficInfo.getTerminalState())
                    .isEqualTo(CronetTrafficInfo.RequestTerminalState.SUCCEEDED);
            assertThat(trafficInfo.getNonfinalUserCallbackExceptionCount()).isEqualTo(0);
            assertThat(trafficInfo.getReadCount()).isGreaterThan(0);
            assertThat(trafficInfo.getOnUploadReadCount()).isGreaterThan(0);
            assertThat(trafficInfo.getIsBidiStream()).isTrue();
            assertThat(trafficInfo.getFinalUserCallbackThrew()).isFalse();
            assertThat(trafficInfo.getConnectionCloseSource())
                    .isEqualTo(ConnectionCloseSource.UNKNOWN);
            assertThat(trafficInfo.getNetworkInternalErrorCode()).isEqualTo(0);
            assertThat(trafficInfo.getFailureReason())
                    .isEqualTo(CronetTrafficInfo.RequestFailureReason.UNKNOWN);
            assertThat(mTestLogger.callsToLogCronetEngineCreation()).isEqualTo(1);
            assertThat(mTestLogger.callsToLogCronetTrafficInfo()).isEqualTo(1);
        } finally {
            assertThat(Http2TestServer.shutdownHttp2TestServer()).isTrue();
        }
    }

    @Test
    @SmallTest
    public void testEmptyHeadersSizeNative() {
        Map<String, List<String>> headers = Collections.emptyMap();
        assertThat(CronetRequestCommon.estimateHeadersSizeInBytes(headers)).isEqualTo(0);
        headers = null;
        assertThat(CronetRequestCommon.estimateHeadersSizeInBytes(headers)).isEqualTo(0);

        ArrayList<Entry<String, String>> headersList = new ArrayList<>();
        assertThat(CronetRequestCommon.estimateHeadersSizeInBytes(headersList)).isEqualTo(0);
        headersList = null;
        assertThat(CronetRequestCommon.estimateHeadersSizeInBytes(headersList)).isEqualTo(0);
    }

    @Test
    @SmallTest
    public void testNonEmptyHeadersSizeNative() {
        Map<String, List<String>> headers = new HashMap<>();
        headers.put("header1", Arrays.asList("value1", "value2")); // 7 + 6 + 6 = 19
        headers.put("header2", null); // 19 + 7 = 26
        headers.put("header3", Collections.emptyList()); // 26 + 7 + 0 = 33
        headers.put(null, Arrays.asList("")); // 33 + 0 + 0 = 33

        assertThat(CronetRequestCommon.estimateHeadersSizeInBytes(headers)).isEqualTo(33);

        ArrayList<Map.Entry<String, String>> headersList = new ArrayList<>();
        headersList.add(new AbstractMap.SimpleImmutableEntry<>("header1", "value1")); // 7 + 6 = 13
        headersList.add(
                new AbstractMap.SimpleImmutableEntry<>("header1", "value2")); // 13 + 7 + 6 = 26
        headersList.add(new AbstractMap.SimpleImmutableEntry<>("header2", null)); // 26 + 7 + 0 = 33
        headersList.add(new AbstractMap.SimpleImmutableEntry<>(null, "")); // 33 + 0 + 0 = 33

        assertThat(CronetRequestCommon.estimateHeadersSizeInBytes(headersList)).isEqualTo(33);
    }
}
