// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarkswidget;

import android.appwidget.AppWidgetManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.net.Uri;
import android.text.TextUtils;
import android.view.ContextThemeWrapper;
import android.view.View;
import android.widget.RemoteViews;
import android.widget.RemoteViewsService;

import androidx.annotation.BinderThread;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;

import com.google.android.apps.chrome.appwidget.bookmarks.BookmarkThumbnailWidgetProvider;

import org.chromium.base.ContextUtils;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.bookmarks.BookmarkModelObserver;
import org.chromium.chrome.browser.init.ChromeBrowserInitializer;
import org.chromium.chrome.browser.night_mode.SystemNightModeMonitor;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.ui.favicon.FaviconUtils;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.bookmarks.BookmarkItem;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.components.browser_ui.widget.RoundedIconGenerator;
import org.chromium.components.favicon.IconType;
import org.chromium.components.favicon.LargeIconBridge;
import org.chromium.components.favicon.LargeIconBridge.LargeIconCallback;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Service to support the bookmarks widget.
 *
 * This provides the list of bookmarks to show in the widget via a RemoteViewsFactory (the
 * RemoteViews equivalent of an Adapter), and updates the widget when the bookmark model changes.
 *
 * Threading note: Be careful! Android calls some methods in this class on the UI thread and others
 * on (multiple) binder threads. Additionally, all interaction with the BookmarkModel must happen on
 * the UI thread. To keep the situation clear, every non-static method is annotated with either
 * {@link UiThread} or {@link BinderThread}.
 */
public class BookmarkWidgetServiceImpl extends BookmarkWidgetService.Impl {
    private static final String TAG = "BookmarkWidget";
    private static final String ACTION_CHANGE_FOLDER_SUFFIX = ".CHANGE_FOLDER";
    private static final String PREF_CURRENT_FOLDER = "bookmarkswidget.current_folder";
    private static final String EXTRA_FOLDER_ID = "folderId";

    @UiThread
    @Override
    public RemoteViewsService.RemoteViewsFactory onGetViewFactory(Intent intent) {
        int widgetId = IntentUtils.safeGetIntExtra(intent, AppWidgetManager.EXTRA_APPWIDGET_ID, -1);
        if (widgetId < 0) {
            Log.w(TAG, "Missing EXTRA_APPWIDGET_ID!");
            return null;
        }
        return new BookmarkAdapter(getService(), widgetId);
    }

    static String getChangeFolderAction() {
        return ContextUtils.getApplicationContext().getPackageName() + ACTION_CHANGE_FOLDER_SUFFIX;
    }

    static SharedPreferences getWidgetState(int widgetId) {
        return ContextUtils.getApplicationContext()
                .getSharedPreferences(
                        String.format(Locale.US, "widgetState-%d", widgetId), Context.MODE_PRIVATE);
    }

    static void deleteWidgetState(int widgetId) {
        SharedPreferences preferences = getWidgetState(widgetId);
        if (preferences != null) preferences.edit().clear().apply();
    }

    static void changeFolder(Intent intent) {
        int widgetId = IntentUtils.safeGetIntExtra(intent, AppWidgetManager.EXTRA_APPWIDGET_ID, -1);
        String serializedFolder = IntentUtils.safeGetStringExtra(intent, EXTRA_FOLDER_ID);
        if (widgetId >= 0 && serializedFolder != null) {
            SharedPreferences prefs = getWidgetState(widgetId);
            prefs.edit().putString(PREF_CURRENT_FOLDER, serializedFolder).apply();
            redrawWidget(widgetId);
        }
    }

    /**
     * Redraws / refreshes a bookmark widget.
     *
     * @param widgetId The ID of the widget to redraw.
     */
    static void redrawWidget(int widgetId) {
        AppWidgetManager.getInstance(ContextUtils.getApplicationContext())
                .notifyAppWidgetViewDataChanged(widgetId, R.id.bookmarks_list);
    }

    /** Holds data describing a bookmark or bookmark folder. */
    private static class Bookmark {
        public String title;
        public GURL url;
        public BookmarkId id;
        public BookmarkId parentId;
        public boolean isFolder;
        public Bitmap favicon;

