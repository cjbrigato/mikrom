// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.http.X509TrustManagerExtensions;
import android.os.Build;
import android.security.KeyChain;
import android.util.Pair;

import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.build.annotations.NullUnmarked;
import org.chromium.build.annotations.Nullable;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.PublicKey;
import java.security.cert.Certificate;
import java.security.cert.CertificateEncodingException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateExpiredException;
import java.security.cert.CertificateFactory;
import java.security.cert.CertificateNotYetValidException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;
import javax.security.auth.x500.X500Principal;

/** Utility functions for interacting with Android's X.509 certificates. */
@JNINamespace("net")
@NullUnmarked
public class X509Util {
    private static final String TAG = "X509Util";

    private static final class TrustStorageListener extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            boolean shouldReloadTrustManager = false;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                if (KeyChain.ACTION_TRUST_STORE_CHANGED.equals(intent.getAction())) {
                    shouldReloadTrustManager = true;
                } else if (KeyChain.ACTION_KEYCHAIN_CHANGED.equals(intent.getAction())) {
                    X509UtilJni.get().notifyClientCertStoreChanged();
                } else if (KeyChain.ACTION_KEY_ACCESS_CHANGED.equals(intent.getAction())
                        && !intent.getBooleanExtra(KeyChain.EXTRA_KEY_ACCESSIBLE, false)) {
                    // We lost access to a client certificate key. Reload all client certificate
                    // state as we are not currently able to forget an individual identity.
                    X509UtilJni.get().notifyClientCertStoreChanged();
                }
            } else {
                @SuppressWarnings("deprecation")
                String action = KeyChain.ACTION_STORAGE_CHANGED;
                // Before Android O, KeyChain only emitted a coarse-grained intent. This fires much
                // more often than it should (https://crbug.com/381912), but there are no APIs to
                // distinguish the various cases.
                if (action.equals(intent.getAction())) {
                    shouldReloadTrustManager = true;
                    X509UtilJni.get().notifyClientCertStoreChanged();
                }
            }

            if (shouldReloadTrustManager) {
                try {
                    reloadDefaultTrustManager();
                } catch (CertificateException e) {
                    Log.e(TAG, "Unable to reload the default TrustManager", e);
                } catch (KeyStoreException e) {
                    Log.e(TAG, "Unable to reload the default TrustManager", e);
                } catch (NoSuchAlgorithmException e) {
                    Log.e(TAG, "Unable to reload the default TrustManager", e);
                }
            }
        }
    }

    private static List<X509Certificate> checkServerTrustedIgnoringRuntimeException(
            X509TrustManagerExtensions tm,
            X509Certificate[] chain,
            String authType,
            String host,
            byte @Nullable [] ocspResponse,
            byte @Nullable [] sctList)
            throws CertificateException {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA
                    && !(ocspResponse == null && sctList == null)) {
                return tm.checkServerTrusted(chain, ocspResponse, sctList, authType, host);
            }
            return tm.checkServerTrusted(chain, authType, host);
        } catch (RuntimeException e) {
            // https://crbug.com/937354: checkServerTrusted() can unexpectedly throw runtime
            // exceptions, most often within conscrypt while parsing certificates.
            Log.e(TAG, "checkServerTrusted() unexpectedly threw: %s", e);
            throw new CertificateException(e);
        }
    }

    private static @Nullable CertificateFactory sCertificateFactory;

    private static final String OID_TLS_SERVER_AUTH = "1.3.6.1.5.5.7.3.1";
    private static final String OID_ANY_EKU = "2.5.29.37.0";
    // Server-Gated Cryptography (necessary to support a few legacy issuers):
    //    Netscape:
    private static final String OID_SERVER_GATED_NETSCAPE = "2.16.840.1.113730.4.1";
    //    Microsoft:
    private static final String OID_SERVER_GATED_MICROSOFT = "1.3.6.1.4.1.311.10.3.3";

    /** Trust manager backed up by the read-only system certificate store. */
    private static @Nullable X509TrustManagerExtensions sDefaultTrustManager;

    /**
     * BroadcastReceiver that listens to change in the system keystore to invalidate certificate
     * caches.
     */
    private static @Nullable TrustStorageListener sTrustStorageListener;

    /**
     * Trust manager backed up by a custom certificate store. We need such manager to plant test
     * root CA to the trust store in testing.
     */
    private static @Nullable X509TrustManagerExtensions sTestTrustManager;

    private static @Nullable KeyStore sTestKeyStore;

    /**
     * The system key store. This is used to determine whether a trust anchor is a system trust
     * anchor or user-installed.
     */
    private static @Nullable KeyStore sSystemKeyStore;

    /**
     * The directory where system certificates are stored. This is used to determine whether a
     * trust anchor is a system trust anchor or user-installed. The KeyStore API alone is not
     * sufficient to efficiently query whether a given X500Principal, PublicKey pair is a trust
     * anchor.
     */
    private static @Nullable File sSystemCertificateDirectory;

    /**
     * An in-memory cache of which trust anchors are system trust roots. This avoids reading and
     * decoding the root from disk on every verification. Mirrors a similar in-memory cache in
     * Conscrypt's X509TrustManager implementation.
     */
    private static @Nullable Set<Pair<X500Principal, PublicKey>> sSystemTrustAnchorCache;

    /**
     * True if the system key store has been loaded. If the "AndroidCAStore" KeyStore instance
     * was not found, sSystemKeyStore may be null while sLoadedSystemKeyStore is true.
     */
    private static boolean sLoadedSystemKeyStore;

    /** A root that will be installed as a user-trusted root for testing purposes. */
    private static @Nullable X509Certificate sTestRoot;

    /** Lock object used to synchronize all calls that modify or depend on the trust managers. */
    private static final Object sLock = new Object();

    /** Ensures that the trust managers and certificate factory are initialized. */
    private static void ensureInitialized()
            throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
        synchronized (sLock) {
            ensureInitializedLocked();
        }
    }

    /**
     * Ensures that the trust managers and certificate factory are initialized. Must be called with
     * |sLock| held. Does not initialize test infrastructure.
     */
    // FindBugs' static field initialization warnings do not handle methods that are expected to be
    // called locked.
    private static void ensureInitializedLocked()
            throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
        assert Thread.holdsLock(sLock);

        if (sCertificateFactory == null) {
            sCertificateFactory = CertificateFactory.getInstance("X.509");
        }
        if (sDefaultTrustManager == null) {
            sDefaultTrustManager = X509Util.createTrustManager(null);
        }
        if (!sLoadedSystemKeyStore) {
            try {
                sSystemKeyStore = KeyStore.getInstance("AndroidCAStore");
                try {
                    sSystemKeyStore.load(null);
                } catch (IOException e) {
                    // No IO operation is attempted.
                }
                sSystemCertificateDirectory =
                        new File(System.getenv("ANDROID_ROOT") + "/etc/security/cacerts");
            } catch (KeyStoreException e) {
                // Could not load AndroidCAStore. Continue anyway; isKnownRoot will always
                // return false.
            }
            sLoadedSystemKeyStore = true;
        }
        if (sSystemTrustAnchorCache == null) {
            sSystemTrustAnchorCache = new HashSet<Pair<X500Principal, PublicKey>>();
        }
        if (sTrustStorageListener == null) {
            sTrustStorageListener = new TrustStorageListener();
            IntentFilter filter = new IntentFilter();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                filter.addAction(KeyChain.ACTION_KEYCHAIN_CHANGED);
                filter.addAction(KeyChain.ACTION_KEY_ACCESS_CHANGED);
                filter.addAction(KeyChain.ACTION_TRUST_STORE_CHANGED);
            } else {
                @SuppressWarnings("deprecation")
                String action = KeyChain.ACTION_STORAGE_CHANGED;
                filter.addAction(action);
            }
            ContextUtils.registerProtectedBroadcastReceiver(
                    ContextUtils.getApplicationContext(), sTrustStorageListener, filter);
        }
    }

    /**
     * Ensures that test  trust managers and certificate factory are initialized. Must be called
     * with |sLock| held.
     */
    private static void ensureTestInitializedLocked()
            throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
        assert Thread.holdsLock(sLock);
        if (sTestKeyStore == null) {
            sTestKeyStore = KeyStore.getInstance(KeyStore.getDefaultType());
            try {
                sTestKeyStore.load(null);
            } catch (IOException e) {
                // No IO operation is attempted.
            }
        }
        if (sTestTrustManager == null) {
            sTestTrustManager = X509Util.createTrustManager(sTestKeyStore);
        }
    }

    /**
     * Creates a X509TrustManagerExtensions backed up by the given key
     * store. When null is passed as a key store, system default trust store is
     * used. Returns null if no created TrustManager was suitable.
     * @throws KeyStoreException, NoSuchAlgorithmException on error initializing the TrustManager.
     */
    private static @Nullable X509TrustManagerExtensions createTrustManager(
            @Nullable KeyStore keyStore) throws KeyStoreException, NoSuchAlgorithmException {
        String algorithm = TrustManagerFactory.getDefaultAlgorithm();
        TrustManagerFactory tmf = TrustManagerFactory.getInstance(algorithm);
        tmf.init(keyStore);

        TrustManager[] trustManagers = null;
        try {
            trustManagers = tmf.getTrustManagers();
        } catch (RuntimeException e) {
            // https://crbug.com/937354: getTrustManagers() can unexpectedly throw runtime
            // exceptions, most often while processing the network security config XML file.
            Log.e(TAG, "TrustManagerFactory.getTrustManagers() unexpectedly threw: %s", e);
            throw new KeyStoreException(e);
        }

        for (TrustManager tm : trustManagers) {
            if (tm instanceof X509TrustManager) {
                try {
                    return new X509TrustManagerExtensions((X509TrustManager) tm);
                } catch (IllegalArgumentException e) {
                    String className = tm.getClass().getName();
                    Log.e(TAG, "Error creating trust manager (" + className + "): " + e);
                }
            }
        }
        Log.e(TAG, "Could not find suitable trust manager");
        return null;
    }

    /** After each modification of test key store, trust manager has to be generated again. */
    private static void reloadTestTrustManager()
            throws KeyStoreException, NoSuchAlgorithmException, CertificateException {
        assert Thread.holdsLock(sLock);
        ensureTestInitializedLocked();

        sTestTrustManager = X509Util.createTrustManager(sTestKeyStore);
    }

    /**
     * After each modification by the system of the key store, trust manager has to be regenerated.
     */
    private static void reloadDefaultTrustManager()
            throws KeyStoreException, NoSuchAlgorithmException, CertificateException {
        synchronized (sLock) {
            sDefaultTrustManager = null;
            sSystemTrustAnchorCache = null;
            ensureInitializedLocked();
        }
        X509UtilJni.get().notifyTrustStoreChanged();
    }

    /** Convert a DER encoded certificate to an X509Certificate. */
    public static X509Certificate createCertificateFromBytes(byte[] derBytes)
            throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
        ensureInitialized();
        return (X509Certificate)
                sCertificateFactory.generateCertificate(new ByteArrayInputStream(derBytes));
    }

    /** Add a test root certificate for use by the Android Platform verifier. */
    public static void addTestRootCertificate(byte[] rootCertBytes)
            throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
        X509Certificate rootCert = createCertificateFromBytes(rootCertBytes);
        synchronized (sLock) {
            ensureTestInitializedLocked();
            // Add the cert to be used by the Android Platform Verifier.
            sTestKeyStore.setCertificateEntry(
                    "root_cert_" + Integer.toString(sTestKeyStore.size()), rootCert);
            reloadTestTrustManager();
        }
    }

    /** Clear test root certificates in use by the Android Platform verifier. */
    public static void clearTestRootCertificates()
            throws NoSuchAlgorithmException, CertificateException, KeyStoreException {
        synchronized (sLock) {
            ensureTestInitializedLocked();
            try {
                sTestKeyStore.load(null);
                reloadTestTrustManager();
            } catch (IOException e) {
                // No IO operation is attempted.
            }
        }
    }

    /** Set a test root certificate for use by CertVerifierBuiltin. */
    public static void setTestRootCertificateForBuiltin(byte[] rootCertBytes)
            throws NoSuchAlgorithmException, CertificateException, KeyStoreException {
        X509Certificate rootCert = createCertificateFromBytes(rootCertBytes);
        synchronized (sLock) {
            // Add the cert to be used by CertVerifierBuiltin.
            //
            // This saves the root so it is returned from getUserAddedRoots, for TrustStoreAndroid.
            // This is done for the Java EmbeddedTestServer implementation and must run before
            // native code is loaded, when getUserAddedRoots is first run.
            sTestRoot = rootCert;
        }
    }

    private static final char[] HEX_DIGITS = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };

    private static String hashPrincipal(X500Principal principal) throws NoSuchAlgorithmException {
        // Android hashes a principal as the first four bytes of its MD5 digest, encoded in
        // lowercase hex and reversed. Verified in 4.2, 4.3, and 4.4.
        byte[] digest = MessageDigest.getInstance("MD5").digest(principal.getEncoded());
        char[] hexChars = new char[8];
        for (int i = 0; i < 4; i++) {
            hexChars[2 * i] = HEX_DIGITS[(digest[3 - i] >> 4) & 0xf];
            hexChars[2 * i + 1] = HEX_DIGITS[digest[3 - i] & 0xf];
        }
        return new String(hexChars);
    }

    private static boolean isKnownRoot(X509Certificate root)
            throws NoSuchAlgorithmException, KeyStoreException {
        assert Thread.holdsLock(sLock);

        // Could not find the system key store. Conservatively report false.
        if (sSystemKeyStore == null) return false;

        // Check the in-memory cache first; avoid decoding the anchor from disk
        // if it has been seen before.
        Pair<X500Principal, PublicKey> key =
                new Pair<X500Principal, PublicKey>(
                        root.getSubjectX500Principal(), root.getPublicKey());

        if (sSystemTrustAnchorCache.contains(key)) return true;

        // Note: It is not sufficient to call sSystemKeyStore.getCertificiateAlias. If the server
        // supplies a copy of a trust anchor, X509TrustManagerExtensions returns the server's
        // version rather than the system one. getCertificiateAlias will then fail to find an anchor
        // name. This is fixed upstream in https://android-review.googlesource.com/#/c/91605/
        //
        // TODO(davidben): When the change trickles into an Android release, query sSystemKeyStore
        // directly.

        // System trust anchors are stored under a hash of the principal. In case of collisions,
        // a number is appended.
        String hash = hashPrincipal(root.getSubjectX500Principal());
        for (int i = 0; true; i++) {
            String alias = hash + '.' + i;
            if (!new File(sSystemCertificateDirectory, alias).exists()) break;

            Certificate anchor = sSystemKeyStore.getCertificate("system:" + alias);
            // It is possible for this to return null if the user deleted a trust anchor. In
            // that case, the certificate remains in the system directory but is also added to
            // another file. Continue iterating as there may be further collisions after the
            // deleted anchor.
            if (anchor == null) continue;

            if (!(anchor instanceof X509Certificate)) {
                // This should never happen.
                String className = anchor.getClass().getName();
                Log.e(TAG, "Anchor " + alias + " not an X509Certificate: " + className);
                continue;
            }

            // If the subject and public key match, this is a system root.
            X509Certificate anchorX509 = (X509Certificate) anchor;
            if (root.getSubjectX500Principal().equals(anchorX509.getSubjectX500Principal())
                    && root.getPublicKey().equals(anchorX509.getPublicKey())) {
                sSystemTrustAnchorCache.add(key);
                return true;
            }
        }

        return false;
    }

    /**
     * If an EKU extension is present in the end-entity certificate, it MUST contain either the
     * anyEKU or serverAuth or netscapeSGC or Microsoft SGC EKUs.
     *
     * @return true if there is no EKU extension or if any of the EKU extensions is one of the valid
     * OIDs for web server certificates.
     *
     * TODO(palmer): This can be removed after the equivalent change is made to the Android default
     * TrustManager and that change is shipped to a large majority of Android users.
     */
    static boolean verifyKeyUsage(X509Certificate certificate) throws CertificateException {
        List<String> ekuOids;
        try {
            ekuOids = certificate.getExtendedKeyUsage();
        } catch (NullPointerException e) {
            // getExtendedKeyUsage() can crash due to an Android platform bug. This probably
            // happens when the EKU extension data is malformed so return false here.
            // See http://crbug.com/233610
            return false;
        }
        if (ekuOids == null) return true;

        for (String ekuOid : ekuOids) {
            if (ekuOid.equals(OID_TLS_SERVER_AUTH)
                    || ekuOid.equals(OID_ANY_EKU)
                    || ekuOid.equals(OID_SERVER_GATED_NETSCAPE)
                    || ekuOid.equals(OID_SERVER_GATED_MICROSOFT)) {
                return true;
            }
        }

        return false;
    }

    /**
     * Get the list of user-added roots.
     *
     * @return DER-encoded list of user-added roots.
     */
    public static byte[][] getUserAddedRoots() {
        List<byte[]> userRootBytes = new ArrayList<byte[]>();
        synchronized (sLock) {
            try {
                ensureInitialized();
            } catch (NoSuchAlgorithmException | KeyStoreException | CertificateException e) {
                return new byte[0][];
            }

            if (sSystemKeyStore == null) {
                return new byte[0][];
            }

            try {
                for (Enumeration<String> aliases = sSystemKeyStore.aliases();
                        aliases.hasMoreElements(); ) {
                    String alias = aliases.nextElement();
                    // We check if its a user added root by looking at the alias; user roots should
                    // start with 'user:'. Another way of checking this would be to fetch the
                    // certificate and call X509TrustManagerExtensions.isUserAddedCertificate(), but
                    // that is imperfect as well because Keystore and X509TrustManagerExtensions
                    // are actually implemented by two separate systems, and mixing them probably
                    // works but might not in all cases.
                    //
                    // Also, to call X509TrustManagerExtensions.isUserAddedCertificate() we'd need
                    // to call Keystore.getCertificate on all of the roots, even the system ones.
                    //
                    // Since there's no perfect way of doing this we go with the simpler and more
                    // performant one.
                    if (alias.startsWith("user:")) {
                        try {
                            Certificate anchor = sSystemKeyStore.getCertificate(alias);
                            if (!(anchor instanceof X509Certificate)) {
                                Log.w(TAG, "alias: " + alias + " is not a X509 Cert, skipping");
                                continue;
                            }
                            X509Certificate anchorX509 = (X509Certificate) anchor;
                            userRootBytes.add(anchorX509.getEncoded());
                        } catch (KeyStoreException e) {
                            Log.e(TAG, "Error reading cert with alias %s, error: %s", alias, e);
                        } catch (CertificateEncodingException e) {
                            Log.e(TAG, "Error encoding cert with alias %s, error: %s", alias, e);
                        }
                    }
                }
            } catch (KeyStoreException e) {
                Log.e(TAG, "Error reading cert aliases: %s", e);
                return new byte[0][];
            }

            if (sTestRoot != null) {
                try {
                    userRootBytes.add(sTestRoot.getEncoded());
                } catch (CertificateEncodingException e) {
                    Log.e(TAG, "Error encoding test root cert, error %s", e);
                }
            }
        }

        return userRootBytes.toArray(new byte[0][]);
    }

    public static AndroidCertVerifyResult verifyServerCertificates(
            byte[][] certChain,
            String authType,
            String host,
            byte @Nullable [] ocspResponse,
            byte @Nullable [] sctList)
            throws KeyStoreException, NoSuchAlgorithmException {
        if (certChain == null || certChain.length == 0 || certChain[0] == null) {
            throw new IllegalArgumentException(
                    "Expected non-null and non-empty certificate "
                            + "chain passed as |certChain|. |certChain|="
                            + Arrays.deepToString(certChain));
        }

        try {
            ensureInitialized();
        } catch (CertificateException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
        }

        List<X509Certificate> serverCertificatesList = new ArrayList<X509Certificate>();
        try {
            serverCertificatesList.add(createCertificateFromBytes(certChain[0]));
        } catch (CertificateException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.UNABLE_TO_PARSE);
        }
        for (int i = 1; i < certChain.length; ++i) {
            try {
                serverCertificatesList.add(createCertificateFromBytes(certChain[i]));
            } catch (CertificateException e) {
                Log.w(TAG, "intermediate " + i + " failed parsing");
            }
        }
        X509Certificate[] serverCertificates =
                serverCertificatesList.toArray(new X509Certificate[serverCertificatesList.size()]);

        // Expired and not yet valid certificates would be rejected by the trust managers, but the
        // trust managers report all certificate errors using the general CertificateException. In
        // order to get more granular error information, cert validity time range is being checked
        // separately.
        try {
            serverCertificates[0].checkValidity();
            if (!verifyKeyUsage(serverCertificates[0])) {
                return new AndroidCertVerifyResult(CertVerifyStatusAndroid.INCORRECT_KEY_USAGE);
            }
        } catch (CertificateExpiredException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.EXPIRED);
        } catch (CertificateNotYetValidException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.NOT_YET_VALID);
        } catch (CertificateException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
        }

        synchronized (sLock) {
            // If no trust manager was found, fail without crashing on the null pointer.
            if (sDefaultTrustManager == null) {
                return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
            }

            List<X509Certificate> verifiedChain = null;
            try {
                verifiedChain =
                        checkServerTrustedIgnoringRuntimeException(
                                sDefaultTrustManager,
                                serverCertificates,
                                authType,
                                host,
                                ocspResponse,
                                sctList);
            } catch (CertificateException eDefaultManager) {
                if (sTestTrustManager != null) {
                    try {
                        verifiedChain =
                                checkServerTrustedIgnoringRuntimeException(
                                        sTestTrustManager,
                                        serverCertificates,
                                        authType,
                                        host,
                                        ocspResponse,
                                        sctList);
                    } catch (CertificateException eTestManager) {
                        // See following if block.
                    }
                }

                if (verifiedChain == null) {
                    // Neither of the trust managers confirms the validity of the certificate chain,
                    // log the error message returned by the system trust manager.
                    Log.i(
                            TAG,
                            "Failed to validate the certificate chain, error: "
                                    + eDefaultManager.getMessage());
                    return new AndroidCertVerifyResult(CertVerifyStatusAndroid.NO_TRUSTED_ROOT);
                }
            }
            boolean isIssuedByKnownRoot = false;
            if (verifiedChain.size() > 0) {
                X509Certificate root = verifiedChain.get(verifiedChain.size() - 1);
                isIssuedByKnownRoot = isKnownRoot(root);
            }
            return new AndroidCertVerifyResult(
                    CertVerifyStatusAndroid.OK, isIssuedByKnownRoot, verifiedChain);
        }
    }

    @NativeMethods
    interface Natives {
        /**
         * Notify the native net::CertDatabase instance that the system database has been updated.
         */
        void notifyTrustStoreChanged();

        void notifyClientCertStoreChanged();
    }
}
