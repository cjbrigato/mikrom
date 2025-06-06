// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.net.Uri;
import android.webkit.MimeTypeMap;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.io.File;
import java.io.FileFilter;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;

/**
 *  A mime type filter that supports filtering both mime types and file extensions.
 *  Note that this class is used specifically to implement
 *  the mime type filtering for web share target spec:
 *  https://wicg.github.io/web-share-target/level-2/#determining-if-a-file-is-accepted
 *  It is also used inside chrome/android/java/src/org/chromium/chrome/browser/photo_picker.
 */
@NullMarked
public class MimeTypeFilter implements FileFilter {
    private final HashSet<String> mExtensions = new HashSet<>();
    private final HashSet<String> mMimeTypes = new HashSet<>();
    private final HashSet<String> mMimeSupertypes = new HashSet<>();
    private final MimeTypeMap mMimeTypeMap;
    private boolean mAcceptAllMimeTypes;
    private final boolean mAcceptDirectory;

    /**
     * Contructs a MimeTypeFilter object.
     * @param mimeTypes A list of MIME types this filter accepts.
     *                  For example: images/gif, video/*.
     */
    public MimeTypeFilter(List<String> mimeTypes, boolean acceptDirectory) {
        for (String field : mimeTypes) {
            field = field.trim().toLowerCase(Locale.US);
            if (field.startsWith(".")) {
                mExtensions.add(field.substring(1));
            } else if (field.equals("*/*")) {
                mAcceptAllMimeTypes = true;
            } else if (field.endsWith("/*")) {
                mMimeSupertypes.add(field.substring(0, field.length() - 2));
            } else if (field.contains("/")) {
                mMimeTypes.add(field);
            }
        }

        mMimeTypeMap = MimeTypeMap.getSingleton();
        mAcceptDirectory = acceptDirectory;
    }

    /** Returns true if either the uri or the mimeType is accepted by the MimeTypeFilter */
    public boolean accept(@Nullable Uri uri, @Nullable String mimeType) {
        if (uri != null) {
            String fileExtension =
                    MimeTypeMap.getFileExtensionFromUrl(uri.toString()).toLowerCase(Locale.US);
            if (mExtensions.contains(fileExtension)) {
                return true;
            }
            if (mimeType == null) {
                mimeType = getMimeTypeFromExtension(fileExtension);
            }
        }

        if (mimeType != null) {
            if (mAcceptAllMimeTypes
                    || mMimeTypes.contains(mimeType)
                    || mMimeSupertypes.contains(getMimeSupertype(mimeType))) {
                return true;
            }
        }
        return false;
    }

    @Override
    public boolean accept(File file) {
        if (file.isDirectory()) {
            return mAcceptDirectory;
        }
        return accept(Uri.fromFile(file), null);
    }

    private @Nullable String getMimeTypeFromExtension(String ext) {
        String mimeType = mMimeTypeMap.getMimeTypeFromExtension(ext);
        return (mimeType != null) ? mimeType.toLowerCase(Locale.US) : null;
    }

    private static String getMimeSupertype(String mimeType) {
        return mimeType.split("/", 2)[0];
    }
}