        public static Bookmark fromBookmarkItem(BookmarkItem item) {
            if (item == null) return null;

            Bookmark bookmark = new Bookmark();
            bookmark.title = item.getTitle();
            bookmark.url = item.getUrl();
            bookmark.id = item.getId();
            bookmark.parentId = item.getParentId();
            bookmark.isFolder = item.isFolder();
            return bookmark;
        }
    }

    /**
     * Holds the list of bookmarks in a folder, as well as information about the folder itself and
     * its parent folder, if any.
     */
    private static class BookmarkFolder {
        public Bookmark folder;
        @Nullable public Bookmark parent;
        public final List<Bookmark> children = new ArrayList<>();
    }

    /** Called when the BookmarkLoader has finished loading the bookmark folder. */
    private interface BookmarkLoaderCallback {
        @UiThread
        void onBookmarksLoaded(BookmarkFolder folder);
    }

    /**
     * Loads a BookmarkFolder asynchronously, and returns the result via BookmarkLoaderCallback.
     *
     * This class must be used only on the UI thread.
     */
    private static class BookmarkLoader {
        private BookmarkLoaderCallback mCallback;
        private BookmarkFolder mFolder;
        private BookmarkModel mBookmarkModel;
        private LargeIconBridge mLargeIconBridge;
        private RoundedIconGenerator mIconGenerator;
        private int mMinIconSizeDp;
        private int mDisplayedIconSize;
        private int mRemainingTaskCount;

        @UiThread
        public void initialize(
                Context context, final BookmarkId folderId, BookmarkLoaderCallback callback) {
            mCallback = callback;

            Resources res = context.getResources();
            mLargeIconBridge = new LargeIconBridge(ProfileManager.getLastUsedRegularProfile());
            mMinIconSizeDp = (int) res.getDimension(R.dimen.default_favicon_min_size);
            mDisplayedIconSize = res.getDimensionPixelSize(R.dimen.default_favicon_size);
            mIconGenerator = FaviconUtils.createRoundedRectangleIconGenerator(context);

            mRemainingTaskCount = 1;
            mBookmarkModel =
                    BookmarkModel.getForProfile(ProfileManager.getLastUsedRegularProfile());
            mBookmarkModel.finishLoadingBookmarkModel(
                    new Runnable() {
                        @Override
                        public void run() {
                            loadBookmarks(folderId);
                        }
                    });
        }

        @UiThread
        private void loadBookmarks(BookmarkId folderId) {
            mFolder = new BookmarkFolder();

            // Load the requested folder if it exists. Otherwise, fall back to the default folder.
            if (folderId != null) {
                mFolder.folder =
                        Bookmark.fromBookmarkItem(mBookmarkModel.getBookmarkById(folderId));
            }
            if (mFolder.folder == null) {
                folderId = mBookmarkModel.getDefaultBookmarkFolder();
                mFolder.folder =
                        Bookmark.fromBookmarkItem(mBookmarkModel.getBookmarkById(folderId));
            }

            mFolder.parent =
                    Bookmark.fromBookmarkItem(
                            mBookmarkModel.getBookmarkById(mFolder.folder.parentId));

            List<BookmarkItem> items = mBookmarkModel.getBookmarksForFolder(folderId);

            // Move folders to the beginning of the list.
            Collections.sort(
                    items,
                    new Comparator<BookmarkItem>() {
                        @Override
                        public int compare(BookmarkItem lhs, BookmarkItem rhs) {
                            return lhs.isFolder() == rhs.isFolder() ? 0 : lhs.isFolder() ? -1 : 1;
                        }
                    });

            for (BookmarkItem item : items) {
                Bookmark bookmark = Bookmark.fromBookmarkItem(item);
                loadFavicon(bookmark);
                mFolder.children.add(bookmark);
            }

            taskFinished();
        }

