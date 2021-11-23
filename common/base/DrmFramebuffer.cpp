/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <DrmFramebuffer.h>
#include <MesonLog.h>
#include <misc.h>

DrmFramebuffer::DrmFramebuffer()
      : mBufferHandle(NULL),
        mMapBase(NULL) {
    mUpdated = false;
    mIsSidebandBuffer = false;
    reset();
}

DrmFramebuffer::DrmFramebuffer(
    const native_handle_t * bufferhnd, int32_t acquireFence)
      : mBufferHandle(NULL),
        mMapBase(NULL) {
    mUpdated = false;
    reset();
    setBufferInfo(bufferhnd, acquireFence);

    if (bufferhnd) {
        mDisplayFrame.left   = mSourceCrop.left   = 0;
        mDisplayFrame.top    = mSourceCrop.top    = 0;
        mDisplayFrame.right  = mSourceCrop.right  = am_gralloc_get_width(bufferhnd);
        mDisplayFrame.bottom = mSourceCrop.bottom = am_gralloc_get_height(bufferhnd);
    }
}

DrmFramebuffer::~DrmFramebuffer() {
    reset();
}

void DrmFramebuffer::setUniqueId(hwc2_layer_t id) {
    mId = id;
}

hwc2_layer_t DrmFramebuffer::getUniqueId() {
    return mId;
}

int32_t DrmFramebuffer::setAcquireFence(int32_t fenceFd) {
    mAcquireFence = std::make_shared<DrmFence>(fenceFd);
    return 0;
}

std::shared_ptr<DrmFence> DrmFramebuffer::getAcquireFence() {
    return mAcquireFence;
}

int32_t DrmFramebuffer::setPrevReleaseFence(int32_t fenceFd) {
    mPrevReleaseFence.reset(new DrmFence(fenceFd));
    return 0;
}

int32_t DrmFramebuffer::setProcessFence(int32_t fenceFd) {
    mProcessFence.reset(new DrmFence(fenceFd));
    return 0;
}

int32_t DrmFramebuffer::getProcessFence() {
    if (mProcessFence.get())
        return mProcessFence->dup();
    return -1;
}

int32_t DrmFramebuffer::onLayerDisplayed(int32_t releaseFence,
        int32_t processFence) {
    if (releaseFence < 0 || processFence < 0) {
        int32_t fenceFd = (releaseFence < 0) ? processFence : releaseFence;
        mCurReleaseFence.reset(new DrmFence(fenceFd));
    } else {
        mCurReleaseFence = DrmFence::merge(
                                    "FBProcessor",
                                    std::make_shared<DrmFence>(releaseFence),
                                    std::make_shared<DrmFence>(processFence));
    }
    return 0;
}

int32_t DrmFramebuffer::setCurReleaseFence(int32_t fenceFd) {
    mCurReleaseFence.reset(new DrmFence(fenceFd));
    return 0;
}

int32_t DrmFramebuffer::getPrevReleaseFence() {
    if (mPrevReleaseFence.get())
        return mPrevReleaseFence->dup();
    return -1;
}

int32_t DrmFramebuffer::getCurReleaseFence() {
    if (mCurReleaseFence.get())
        return mCurReleaseFence->dup();
    return -1;
}

void DrmFramebuffer::reset() {
    clearBufferInfo();
    mPrevReleaseFence.reset();
    mCurReleaseFence.reset();
    mProcessFence.reset();
    mHdrMetaData.clear();
    mBlendMode       = DRM_BLEND_MODE_INVALID;
    mPlaneAlpha      = 1.0;
    mTransform       = 0;
    mZorder          = 0xFFFFFFFF; //set to special value for debug.
    mDataspace       = 0;
    mCompositionType = 0;

    mDisplayFrame.left   = mSourceCrop.left   = 0;
    mDisplayFrame.top    = mSourceCrop.top    = 0;
    mDisplayFrame.right  = mSourceCrop.right  = 0;
    mDisplayFrame.bottom = mSourceCrop.bottom = 0;
    memset(&mVtSourceCrop, 0, sizeof(mVtSourceCrop));
}

void DrmFramebuffer::clearFbHandleFlag() {
    mFbHandleUpdated = false;
}

void DrmFramebuffer::setBufferInfo(
    const native_handle_t * bufferhnd,
    int32_t acquireFence,
    bool isSidebandBuffer) {
    if (bufferhnd) {
        mFbHandleUpdated = true;
        mIsSidebandBuffer = isSidebandBuffer;
        mBufferHandle = gralloc_ref_dma_buf(bufferhnd, isSidebandBuffer);
        if (acquireFence >= 0)
            mAcquireFence = std::make_shared<DrmFence>(acquireFence);
    }
}

void DrmFramebuffer::clearBufferInfo() {
    if (mBufferHandle) {
        unlock();
        gralloc_unref_dma_buf(mBufferHandle, mIsSidebandBuffer);
        mBufferHandle  = NULL;
        mFbHandleUpdated = false;
    }

    /* clearBufferInfo() means buffer changed,
    * for release fence:
    * 1. reset PrevRelease which already used in last loop.
    * 2. CureRelease move to PrevRelase, it can be returned in next loop.
    */
    mAcquireFence.reset();
    mAcquireFence = DrmFence::NO_FENCE;
    // not reset prevReleaseFence for sidebandBuffer, as vt sidebanbuffer use it later
    if (!isVtBufferLocked()) {
        mPrevReleaseFence = mCurReleaseFence;
        mCurReleaseFence.reset();
        mCurReleaseFence = DrmFence::NO_FENCE;
    }
    mProcessFence.reset();
    mProcessFence = DrmFence::NO_FENCE;

    mFbType        = DRM_FB_RENDER;
    mSecure         = false;
}

int32_t DrmFramebuffer::lock(void ** addr) {
    if (!mMapBase) {
        if (0 != gralloc_lock_dma_buf(mBufferHandle, &mMapBase)) {
            mMapBase = NULL;
            return -EIO;
        }
    }

    *addr = mMapBase;
    return 0;
}

int32_t DrmFramebuffer::unlock() {
    if (mMapBase && mBufferHandle) {
        gralloc_unlock_dma_buf(mBufferHandle);
        mMapBase = NULL;
    }

    return 0;
}

bool DrmFramebuffer::isRotated() {
    return mTransform != 0;
}

drm_fb_type_t DrmFramebuffer::getFbType() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mFbType;
}

drm_rect_t DrmFramebuffer::getSourceCrop() {
    // overrider the source crop if has vtSourceCrop
    if (mVtSourceCrop.left > 0 || mVtSourceCrop.top > 0 ||
            mVtSourceCrop.right > 0 || mVtSourceCrop.bottom > 0) {
        return mVtSourceCrop;
    } else {
        if (isVtBuffer()) {
            drm_rect_t crop = {0, 0, -1, -1};
            return crop;
        }

        return mSourceCrop;
    }
}
