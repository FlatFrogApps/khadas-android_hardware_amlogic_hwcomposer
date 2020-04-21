/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <MesonLog.h>
#include "HwcVideoPlane.h"
#include "AmFramebuffer.h"
#include <DebugHelper.h>
#include <OmxUtil.h>
#include <misc.h>


HwcVideoPlane::HwcVideoPlane(int32_t drvFd, uint32_t id)
    : HwDisplayPlane(drvFd, id) {
    snprintf(mName, 64, "HwcVideo-%d", id);
    mLastComposeFbs.clear();
    mLastFence = std::make_shared<DrmFence>(-1);
    memset(mAmVideosPath, 0, sizeof(mAmVideosPath));
}

HwcVideoPlane::~HwcVideoPlane() {
}

const char * HwcVideoPlane::getName() {
    return mName;
}

uint32_t HwcVideoPlane::getPlaneType() {
    return HWC_VIDEO_PLANE;
}

uint32_t HwcVideoPlane::getCapabilities() {
    /*HWCVideoplane always support zorder.*/
    return PLANE_SUPPORT_ZORDER;
}

int32_t HwcVideoPlane::getFixedZorder() {
    return INVALID_ZORDER;
}

uint32_t HwcVideoPlane::getPossibleCrtcs() {
    return CRTC_VOUT1;
}

bool HwcVideoPlane::isFbSupport(std::shared_ptr<DrmFramebuffer> & fb) {
    if (fb->mFbType == DRM_FB_VIDEO_OMX_V4L ||
        fb->mFbType == DRM_FB_VIDEO_OMX2_V4L2)
        return true;

    return false;
}

void HwcVideoPlane::setAmVideoPath(int id) {
    if (id == 0) {
        strncpy(mAmVideosPath, "/sys/class/video/disable_video", sizeof(mAmVideosPath));
    } else if (id == 1) {
        strncpy(mAmVideosPath, "/sys/class/video/disable_videopip", sizeof(mAmVideosPath));
    } else {
        // nothing
    }
}

int32_t HwcVideoPlane::getVideodisableStatus(int& status) {
    /*fun is only called in updateVideodisableStatus(), mVideoDisableFd >= 0*/
    char buf[32] = {0};
    int ret;
    int fd = -1;

    if (strlen(mAmVideosPath) == 0)
        return -1;

    if ((fd = open(mAmVideosPath,  O_RDWR, 0)) < 0) {
        MESON_LOGE("open %s failed", mAmVideosPath);
        return -1;
    }

    if ((ret = read(fd, buf, sizeof(buf))) < 0) {
        MESON_LOGE("get video disable failed, ret=%d error=%s", ret, strerror(errno));
    } else {
        sscanf(buf, "%d", &status);
        ret = 0;
    }

    close(fd);
    return ret;
}

int32_t HwcVideoPlane::setVideodisableStatus(int status) {
    /*fun is only called in updateVideodisableStatus(), mVideoDisableFd >= 0*/
    char buf[32] = {0};
    int ret;
    int fd = -1;

    if (strlen(mAmVideosPath) == 0)
        return -1;

    if ((fd = open(mAmVideosPath,  O_RDWR, 0)) < 0) {
        MESON_LOGE("open %s failed", mAmVideosPath);
        return -1;
    }

    snprintf(buf, 32, "%d", status);
    if ((ret = write(fd, buf, strnlen(buf, sizeof(buf)))) < 0) {
        MESON_LOGE("set video disable failed, ret=%d", ret);
    } else {
        ret = 0;
    }

    close(fd);
    return ret;
}