        @UiThread
        private void loadFavicon(final Bookmark bookmark) {
            if (bookmark.isFolder) return;

            mRemainingTaskCount++;
            LargeIconCallback callback =
                    new LargeIconCallback() {
                        @Override
                        public void onLargeIconAvailable(
                                Bitmap icon,
                                int fallbackColor,
                                boolean isFallbackColorDefault,
                                @IconType int iconType) {
                            if (icon == null) {
                                mIconGenerator.setBackgroundColor(fallbackColor);
                                icon = mIconGenerator.generateIconForUrl(bookmark.url);
                            } else {
                                icon =
                                        Bitmap.createScaledBitmap(
                                                icon, mDisplayedIconSize, mDisplayedIconSize, true);
                            }
                            bookmark.favicon = icon;
                            taskFinished();
                        }
                    };
            mLargeIconBridge.getLargeIconForUrl(bookmark.url, mMinIconSizeDp, callback);
        }

        @UiThread
        private void taskFinished() {
            mRemainingTaskCount--;
            if (mRemainingTaskCount == 0) {
                mCallback.onBookmarksLoaded(mFolder);
                destroy();
            }
        }

        @UiThread
        private void destroy() {
            mLargeIconBridge.destroy();
        }
    }

    /** Provides the RemoteViews, one per bookmark, to be shown in the widget. */
    private static class BookmarkAdapter
            implements RemoteViewsService.RemoteViewsFactory, SystemNightModeMonitor.Observer {
        // Can be accessed on any thread
        private final Context mContext;
        private final int mWidgetId;
        private final SharedPreferences mPreferences;
        private final RemoteViews mBookmarkWidgeRemoteView;
        private int mIconColor;

        // Accessed only on the UI thread
        private BookmarkModel mBookmarkModel;

        // Accessed only on binder threads.
        private BookmarkFolder mCurrentFolder;

        @UiThread
        public BookmarkAdapter(Context context, int widgetId) {
            mContext = context;
            mWidgetId = widgetId;
            mPreferences = getWidgetState(mWidgetId);
            mIconColor = getIconColor(mContext);
            SystemNightModeMonitor.getInstance().addObserver(this);
            mBookmarkWidgeRemoteView =
                    new RemoteViews(mContext.getPackageName(), R.layout.bookmark_widget);
            mBookmarkWidgeRemoteView.setOnClickPendingIntent(
                    R.id.empty_message,
                    BookmarkWidgetProxy.createBookmarkProxyLaunchIntent(context));
        }

        @UiThread
        @Override
        public void onCreate() {
            // Required to be applied here redundantly to prevent crashes in the cases where the
            // package data is deleted or the Chrome application forced to stop.
            ChromeBrowserInitializer.getInstance().handleSynchronousStartup();
            if (isWidgetNewlyCreated()) {
                RecordUserAction.record("BookmarkNavigatorWidgetAdded");
            }

            mBookmarkModel =
                    BookmarkModel.getForProfile(ProfileManager.getLastUsedRegularProfile());
            mBookmarkModel.addObserver(
                    new BookmarkModelObserver() {
                        @Override
                        public void bookmarkModelLoaded() {
                            // Do nothing. No need to refresh.
                        }

                        @Override
                        public void bookmarkModelChanged() {
                            redrawWidget(mWidgetId);
                        }
                    });
        }

        @UiThread
        private boolean isWidgetNewlyCreated() {
            // This method relies on the fact that PREF_CURRENT_FOLDER is not yet
            // set when onCreate is called for a newly created widget.
            String serializedFolder = mPreferences.getString(PREF_CURRENT_FOLDER, null);
            return serializedFolder == null;
        }

        @UiThread
        private void refreshWidget() {
            mContext.sendBroadcast(
                    new Intent(
                                    BookmarkWidgetProvider.getBookmarkAppWidgetUpdateAction(
                                            mContext),
                                    null,
                                    mContext,
                                    BookmarkThumbnailWidgetProvider.class)
                            .putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, mWidgetId));
        }

        // ---------------------------------------------------------------- //
        // Methods below this line are called on binder threads.            //
        // ---------------------------------------------------------------- //
        // Different methods may be called on *different* binder threads,   //
        // but the system ensures that the effects of each method call will //
        // be visible before the next method is called. Thus, additional    //
        // synchronization is not needed when accessing mCurrentFolder.     //
        // ---------------------------------------------------------------- //

