/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <misc.h>
#include <DrmTypes.h>
#include <BasicTypes.h>
#include <MesonLog.h>
#include <Vdin.h>
#include "VdinPostProcessor.h"

//#define VDIN_POSTPROCESS_DEBUG 1
//#define VDIN_CAP_ALWAYS_ON 1

#define DEFAULT_FB_ZORDER (1)

#define VDIN_BUF_CNT (4)
#define VOUT_BUF_CNT (3)
/*vdin will keep one frame always*/
#define VDIN_CAP_CNT (VDIN_BUF_CNT - 1)


VdinPostProcessor::VdinPostProcessor() {
    mExitThread = true;
    mStat = PROCESSOR_STOP;
}

VdinPostProcessor::~VdinPostProcessor() {
    mVdinFbs.clear();
    mPlanes.clear();

    mFbProcessor.reset();
}

int32_t VdinPostProcessor::setVout(
    std::shared_ptr<HwDisplayCrtc> & crtc,
    std::vector<std::shared_ptr<HwDisplayPlane>> & planes,
    int w, int h) {
    mVout = crtc;
    mPlanes = planes;
    auto it = mPlanes.begin();
    for (; it != mPlanes.end(); it ++) {
        if ((*it)->getPlaneType() == OSD_PLANE) {
            mDisplayPlane = *it;
            mPlanes.erase(it);
            break;
        }
    }
    MESON_ASSERT(mDisplayPlane != NULL, "plane size should > 0.");

    mVoutW = w;
    mVoutH = h;

    return 0;
}

int32_t VdinPostProcessor::postVout(std::shared_ptr<DrmFramebuffer> fb) {
    if (fb.get() != NULL) {
        display_zoom_info_t osdDisplayFrame;
        osdDisplayFrame.framebuffer_w = fb->mSourceCrop.right - fb->mSourceCrop.left;
        osdDisplayFrame.framebuffer_h = fb->mSourceCrop.bottom - fb->mSourceCrop.top;
        osdDisplayFrame.crtc_display_x = 0;
        osdDisplayFrame.crtc_display_y = 0;
        osdDisplayFrame.crtc_display_w = fb->mDisplayFrame.right -
            fb->mDisplayFrame.left;
        osdDisplayFrame.crtc_display_h = fb->mDisplayFrame.bottom -
            fb->mDisplayFrame.top;
        mVout->setDisplayFrame(osdDisplayFrame);

        fb->mFbType = DRM_FB_SCANOUT;
        fb->mBlendMode = DRM_BLEND_MODE_PREMULTIPLIED;
        fb->mPlaneAlpha = 1.0f;
        fb->mTransform = 0;
        mDisplayPlane->setPlane(fb, DEFAULT_FB_ZORDER, UNBLANK);
    } else {
        mDisplayPlane->setPlane(NULL, DEFAULT_FB_ZORDER, BLANK_FOR_NO_CONTENT);
    }

    /*blank unused planes.*/
    for (auto it = mPlanes.begin(); it != mPlanes.end(); it ++) {
        (*it)->setPlane(NULL, DEFAULT_FB_ZORDER, BLANK_FOR_NO_CONTENT);
    }

    int32_t fencefd;
    mVout->setOsdChannels(1);
    if (mVout->pageFlip(fencefd) < 0) {
        MESON_LOGE("VdinPostProcessor, page flip failed.");
        return -EIO;
    }
    close(fencefd);

    return 0;
}

int32_t VdinPostProcessor::startVdin() {
    int w = 0, h = 0, format = 0;
    Vdin::getInstance().getStreamInfo(w, h, format);
    MESON_ASSERT(format == HAL_PIXEL_FORMAT_RGB_888,
        "Only support HAL_PIXEL_FORMAT_RGB_888");
   Vdin::getInstance().setStreamInfo(format, VDIN_BUF_CNT);

    mVdinFbs.clear();
    for (int i = 0;i < VDIN_BUF_CNT;i ++) {
        buffer_handle_t hnd = gralloc_alloc_dma_buf(w, h, format, true, false);
        MESON_ASSERT(hnd != NULL && am_gralloc_get_buffer_fd(hnd) >= 0,
            "alloc vdin buf failed.");
        std::shared_ptr<DrmFramebuffer> buf = std::make_shared<DrmFramebuffer>(hnd, -1);
        /*queue buf before start streaming.*/
        Vdin::getInstance().queueBuffer(buf, i);
        mVdinFbs.push_back(buf);
        mVdinHnds.push_back(hnd);
    }

    return Vdin::getInstance().start();
}

