// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview;

import android.graphics.Rect;
import android.widget.OverScroller;

import com.google.common.annotations.VisibleForTesting;
/// M: add log
import com.mediatek.xlog.Xlog;

/**
 * Takes care of syncing the scroll offset between the Android View system and the
 * InProcessViewRenderer.
 *
 * Unless otherwise values (sizes, scroll offsets) are in physical pixels.
 */
@VisibleForTesting
public class AwScrollOffsetManager {
    // Values taken from WebViewClassic.

    /// M: add log
    private static final String TAG = "AwScrollOffsetManager";
    private boolean mEnableLog = false;

    // The amount of content to overlap between two screens when using pageUp/pageDown methiods.
    private static final int PAGE_SCROLL_OVERLAP = 24;
    // Standard animated scroll speed.
    private static final int STD_SCROLL_ANIMATION_SPEED_PIX_PER_SEC = 480;
    // Time for the longest scroll animation.
    private static final int MAX_SCROLL_ANIMATION_DURATION_MILLISEC = 750;

    // The unit of all the values in this delegate are physical pixels.
    public interface Delegate {
        // Call View#overScrollBy on the containerView.
        void overScrollContainerViewBy(int deltaX, int deltaY, int scrollX, int scrollY,
                int scrollRangeX, int scrollRangeY, boolean isTouchEvent);
        // Call View#scrollTo on the containerView.
        void scrollContainerViewTo(int x, int y);
        // Store the scroll offset in the native side. This should really be a simple store
        // operation, the native side shouldn't synchronously alter the scroll offset from within
        // this call.
        void scrollNativeTo(int x, int y);

        int getContainerViewScrollX();
        int getContainerViewScrollY();

        void invalidate();
    }

    private final Delegate mDelegate;

    // Scroll offset as seen by the native side.
    private int mNativeScrollX;
    private int mNativeScrollY;

    // How many pixels can we scroll in a given direction.
    private int mMaxHorizontalScrollOffset;
    private int mMaxVerticalScrollOffset;

    // Size of the container view.
    private int mContainerViewWidth;
    private int mContainerViewHeight;

    // Whether we're in the middle of processing a touch event.
    private boolean mProcessingTouchEvent;

    private boolean mFlinging;

    // Whether (and to what value) to update the native side scroll offset after we've finished
    // processing a touch event.
    private boolean mApplyDeferredNativeScroll;
    private int mDeferredNativeScrollX;
    private int mDeferredNativeScrollY;

    // The velocity of the last recorded fling,
    private int mLastFlingVelocityX;
    private int mLastFlingVelocityY;

    private OverScroller mScroller;

    public AwScrollOffsetManager(Delegate delegate, OverScroller overScroller) {
        mDelegate = delegate;
        mScroller = overScroller;
    }

    /// M: add log
    public void setEnableLog(boolean enable) {
        mEnableLog = enable;
    }

    //----- Scroll range and extent calculation methods -------------------------------------------

    public int computeHorizontalScrollRange() {
        return mContainerViewWidth + mMaxHorizontalScrollOffset;
    }

    public int computeMaximumHorizontalScrollOffset() {
        if (mEnableLog) {
            Xlog.d(TAG, "computeMaximumHorizontalScrollOffset " + mMaxHorizontalScrollOffset);
        }
        return mMaxHorizontalScrollOffset;
    }

    public int computeHorizontalScrollOffset() {
        return mDelegate.getContainerViewScrollX();
    }

    public int computeVerticalScrollRange() {
        return mContainerViewHeight + mMaxVerticalScrollOffset;
    }

    public int computeMaximumVerticalScrollOffset() {
        if (mEnableLog) {
            Xlog.d(TAG, "computeMaximumVerticalScrollOffset " + mMaxVerticalScrollOffset);
        }
        return mMaxVerticalScrollOffset;
    }

    public int computeVerticalScrollOffset() {
        return mDelegate.getContainerViewScrollY();
    }

    public int computeVerticalScrollExtent() {
        return mContainerViewHeight;
    }