        @BinderThread
        @Override
        public void onDestroy() {
            PostTask.runOrPostTask(
                    TaskTraits.UI_DEFAULT,
                    () -> {
                        SystemNightModeMonitor.getInstance().removeObserver(this);
                    });
            deleteWidgetState(mWidgetId);
        }

        @BinderThread
        @Override
        public void onDataSetChanged() {
            updateBookmarkList();
        }

        @BinderThread
        private void updateBookmarkList() {
            BookmarkId folderId =
                    BookmarkId.getBookmarkIdFromString(
                            mPreferences.getString(PREF_CURRENT_FOLDER, null));

            // Blocks until bookmarks are loaded from the UI thread.
            mCurrentFolder = loadBookmarks(folderId);

            // Update empty message visibility right after mCurrentFolder is updated.
            updateFolderEmptyMessageVisibility();

            mPreferences
                    .edit()
                    .putString(PREF_CURRENT_FOLDER, mCurrentFolder.folder.id.toString())
                    .apply();
        }

        @BinderThread
        private void updateFolderEmptyMessageVisibility() {
            AppWidgetManager appWidgetManager = AppWidgetManager.getInstance(mContext);
            if (!BookmarkWidgetProvider.shouldShowIconsOnly(appWidgetManager, mWidgetId)) {
                boolean folderIsEmpty = mCurrentFolder != null && mCurrentFolder.children.isEmpty();
                mBookmarkWidgeRemoteView.setViewVisibility(
                        R.id.empty_message, folderIsEmpty ? View.VISIBLE : View.GONE);

                // Directly update the widget on the UI thread.
                PostTask.runOrPostTask(
                        TaskTraits.UI_DEFAULT,
                        () -> {
                            // Use AppWidgetManager#partiallyUpdateAppWidget to update only the
                            // empty_message visibility, avoiding full widget redraws and redundant
                            // intent setup from BookmarkWidgetProvider#performUpdate.
                            appWidgetManager.partiallyUpdateAppWidget(
                                    mWidgetId, mBookmarkWidgeRemoteView);
                        });
            }
        }

        @BinderThread
        private BookmarkFolder loadBookmarks(final BookmarkId folderId) {
            final LinkedBlockingQueue<BookmarkFolder> resultQueue = new LinkedBlockingQueue<>(1);
            // A reference of BookmarkLoader is needed in binder thread to
            // prevent it from being garbage collected.
            final BookmarkLoader bookmarkLoader = new BookmarkLoader();
            PostTask.runOrPostTask(
                    TaskTraits.UI_DEFAULT,
                    () -> {
                        bookmarkLoader.initialize(
                                mContext,
                                folderId,
                                new BookmarkLoaderCallback() {
                                    @Override
                                    public void onBookmarksLoaded(BookmarkFolder folder) {
                                        resultQueue.add(folder);
                                    }
                                });
                    });
            try {
                return resultQueue.take();
            } catch (InterruptedException e) {
                return null;
            }
        }

        @BinderThread
        private Bookmark getBookmarkForPosition(int position) {
            if (mCurrentFolder == null) return null;

            // The position 0 is saved for an entry of the current folder used to go up.
            // This is not the case when the current node has no parent (it's the root node).
            if (mCurrentFolder.parent != null) {
                if (position == 0) return mCurrentFolder.folder;
                position--;
            }

            // This is necessary because when Chrome is cleared from Application settings, Bookmark
            // widget will not be notified and it causes inconsistency between model and widget.
            // Then if the widget is quickly scrolled down, this has an IndexOutOfBound error.
            if (mCurrentFolder.children.size() <= position) return null;

            return mCurrentFolder.children.get(position);
        }

        @BinderThread
        @Override
        public int getViewTypeCount() {
            return 2;
        }

        @BinderThread
        @Override
        public boolean hasStableIds() {
            return false;
        }