int32_t HwcVideoPlane::setComposePlane(
    DiComposerPair *difbs, int blankOp) {
    MESON_ASSERT(mDrvFd >= 0, "osd plane fd is not valiable!");
    MESON_ASSERT(blankOp == UNBLANK, "osd plane fd is not valiable!");

    u32 i;
    video_frame_info_t *vFrameInfo;
    int video_composer_enable;
    std::shared_ptr<DrmFramebuffer> fb;
    std::vector<std::shared_ptr<DrmFramebuffer>> composeFbs;
    std::shared_ptr<DrmFence> compseFence;
    int sideband_type;

    memset(&mVideoFramesInfo, 0, sizeof(mVideoFramesInfo));
    mFramesCount = difbs->composefbs.size();
    for (i = 0; i < mFramesCount; i++) {
        vFrameInfo = &mVideoFramesInfo.frame_info[i];
        fb = difbs->composefbs[i];

        buffer_handle_t buf = fb->mBufferHandle;
        drm_rect_t dispFrame = fb->mDisplayFrame;
        drm_rect_t srcCrop = fb->mSourceCrop;
        vFrameInfo->sideband_type = 0;
        if (fb->mFbType == DRM_FB_VIDEO_OMX_V4L ||
            fb->mFbType == DRM_FB_VIDEO_OMX2_V4L2) {
            vFrameInfo->fd = am_gralloc_get_omx_v4l_file(buf);
            vFrameInfo->type = 0;
        } else if (fb->mFbType == DRM_FB_VIDEO_DMABUF) {
            vFrameInfo->fd = am_gralloc_get_buffer_fd(buf);
            vFrameInfo->type = 1;
        } else if (fb->mFbType == DRM_FB_VIDEO_SIDEBAND ||
            fb->mFbType == DRM_FB_VIDEO_SIDEBAND_SECOND ||
            fb->mFbType == DRM_FB_VIDEO_SIDEBAND_TV) {
            vFrameInfo->type = 2;
            am_gralloc_get_sideband_type(buf, &sideband_type);
            vFrameInfo->sideband_type = sideband_type;
        }

        vFrameInfo->dst_x = dispFrame.left;
        vFrameInfo->dst_y = dispFrame.top;
        vFrameInfo->dst_w = dispFrame.right - dispFrame.left;
        vFrameInfo->dst_h = dispFrame.bottom - dispFrame.top;

        vFrameInfo->crop_x = srcCrop.left;
        vFrameInfo->crop_y = srcCrop.top;
        vFrameInfo->crop_w = srcCrop.right - srcCrop.left;
        vFrameInfo->crop_h = srcCrop.bottom - srcCrop.top;
        vFrameInfo->buffer_w = am_gralloc_get_width(buf);
        vFrameInfo->buffer_h = am_gralloc_get_height(buf);
        vFrameInfo->zorder = fb->mZorder;
        vFrameInfo->transform = fb->mTransform;
    }
    mVideoFramesInfo.frame_count = mFramesCount;
    mVideoFramesInfo.layer_index = mId;
    mVideoFramesInfo.disp_zorder = difbs->zorder;

    if (!mStatus) {
        video_composer_enable = 1;
        if (ioctl(mDrvFd, VIDEO_COMPOSER_IOCTL_SET_ENABLE, &video_composer_enable) != 0) {
            MESON_LOGE("video composer: ioctl error, %s(%d), mDrvFd = %d",
                strerror(errno), errno, mDrvFd);
            return -1;
        }
        mStatus = 1;
    }

    /* update video plane disable status */
    if (fb) {
        int blankStatus = 0;
        getVideodisableStatus(blankStatus);

        /* the value of blankOp is UNBLANK */
        if (fb->mFbType == DRM_FB_VIDEO_OMX_V4L ||
            fb->mFbType == DRM_FB_VIDEO_OMX2_V4L2) {
            if (blankStatus == 1) {
                setVideodisableStatus(2);
            }
        } else {
            MESON_LOGI("not support video fb type: %d", fb->mFbType);
        }
    }

    if (ioctl(mDrvFd, VIDEO_COMPOSER_IOCTL_SET_FRAMES, &mVideoFramesInfo) != 0) {
        MESON_LOGE("video composer: ioctl error, %s(%d), mDrvFd = %d",
            strerror(errno), errno, mDrvFd);
        return -1;
    }

    composeFbs = difbs->composefbs;
    int composefd =
        (vFrameInfo->composer_fen_fd >= 0) ? vFrameInfo->composer_fen_fd : -1;
    compseFence = std::make_shared<DrmFence>(composefd);

    /*update last frame release fence*/
    if (mLastComposeFbs.size() > 0) {
        for (auto fbIt = mLastComposeFbs.begin(); fbIt != mLastComposeFbs.end(); ++fbIt) {
            if (DebugHelper::getInstance().discardOutFence()) {
                (*fbIt)->setReleaseFence(-1);
            } else {
                (*fbIt)->setReleaseFence(mLastFence->dup());
            }
        }
        mLastComposeFbs.clear();
        mLastFence.reset();
    }

    mLastComposeFbs = composeFbs;
    mLastFence = compseFence;
    composeFbs.clear();
    compseFence.reset();

    return 0;
}