int32_t VdinPostProcessor::stopVdin() {
    Vdin::getInstance().stop();
    mVdinFbs.clear();
    for (auto it = mVdinHnds.begin(); it != mVdinHnds.end(); it ++) {
        gralloc_free_dma_buf((native_handle_t * )*it);
    }
    mVdinHnds.clear();

    while (!mVdinQueue.empty()) {
        mVdinQueue.pop();
    }
    return 0;
}

int32_t VdinPostProcessor::setFbProcessor(
    std::shared_ptr<FbProcessor> & processor) {
    mFbProcessor = processor;
    return 0;
}

int32_t VdinPostProcessor::start() {
    std::lock_guard<std::mutex> lock(mMutex);

    if (mStat == PROCESSOR_START)
        return 0;

    MESON_LOGD("VdinPostProcessor::start");
    if (mFbProcessor.get() == NULL ||
        mDisplayPlane.get() == NULL) {
        return -EINVAL;
    }

    if (mVoutHnds.size() == 0) {
        for (int i = 0;i < VOUT_BUF_CNT;i ++) {
            buffer_handle_t hnd = gralloc_alloc_dma_buf(
                mVoutW, mVoutH, HAL_PIXEL_FORMAT_RGB_888, true, false);
            mVoutHnds.push_back(hnd);

            auto buf = std::make_shared<DrmFramebuffer>(hnd, -1);
            mVoutQueue.push(buf);
        }
    }

    mStat = PROCESSOR_START;
    mProcessMode = PROCESS_IDLE;

    /*process thread will start later when hwc2display really have output.*/
    return 0;
}

int32_t VdinPostProcessor::stop() {
    if (mStat == PROCESSOR_STOP)
        return 0;

    std::unique_lock<std::mutex> cmdLock(mMutex);
    mExitThread = true;
    cmdLock.unlock();
    mCmdCond.notify_one();

    pthread_join(mThread, NULL);
    mStat = PROCESSOR_STOP;

    while (!mCmdQ.empty()) {
        mCmdQ.pop();
    }

    /*clear queues.*/
    while (!mVoutQueue.empty()) {
        mVoutQueue.pop();
    }

    for (auto it = mVoutHnds.begin(); it != mVoutHnds.end(); it ++) {
        gralloc_free_dma_buf((native_handle_t * )*it);
    }
    mVoutHnds.clear();

    return 0;
}

bool VdinPostProcessor::running() {
    return mStat == PROCESSOR_START;
}

int32_t VdinPostProcessor::present(int flags, int32_t fence) {
    if (fence >= 0)
        close(fence);

    if (mStat == PROCESSOR_STOP)
        return 0;

    std::unique_lock<std::mutex> cmdLock(mMutex);

#ifdef VDIN_POSTPROCESS_DEBUG
    MESON_LOGD("VdinPostProcessor::present %d @ %d", flags, mStat);
#endif

    /*First present comes, we need start processor thread.*/
    if (!(flags & PRESENT_BLANK)) {
        if (mExitThread == true && mStat == PROCESSOR_START) {
            int ret = pthread_create(&mThread, NULL, VdinPostProcessor::threadMain, (void *)this);
            MESON_ASSERT(ret == 0, "failed to start VdinFlinger main thread: %s", strerror(ret));
            mExitThread = false;
        }
    }

    mCmdQ.push(flags);

    cmdLock.unlock();
    mCmdCond.notify_one();
    return 0;
}