    //---------------------------------------------------------------------------------------------
    // Called when the scroll range changes. This needs to be the size of the on-screen content.
    public void setMaxScrollOffset(int width, int height) {
        if (mEnableLog) {
            Xlog.d(TAG, "setMaxScrollOffset " + width + ", " + height);
        }
        mMaxHorizontalScrollOffset = width;
        mMaxVerticalScrollOffset = height;
    }

    // Called when the physical size of the view changes.
    public void setContainerViewSize(int width, int height) {
        if (mEnableLog) {
            Xlog.d(TAG, "setContainerViewSize " + width + ", " + height);
        }
        mContainerViewWidth = width;
        mContainerViewHeight = height;
    }

    public void syncScrollOffsetFromOnDraw() {
        // Unfortunately apps override onScrollChanged without calling super which is why we need
        // to sync the scroll offset on every onDraw.
        onContainerViewScrollChanged(mDelegate.getContainerViewScrollX(),
                mDelegate.getContainerViewScrollY());
    }

    public void setProcessingTouchEvent(boolean processingTouchEvent) {
        assert mProcessingTouchEvent != processingTouchEvent;
        mProcessingTouchEvent = processingTouchEvent;

        if (!mProcessingTouchEvent && mApplyDeferredNativeScroll) {
            mApplyDeferredNativeScroll = false;
            scrollNativeTo(mDeferredNativeScrollX, mDeferredNativeScrollY);
        }
    }

    // Called by the native side to scroll the container view.
    public void scrollContainerViewTo(int x, int y) {
        if (mEnableLog) {
            Xlog.d(TAG, "scrollContainerViewTo " + x + ", " + y);
        }
        mNativeScrollX = x;
        mNativeScrollY = y;

        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();
        final int deltaX = x - scrollX;
        final int deltaY = y - scrollY;
        final int scrollRangeX = computeMaximumHorizontalScrollOffset();
        final int scrollRangeY = computeMaximumVerticalScrollOffset();

        // We use overScrollContainerViewBy to be compatible with WebViewClassic which used this
        // method for handling both over-scroll as well as in-bounds scroll.
        mDelegate.overScrollContainerViewBy(deltaX, deltaY, scrollX, scrollY,
                scrollRangeX, scrollRangeY, mProcessingTouchEvent);
    }

    public boolean isFlingActive() {
        return mFlinging;
    }

    // Called by the native side to over-scroll the container view.
    public void overScrollBy(int deltaX, int deltaY) {
        // TODO(mkosiba): Once http://crbug.com/260663 and http://crbug.com/261239 are fixed it
        // should be possible to uncomment the following asserts:
        // if (deltaX < 0) assert mDelegate.getContainerViewScrollX() == 0;
        // if (deltaX > 0) assert mDelegate.getContainerViewScrollX() ==
        //          computeMaximumHorizontalScrollOffset();
        scrollBy(deltaX, deltaY);
    }

    private void scrollBy(int deltaX, int deltaY) {
        if (mEnableLog) {
            Xlog.d(TAG, "scrollBy " + deltaX + ", " + deltaY);
        }
        if (deltaX == 0 && deltaY == 0) return;

        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();
        final int scrollRangeX = computeMaximumHorizontalScrollOffset();
        final int scrollRangeY = computeMaximumVerticalScrollOffset();

        // The android.view.View.overScrollBy method is used for both scrolling and over-scrolling
        // which is why we use it here.
        mDelegate.overScrollContainerViewBy(deltaX, deltaY, scrollX, scrollY,
                scrollRangeX, scrollRangeY, mProcessingTouchEvent);
    }

    private int clampHorizontalScroll(int scrollX) {
        scrollX = Math.max(0, scrollX);
        scrollX = Math.min(computeMaximumHorizontalScrollOffset(), scrollX);
        return scrollX;
    }

    private int clampVerticalScroll(int scrollY) {
        scrollY = Math.max(0, scrollY);
        scrollY = Math.min(computeMaximumVerticalScrollOffset(), scrollY);
        return scrollY;
    }

