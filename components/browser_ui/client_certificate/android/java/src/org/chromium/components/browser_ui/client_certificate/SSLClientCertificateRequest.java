// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.client_certificate;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface.OnClickListener;
import android.security.KeyChain;
import android.security.KeyChainAliasCallback;
import android.security.KeyChainException;

import androidx.annotation.VisibleForTesting;
import androidx.appcompat.app.AlertDialog;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.task.AsyncTask;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.base.WindowAndroid;

import java.security.Principal;
import java.security.PrivateKey;
import java.security.cert.CertificateEncodingException;
import java.security.cert.X509Certificate;

import javax.security.auth.x500.X500Principal;

/**
 * Handles selection of client certificate on the Java side. This class is responsible for selection
 * of the client certificate to be used for authentication and retrieval of the private key and full
 * certificate chain.
 *
 * <p>The entry point is selectClientCertificate() and it will be called on the UI thread. Then the
 * class will construct and run an appropriate CertAsyncTask, that will run in background, and
 * finally pass the results back to the UI thread, which will return to the native code.
 */
@JNINamespace("browser_ui")
@NullMarked
public class SSLClientCertificateRequest {
    static final String TAG = "SSLClientCertRequest";

    /**
     * Implementation for anynchronous task of handling the certificate request. This
     * AsyncTask retrieves the authentication material from the system key store.
     * The key store is accessed in background, as the APIs being exercised
     * may be blocking. The results are posted back to native on the UI thread.
     */
    private static class CertAsyncTaskKeyChain extends AsyncTask<Void> {
        // These fields will store the results computed in doInBackground so that they can be posted
        // back in onPostExecute.
        private byte @Nullable [][] mEncodedChain;
        @Nullable private PrivateKey mPrivateKey;

        // Pointer to the native certificate request needed to return the results.
        private final long mNativePtr;

        final Context mContext;
        final String mAlias;

        CertAsyncTaskKeyChain(Context context, long nativePtr, String alias) {
            mNativePtr = nativePtr;
            mContext = context;
            assert alias != null;
            mAlias = alias;
        }

        @Override
        protected Void doInBackground() {
            String alias = getAlias();
            if (alias == null) return null;

            PrivateKey key = getPrivateKey(alias);
            X509Certificate[] chain = getCertificateChain(alias);

            if (key == null || chain == null || chain.length == 0) {
                Log.w(TAG, "Empty client certificate chain?");
                return null;
            }

            // Encode the certificate chain.
            byte[][] encodedChain = new byte[chain.length][];
            try {
                for (int i = 0; i < chain.length; ++i) {
                    encodedChain[i] = chain[i].getEncoded();
                }
            } catch (CertificateEncodingException e) {
                Log.w(TAG, "Could not retrieve encoded certificate chain: " + e);
                return null;
            }

            mEncodedChain = encodedChain;
            mPrivateKey = key;
            return null;
        }

        @Override
        protected void onPostExecute(Void result) {
            ThreadUtils.assertOnUiThread();
            SSLClientCertificateRequestJni.get()
                    .onSystemRequestCompletion(mNativePtr, mEncodedChain, mPrivateKey);
        }

        private String getAlias() {
            return mAlias;
        }

        private @Nullable PrivateKey getPrivateKey(String alias) {
            try {
                return KeyChain.getPrivateKey(mContext, alias);
            } catch (KeyChainException e) {
                Log.w(TAG, "KeyChainException when looking for '" + alias + "' certificate");
                return null;
            } catch (InterruptedException e) {
                Log.w(TAG, "InterruptedException when looking for '" + alias + "'certificate");
                return null;
            }
        }

        private X509Certificate @Nullable [] getCertificateChain(String alias) {
            try {
                return KeyChain.getCertificateChain(mContext, alias);
            } catch (KeyChainException e) {
                Log.w(TAG, "KeyChainException when looking for '" + alias + "' certificate");
                return null;
            } catch (InterruptedException e) {
                Log.w(TAG, "InterruptedException when looking for '" + alias + "'certificate");
                return null;
            }
        }
    }

    /**
     * The system KeyChain API will call us back on the alias() method, passing the alias of the
     * certificate selected by the user.
     */
    private static class KeyChainCertSelectionCallback implements KeyChainAliasCallback {
        private final long mNativePtr;
        private final Context mContext;
        private boolean mAliasCalled;

        KeyChainCertSelectionCallback(Context context, long nativePtr) {
            mContext = context;
            mNativePtr = nativePtr;
        }

