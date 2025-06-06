// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import static com.google.common.truth.Truth.assertWithMessage;

import android.net.Network;

import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;
import org.json.JSONException;
import org.json.JSONObject;

import org.chromium.net.impl.CronetEngineBuilderImpl;
import org.chromium.net.impl.CronetUrlRequest;
import org.chromium.net.impl.CronetUrlRequestContext;

/** Utilities for Cronet testing */
@JNINamespace("cronet")
public class CronetTestUtil {
    // QUIC test domain must match the certificate used
    // (quic-chain.pem and quic-leaf-cert.key), and the file served (
    // components/cronet/android/test/assets/test/quic_data/simple.txt).
    static final String QUIC_FAKE_HOST = "test.example.com";
    private static final String[] TEST_DOMAINS = {QUIC_FAKE_HOST};
    private static final String LOOPBACK_ADDRESS = "127.0.0.1";

    /**
     * Generates rules for customized DNS mapping for testing hostnames used by test servers,
     * namely:
     * <ul>
     * <li>{@link QuicTestServer#getServerHost}</li>
     * </ul>
     * Maps the test hostnames to 127.0.0.1.
     */
    public static JSONObject generateHostResolverRules() throws JSONException {
        return generateHostResolverRules(LOOPBACK_ADDRESS);
    }

    /**
     * Generates rules for customized DNS mapping for testing hostnames used by test servers,
     * namely:
     * <ul>
     * <li>{@link QuicTestServer#getServerHost}</li>
     * </ul>
     * @param destination host to map to
     */
    public static JSONObject generateHostResolverRules(String destination) throws JSONException {
        StringBuilder rules = new StringBuilder();
        for (String domain : TEST_DOMAINS) {
            rules.append("MAP " + domain + " " + destination + ",");
        }
        return new JSONObject().put("host_resolver_rules", rules);
    }

    /**
     * Returns the value of load flags in |urlRequest|.
     *
     * @param urlRequest is the UrlRequest object of interest.
     */
    public static int getLoadFlags(UrlRequest urlRequest) {
        return CronetTestUtilJni.get()
                .getLoadFlags(((CronetUrlRequest) urlRequest).getUrlRequestAdapterForTesting());
    }

    public static boolean doesURLRequestContextExistForTesting(
            CronetEngine engine, Network network) {
        CronetUrlRequestContext context = (CronetUrlRequestContext) engine;
        return CronetTestUtilJni.get()
                .uRLRequestContextExistsForTesting(
                        context.getUrlRequestContextAdapter(), network.getNetworkHandle());
    }

    public static void setMockCertVerifierForTesting(
            ExperimentalCronetEngine.Builder builder, long mockCertVerifier) {
        getCronetEngineBuilderImpl(builder).setMockCertVerifierForTesting(mockCertVerifier);
    }

    static CronetEngineBuilderImpl getCronetEngineBuilderImpl(
            ExperimentalCronetEngine.Builder builder) {
        return (CronetEngineBuilderImpl) builder.getBuilderDelegate();
    }

    /** Returns whether the device supports calling nativeGetTaggedBytes(). */
    public static boolean nativeCanGetTaggedBytes() {
        return CronetTestUtilJni.get().canGetTaggedBytes();
    }

    public static String[] nativeGetClientConnectionOptions(CronetEngine engine) {
        CronetUrlRequestContext context = (CronetUrlRequestContext) engine;
        return CronetTestUtilJni.get()
                .getClientConnectionOptions(context.getUrlRequestContextAdapter());
    }

    public static String[] nativeGetConnectionOptions(CronetEngine engine) {
        CronetUrlRequestContext context = (CronetUrlRequestContext) engine;
        return CronetTestUtilJni.get().getConnectionOptions(context.getUrlRequestContextAdapter());
    }

    /**
     * Query the system to find out how many bytes were received with tag {@code expectedTag} for
     * our UID.
     *
     * @param expectedTag the tag to query for.
     * @return the count of received bytes.
     */
    public static long nativeGetTaggedBytes(int expectedTag) {
        return CronetTestUtilJni.get().getTaggedBytes(expectedTag);
    }

    /** Helper method to assert that the request is negotiated over QUIC. */
    public static void assertIsQuic(UrlResponseInfo responseInfo) {
        String protocol = responseInfo.getNegotiatedProtocol();
        assertWithMessage("Expected the negotiatedProtocol to be QUIC but was " + protocol)
                .that(protocol.startsWith("http/2+quic") || protocol.startsWith("h3"))
                .isTrue();
    }

    @NativeMethods("cronet_tests")
    interface Natives {
        boolean canGetTaggedBytes();

        long getTaggedBytes(int expectedTag);

        int getLoadFlags(long urlRequestAdapter);

        void prepareNetworkThread(long contextAdapter);

        void cleanupNetworkThread(long contextAdapter);

        boolean uRLRequestContextExistsForTesting(long contextAdapter, long networkHandle);

        String[] getClientConnectionOptions(long contextAdapter);

        String[] getConnectionOptions(long contextAdapter);
    }
}