void * VdinPostProcessor::threadMain(void * data) {
    MESON_ASSERT(data, "vdin data should not be NULL.");
    VdinPostProcessor * pThis = (VdinPostProcessor *) data;

    if (pThis->mFbProcessor)
        pThis->mFbProcessor->setup();

    pThis->startVdin();
    while (!pThis->mExitThread) {
        pThis->process();
    }
    pThis->stopVdin();

    /*blank vout.*/
    pThis->postVout(NULL);

    if (pThis->mFbProcessor)
        pThis->mFbProcessor->teardown();

    pthread_exit(0);
    return NULL;
}

int32_t VdinPostProcessor::process() {
    std::unique_lock<std::mutex> lock(mMutex);
    static int capCnt = 0;
    if (mCmdQ.size() > 0) {
        int cmd = mCmdQ.front();
        mCmdQ.pop();
        if (cmd & PRESENT_SIDEBAND) {
            mProcessMode = PROCESS_ALWAYS;
        } else if ((cmd & PRESENT_BLANK) || (cmd == 0)) {
            mProcessMode = PROCESS_ONCE;
            capCnt = VDIN_CAP_CNT;
        }
    }
    if (mProcessMode == PROCESS_IDLE) {
        //wait new present cmd.
        MESON_LOGD("In idle, waiting new cmd.");
        mCmdCond.wait(lock);
        return 0;
    }
#ifdef VDIN_CAP_ALWAYS_ON
    mProcessMode = PROCESS_ALWAYS;
#endif
    lock.unlock();

    if (mProcessMode == PROCESS_ALWAYS) {
        /*always set capCnt, so always queue buf to vdin..*/
        capCnt = VDIN_CAP_CNT;
    }

#ifdef VDIN_POSTPROCESS_DEBUG
    MESON_LOGD("VdinPostProcessor processMode(%d) capCnt(%d) vdinkeep (%d)",
        mProcessMode, capCnt, mVdinQueue.size());
#endif

    /*all vdin bufs consumed, go to idle.*/
    if (capCnt <= 0 && mVdinQueue.size() == VDIN_CAP_CNT) {
        if (mProcessMode == PROCESS_ONCE) {
            MESON_LOGD("PROCESS_ONCE -> PROCESS_IDLE.");
            mProcessMode = PROCESS_IDLE;
        }
    } else {
        std::shared_ptr<DrmFramebuffer> nullfb;
        std::shared_ptr<DrmFramebuffer> infb;
        std::shared_ptr<DrmFramebuffer> outfb;
        int vdinIdx = -1;

        /*Release buf to vdin here, for we may keeped all the buf..*/
        while (capCnt > 0 && mVdinQueue.size() > 0) {
            vdinIdx = mVdinQueue.front();
            Vdin::getInstance().queueBuffer(nullfb, vdinIdx);
#ifdef VDIN_POSTPROCESS_DEBUG
            MESON_LOGE("Vdin::queue %d", vdinIdx);
#endif
            mVdinQueue.pop();
            capCnt --;
        }

        if (Vdin::getInstance().dequeueBuffer(vdinIdx) == 0) {
            MESON_ASSERT(vdinIdx >= 0 && !mVoutQueue.empty(), "idx always >= 0.");
#ifdef VDIN_POSTPROCESS_DEBUG
            MESON_LOGE("Vdin::dequeue %d", vdinIdx);
#endif
            infb = mVdinFbs[vdinIdx];

            /*get ouput buf, and wait it ready.*/
            outfb = mVoutQueue.front();
            int releaseFence = outfb->getReleaseFence();
            if (releaseFence >= 0) {
                DrmFence fence(releaseFence);
                fence.wait(3000);
                outfb->clearReleaseFence();
            }
            mVoutQueue.pop();

            /*Should be a block process.*/
            mFbProcessor->process(infb, outfb);

            /*post outbuf to vout, and return to vout queue.*/
            postVout(outfb);
            mVoutQueue.push(outfb);

            /*hold the buffer now.*/
            mVdinQueue.push(vdinIdx);
        } else {
            MESON_LOGE("Vdin dequeue failed, still need cap %d", capCnt);
        }
    }

    return 0;
}