int32_t HwcVideoPlane::setPlane(
    std::shared_ptr<DrmFramebuffer> fb __unused,
    uint32_t zorder __unused, int blankOp __unused) {
    MESON_ASSERT(mDrvFd >= 0, "osd plane fd is not valiable!");
    MESON_ASSERT(blankOp != UNBLANK, "only called when blank now!");

    if (mStatus) {
        int video_composer_enable = 0;
        MESON_LOGE("di composer device %d set disable.\n", mDrvFd);
        if (ioctl(mDrvFd, VIDEO_COMPOSER_IOCTL_SET_ENABLE, &video_composer_enable) != 0) {
            MESON_LOGE("video composer: ioctl error, %s(%d), mDrvFd = %d",
                strerror(errno), errno, mDrvFd);
            return -1;
        }
        mStatus = 0;
    }

    /* update video plane disable status */
    if (fb) {
        int blankStatus = 0;
        getVideodisableStatus(blankStatus);

        /* the value of blankOp is UNBLANK */
        if (fb->mFbType == DRM_FB_VIDEO_OMX_V4L ||
            fb->mFbType == DRM_FB_VIDEO_OMX2_V4L2) {
            if (blankStatus == 1) {
                setVideodisableStatus(2);
            }
        } else {
            MESON_LOGI("not support video fb type: %d", fb->mFbType);
        }
    }

    /*update last frame release fence*/
    if (mLastComposeFbs.size() > 0) {
        for (auto fbIt = mLastComposeFbs.begin(); fbIt != mLastComposeFbs.end(); ++fbIt) {
            if (DebugHelper::getInstance().discardOutFence()) {
                (*fbIt)->setReleaseFence(-1);
            } else {
                (*fbIt)->setReleaseFence(mLastFence->dup());
            }
        }
        mLastComposeFbs.clear();
        mLastFence.reset();
    }

    return 0;
}

void HwcVideoPlane::dump(String8 & dumpstr __unused) {
    u32 i;
    video_frame_info_t *vFrameInfo;
    for (i = 0; i < mFramesCount; i++) {
        vFrameInfo = &mVideoFramesInfo.frame_info[i];
        dumpstr.appendFormat("HwcVideo%2d "
                "     %3d | %1d | %4d, %4d, %4d, %4d |  %4d, %4d, %4d, %4d | %2d | %2d",
                 mId,
                 vFrameInfo->zorder,
                 vFrameInfo->fd,
                 vFrameInfo->crop_x, vFrameInfo->crop_y, vFrameInfo->crop_w, vFrameInfo->crop_h,
                 vFrameInfo->dst_x, vFrameInfo->dst_y, vFrameInfo->dst_w, vFrameInfo->dst_h,
                 vFrameInfo->composer_fen_fd,
                 vFrameInfo->disp_fen_fd);
    }
}