    // Called by the View system as a response to the mDelegate.overScrollContainerViewBy call.
    public void onContainerViewOverScrolled(int scrollX, int scrollY, boolean clampedX,
            boolean clampedY) {
        // Clamp the scroll offset at (0, max).
        scrollX = clampHorizontalScroll(scrollX);
        scrollY = clampVerticalScroll(scrollY);

        mDelegate.scrollContainerViewTo(scrollX, scrollY);

        // This is only necessary if the containerView scroll offset ends up being different
        // than the one set from native in which case we want the value stored on the native side
        // to reflect the value stored in the containerView (and not the other way around).
        scrollNativeTo(mDelegate.getContainerViewScrollX(), mDelegate.getContainerViewScrollY());
    }

    // Called by the View system when the scroll offset had changed. This might not get called if
    // the embedder overrides WebView#onScrollChanged without calling super.onScrollChanged. If
    // this method does get called it is called both as a response to the embedder scrolling the
    // view as well as a response to mDelegate.scrollContainerViewTo.
    public void onContainerViewScrollChanged(int x, int y) {
        scrollNativeTo(x, y);
    }

    private void scrollNativeTo(int x, int y) {
        x = clampHorizontalScroll(x);
        y = clampVerticalScroll(y);

        // We shouldn't do the store to native while processing a touch event since that confuses
        // the gesture processing logic.
        if (mProcessingTouchEvent) {
            mDeferredNativeScrollX = x;
            mDeferredNativeScrollY = y;
            mApplyDeferredNativeScroll = true;
            return;
        }

        if (x == mNativeScrollX && y == mNativeScrollY)
            return;

        // The scrollNativeTo call should be a simple store, so it's OK to assume it always
        // succeeds.
        mNativeScrollX = x;
        mNativeScrollY = y;

        mDelegate.scrollNativeTo(x, y);
    }

    // Called at the beginning of every fling gesture.
    public void onFlingStartGesture(int velocityX, int velocityY) {
        mLastFlingVelocityX = velocityX;
        mLastFlingVelocityY = velocityY;
    }

    // Called whenever some other touch interaction requires the fling gesture to be canceled.
    public void onFlingCancelGesture() {
        // TODO(mkosiba): Support speeding up a fling by flinging again.
        // http://crbug.com/265841
        mScroller.forceFinished(true);
    }

    // Called when a fling gesture is not handled by the renderer.
    // We explicitly ask the renderer not to handle fling gestures targeted at the root
    // scroll layer.
    public void onUnhandledFlingStartEvent() {
        flingScroll(-mLastFlingVelocityX, -mLastFlingVelocityY);
    }

    // Starts the fling animation. Called both as a response to a fling gesture and as via the
    // public WebView#flingScroll(int, int) API.
    public void flingScroll(int velocityX, int velocityY) {
        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();
        final int scrollRangeX = computeMaximumHorizontalScrollOffset();
        final int scrollRangeY = computeMaximumVerticalScrollOffset();

        mScroller.fling(scrollX, scrollY, velocityX, velocityY,
                0, scrollRangeX, 0, scrollRangeY);
        mFlinging = true;
        mDelegate.invalidate();
    }

    // Called immediately before the draw to update the scroll offset.
    public void computeScrollAndAbsorbGlow(OverScrollGlow overScrollGlow) {
        final boolean stillAnimating = mScroller.computeScrollOffset();
        if (!stillAnimating) {
            mFlinging = false;
            return;
        }

        final int oldX = mDelegate.getContainerViewScrollX();
        final int oldY = mDelegate.getContainerViewScrollY();
        int x = mScroller.getCurrX();
        int y = mScroller.getCurrY();

        final int scrollRangeX = computeMaximumHorizontalScrollOffset();
        final int scrollRangeY = computeMaximumVerticalScrollOffset();

        if (overScrollGlow != null) {
            overScrollGlow.absorbGlow(x, y, oldX, oldY, scrollRangeX, scrollRangeY,
                    mScroller.getCurrVelocity());
        }

        // The mScroller is configured not to go outside of the scrollable range, so this call
        // should never result in attempting to scroll outside of the scrollable region.
        scrollBy(x - oldX, y - oldY);

        mDelegate.invalidate();
    }

    private static int computeDurationInMilliSec(int dx, int dy) {
        int distance = Math.max(Math.abs(dx), Math.abs(dy));
        int duration = distance * 1000 / STD_SCROLL_ANIMATION_SPEED_PIX_PER_SEC;
        return Math.min(duration, MAX_SCROLL_ANIMATION_DURATION_MILLISEC);
    }