        @Override
        public void alias(final @Nullable String alias) {
            if (mAliasCalled) {
                Log.w(TAG, "KeyChainCertSelectionCallback called more than once ('" + alias + "')");
                return;
            }
            mAliasCalled = true;

            // This is called by KeyChainActivity in a background thread. Post task to
            // handle the certificate selection on the UI thread.
            PostTask.runOrPostTask(
                    TaskTraits.UI_DEFAULT,
                    () -> {
                        if (alias == null) {
                            // No certificate was selected.
                            PostTask.runOrPostTask(
                                    TaskTraits.UI_DEFAULT,
                                    () ->
                                            SSLClientCertificateRequestJni.get()
                                                    .onSystemRequestCompletion(
                                                            mNativePtr, null, null));
                        } else {
                            new CertAsyncTaskKeyChain(mContext, mNativePtr, alias)
                                    .executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR);
                        }
                    });
        }
    }

    /** Wrapper class for the static KeyChain#choosePrivateKeyAlias method to facilitate testing. */
    @VisibleForTesting
    static class KeyChainCertSelectionWrapper {
        private final Activity mActivity;
        private final KeyChainAliasCallback mCallback;
        private final String[] mKeyTypes;
        private final Principal @Nullable [] mPrincipalsForCallback;
        private final String mHostName;
        private final int mPort;
        private final @Nullable String mAlias;

        public KeyChainCertSelectionWrapper(
                Activity activity,
                KeyChainAliasCallback callback,
                String[] keyTypes,
                Principal @Nullable [] principalsForCallback,
                String hostName,
                int port,
                @Nullable String alias) {
            mActivity = activity;
            mCallback = callback;
            mKeyTypes = keyTypes;
            mPrincipalsForCallback = principalsForCallback;
            mHostName = hostName;
            mPort = port;
            mAlias = alias;
        }

        /**
         * Calls KeyChain#choosePrivateKeyAlias with the provided arguments.
         *
         */
        // WrongConstant: @KeyProperties.KeyAlgorithmEnum for mKeyTypes is hidden with @hide.
        @SuppressWarnings("WrongConstant")
        public void choosePrivateKeyAlias() throws ActivityNotFoundException {
            KeyChain.choosePrivateKeyAlias(
                    mActivity,
                    mCallback,
                    mKeyTypes,
                    mPrincipalsForCallback,
                    mHostName,
                    mPort,
                    mAlias);
        }
    }

    /**
     * Dialog that explains to the user that client certificates aren't supported on their operating
     * system. Separated out into its own class to allow Robolectric unit testing of
     * maybeShowCertSelection without depending on Chrome resources.
     */
    @VisibleForTesting
    static class CertSelectionFailureDialog {
        private final Context mContext;

        public CertSelectionFailureDialog(Context context) {
            mContext = context;
        }

        /** Builds and shows the dialog. */
        public void show() {
            final AlertDialog.Builder builder =
                    new AlertDialog.Builder(mContext, R.style.ThemeOverlay_BrowserUI_AlertDialog);
            builder.setTitle(R.string.client_cert_unsupported_title)
                    .setMessage(R.string.client_cert_unsupported_message)
                    .setNegativeButton(
                            R.string.close,
                            (OnClickListener)
                                    (dialog, which) -> {
                                        // Do nothing
                                    });
            builder.show();
        }
    }

    /**
     * Create a new asynchronous request to select a client certificate.
     *
     * @param nativePtr         The native object responsible for this request.
     * @param window            A WindowAndroid instance.
     * @param keyTypes          The list of supported key exchange types.
     * @param encodedPrincipals The list of CA DistinguishedNames.
     * @param hostName          The server host name is available (empty otherwise).
     * @param port              The server port if available (0 otherwise).
     * @return                  true on success.
     * Note that nativeOnSystemRequestComplete will be called iff this method returns true.
     */
    @CalledByNative
    private static boolean selectClientCertificate(
            final long nativePtr,
            final WindowAndroid window,
            final String[] keyTypes,
            byte[][] encodedPrincipals,
            final String hostName,
            final int port) {
        ThreadUtils.assertOnUiThread();

        // Use the context for the failure dialog in case the activity doesn't have the correct
        // resources.
        final Context context = window.getContext().get();
        final Activity activity = ContextUtils.activityFromContext(context);
        if (activity == null) {
            Log.w(TAG, "Certificate request on GC'd activity.");
            return false;
        }

        // Build the list of principals from encoded versions.
        Principal[] principals = null;
        if (encodedPrincipals.length > 0) {
            principals = new X500Principal[encodedPrincipals.length];
            try {
                for (int n = 0; n < encodedPrincipals.length; n++) {
                    principals[n] = new X500Principal(encodedPrincipals[n]);
                }
            } catch (Exception e) {
                Log.w(TAG, "Exception while decoding issuers list: " + e);
                return false;
            }
        }

        KeyChainCertSelectionCallback callback =
                new KeyChainCertSelectionCallback(activity.getApplicationContext(), nativePtr);
        KeyChainCertSelectionWrapper keyChain =
                new KeyChainCertSelectionWrapper(
                        activity, callback, keyTypes, principals, hostName, port, null);
        assumeNonNull(context);
        maybeShowCertSelection(keyChain, callback, new CertSelectionFailureDialog(context));

        // We've taken ownership of the native ssl request object.
        return true;
    }

    /**
     * Attempt to show the certificate selection dialog and shows the provided
     * CertSelectionFailureDialog if the platform's cert selection activity can't be found.
     */
    @VisibleForTesting
    static void maybeShowCertSelection(
            KeyChainCertSelectionWrapper keyChain,
            KeyChainAliasCallback callback,
            CertSelectionFailureDialog failureDialog) {
        try {
            keyChain.choosePrivateKeyAlias();
        } catch (ActivityNotFoundException e) {
            // This exception can be hit when a platform is missing the activity to select
            // a client certificate. It gets handled here to avoid a crash.
            // Complete the callback without selecting a certificate.
            callback.alias(null);
            // Show a dialog letting the user know that the system does not support
            // client certificate selection.
            failureDialog.show();
        }
    }

    public static void notifyClientCertificatesChangedOnIOThread() {
        Log.d(TAG, "ClientCertificatesChanged!");
        SSLClientCertificateRequestJni.get().notifyClientCertificatesChangedOnIOThread();
    }

    @NativeMethods
    interface Natives {
        void notifyClientCertificatesChangedOnIOThread();

        // Called to pass request results to native side.
        void onSystemRequestCompletion(
                long requestPtr, byte @Nullable [][] certChain, @Nullable PrivateKey privateKey);
    }
}
