// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.services.media_session;

import static org.chromium.build.NullUtil.assumeNonNull;

import android.text.TextUtils;

import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

/**
 * The MediaMetadata class carries information related to a media session. It is
 * the Java counterpart of media_session::MediaMetadata.
 */
@JNINamespace("media_session")
@NullMarked
public final class MediaMetadata {
    private @Nullable String mTitle;

    private String mArtist;

    private String mAlbum;

    /** Returns the title associated with the media session. */
    public @Nullable String getTitle() {
        return mTitle;
    }

    /** Returns the artist name associated with the media session. */
    public String getArtist() {
        return mArtist;
    }

    /** Returns the album name associated with the media session. */
    public String getAlbum() {
        return mAlbum;
    }

    /**
     * Sets the title associated with the media session.
     * @param title The title to use for the media session.
     */
    public void setTitle(@Nullable String title) {
        mTitle = title;
    }

    /**
     * Sets the arstist name associated with the media session.
     * @param arstist The artist name to use for the media session.
     */
    public void setArtist(String artist) {
        mArtist = artist;
    }

    /**
     * Sets the album name associated with the media session.
     * @param album The album name to use for the media session.
     */
    public void setAlbum(String album) {
        mAlbum = album;
    }

    /**
     * Creates a new MediaMetadata from the C++ code. This is exactly like the
     * constructor below apart that it can be called by native code.
     */
    @CalledByNative
    private static MediaMetadata create(@Nullable String title, String artist, String album) {
        return new MediaMetadata(title, artist, album);
    }

    /** Creates a new MediaMetadata. */
    public MediaMetadata(@Nullable String title, String artist, String album) {
        mTitle = title;
        mArtist = artist;
        mAlbum = album;
    }

    /** Comparing MediaMetadata is expensive and should be used sparingly */
    @Override
    public boolean equals(@Nullable Object obj) {
        if (obj == this) return true;
        if (!(obj instanceof MediaMetadata)) return false;

        MediaMetadata other = (MediaMetadata) obj;
        return TextUtils.equals(mTitle, other.mTitle)
                && TextUtils.equals(mArtist, other.mArtist)
                && TextUtils.equals(mAlbum, other.mAlbum);
    }

    /**
     * @return The hash code of this {@link MediaMetadata}. The method uses the same algorithm in
     * {@link java.util.List} for combinine hash values.
     */
    @Override
    public int hashCode() {
        assumeNonNull(mTitle);
        int result = mTitle.hashCode();
        result = 31 * result + mArtist.hashCode();
        result = 31 * result + mAlbum.hashCode();
        return result;
    }
}