        @BinderThread
        @Override
        public int getCount() {
            // On some Sony devices, getCount() could be called before onDatasetChanged()
            // returns. If it happens, refresh widget until the bookmarks are all loaded.
            if (mCurrentFolder == null
                    || !mPreferences
                            .getString(PREF_CURRENT_FOLDER, "")
                            .equals(mCurrentFolder.folder.id.toString())) {
                PostTask.runOrPostTask(
                        TaskTraits.UI_DEFAULT,
                        () -> {
                            refreshWidget();
                        });
            }
            if (mCurrentFolder == null) {
                return 0;
            }
            return mCurrentFolder.children.size() + (mCurrentFolder.parent != null ? 1 : 0);
        }

        @BinderThread
        @Override
        public long getItemId(int position) {
            Bookmark bookmark = getBookmarkForPosition(position);
            if (bookmark == null) return BookmarkId.INVALID_FOLDER_ID;
            return bookmark.id.getId();
        }

        @BinderThread
        @Override
        public RemoteViews getLoadingView() {
            return new RemoteViews(mContext.getPackageName(), R.layout.bookmark_widget_item);
        }

        @BinderThread
        @Override
        public RemoteViews getViewAt(int position) {
            if (mCurrentFolder == null) {
                Log.w(TAG, "No current folder data available.");
                return null;
            }

            Bookmark bookmark = getBookmarkForPosition(position);
            if (bookmark == null) {
                Log.w(TAG, "Couldn't get bookmark for position %d", position);
                return null;
            }

            String title = bookmark.title;
            String url = bookmark.url.getSpec();
            BookmarkId id =
                    (bookmark == mCurrentFolder.folder) ? mCurrentFolder.parent.id : bookmark.id;

            RemoteViews views =
                    new RemoteViews(mContext.getPackageName(), R.layout.bookmark_widget_item);

            // Set the title of the bookmark. Use the url as a backup.
            views.setTextViewText(R.id.title, TextUtils.isEmpty(title) ? url : title);
            if (bookmark == mCurrentFolder.folder) {
                views.setInt(R.id.back_button, "setColorFilter", mIconColor);
                setWidgetItemBackButtonVisible(true, views);
            } else if (bookmark.isFolder) {
                views.setInt(R.id.favicon, "setColorFilter", mIconColor);
                views.setImageViewResource(R.id.favicon, R.drawable.ic_folder_blue_24dp);
                setWidgetItemBackButtonVisible(false, views);
            } else {
                // Clear any color filter so that it doesn't cover the favicon bitmap.
                views.setInt(R.id.favicon, "setColorFilter", 0);
                views.setImageViewBitmap(R.id.favicon, bookmark.favicon);
                setWidgetItemBackButtonVisible(false, views);
            }

            Intent fillIn;
            if (bookmark.isFolder) {
                fillIn =
                        new Intent(getChangeFolderAction())
                                .putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, mWidgetId)
                                .putExtra(EXTRA_FOLDER_ID, id.toString());
            } else {
                fillIn = new Intent(Intent.ACTION_VIEW);
                fillIn.putExtra(IntentHandler.EXTRA_PAGE_TRANSITION_BOOKMARK_ID, id.toString());
                if (!TextUtils.isEmpty(url)) {
                    fillIn = fillIn.addCategory(Intent.CATEGORY_BROWSABLE).setData(Uri.parse(url));
                } else {
                    fillIn = fillIn.addCategory(Intent.CATEGORY_LAUNCHER);
                }
            }
            views.setOnClickFillInIntent(R.id.list_item, fillIn);
            return views;
        }

        @Override
        public void onSystemNightModeChanged() {
            mIconColor = getIconColor(mContext);
            redrawWidget(mWidgetId);
        }

        private void setWidgetItemBackButtonVisible(boolean visible, RemoteViews views) {
            views.setViewVisibility(R.id.favicon, visible ? View.GONE : View.VISIBLE);
            views.setViewVisibility(R.id.back_button, visible ? View.VISIBLE : View.GONE);
        }

        private int getIconColor(Context context) {
            ContextThemeWrapper wrapper =
                    new ContextThemeWrapper(context, R.style.Theme_Chromium_Widget);

            return SemanticColorUtils.getDefaultIconColorSecondary(wrapper);
        }
    }
}