    private boolean animateScrollTo(int x, int y) {
        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();

        x = clampHorizontalScroll(x);
        y = clampVerticalScroll(y);

        int dx = x - scrollX;
        int dy = y - scrollY;

        if (dx == 0 && dy == 0)
            return false;

        mScroller.startScroll(scrollX, scrollY, dx, dy, computeDurationInMilliSec(dx, dy));
        mDelegate.invalidate();

        return true;
    }

    /**
     * See {@link WebView#pageUp(boolean)}
     */
    public boolean pageUp(boolean top) {
        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();

        if (top) {
            // go to the top of the document
            return animateScrollTo(scrollX, 0);
        }
        int dy = -mContainerViewHeight / 2;
        if (mContainerViewHeight > 2 * PAGE_SCROLL_OVERLAP) {
            dy = -mContainerViewHeight + PAGE_SCROLL_OVERLAP;
        }
        // animateScrollTo clamps the argument to the scrollable range so using (scrollY + dy) is
        // fine.
        return animateScrollTo(scrollX, scrollY + dy);
    }

    /**
     * See {@link WebView#pageDown(boolean)}
     */
    public boolean pageDown(boolean bottom) {
        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();

        if (bottom) {
            return animateScrollTo(scrollX, computeVerticalScrollRange());
        }
        int dy = mContainerViewHeight / 2;
        if (mContainerViewHeight > 2 * PAGE_SCROLL_OVERLAP) {
            dy = mContainerViewHeight - PAGE_SCROLL_OVERLAP;
        }
        // animateScrollTo clamps the argument to the scrollable range so using (scrollY + dy) is
        // fine.
        return animateScrollTo(scrollX, scrollY + dy);
    }

    /**
     * See {@link WebView#requestChildRectangleOnScreen(View, Rect, boolean)}
     */
    public boolean requestChildRectangleOnScreen(int childOffsetX, int childOffsetY, Rect rect,
            boolean immediate) {
        // TODO(mkosiba): WebViewClassic immediately returns false if a zoom animation is
        // in progress. We currently can't tell if one is happening.. should we instead cancel any
        // scroll animation when the size/pageScaleFactor changes?

        // TODO(mkosiba): Take scrollbar width into account in the screenRight/screenBotton
        // calculations. http://crbug.com/269032

        final int scrollX = mDelegate.getContainerViewScrollX();
        final int scrollY = mDelegate.getContainerViewScrollY();

        rect.offset(childOffsetX, childOffsetY);

        int screenTop = scrollY;
        int screenBottom = scrollY + mContainerViewHeight;
        int scrollYDelta = 0;

        if (rect.bottom > screenBottom) {
            int oneThirdOfScreenHeight = mContainerViewHeight / 3;
            if (rect.width() > 2 * oneThirdOfScreenHeight) {
                // If the rectangle is too tall to fit in the bottom two thirds
                // of the screen, place it at the top.
                scrollYDelta = rect.top - screenTop;
            } else {
                // If the rectangle will still fit on screen, we want its
                // top to be in the top third of the screen.
                scrollYDelta = rect.top - (screenTop + oneThirdOfScreenHeight);
            }
        } else if (rect.top < screenTop) {
            scrollYDelta = rect.top - screenTop;
        }

        int screenLeft = scrollX;
        int screenRight = scrollX + mContainerViewWidth;
        int scrollXDelta = 0;

        if (rect.right > screenRight && rect.left > screenLeft) {
            if (rect.width() > mContainerViewWidth) {
                scrollXDelta += (rect.left - screenLeft);
            } else {
                scrollXDelta += (rect.right - screenRight);
            }
        } else if (rect.left < screenLeft) {
            scrollXDelta -= (screenLeft - rect.left);
        }

        if (scrollYDelta == 0 && scrollXDelta == 0) {
            return false;
        }

        if (immediate) {
            scrollBy(scrollXDelta, scrollYDelta);
            return true;
        } else {
            return animateScrollTo(scrollX + scrollXDelta, scrollY + scrollYDelta);
        }
    }
}
