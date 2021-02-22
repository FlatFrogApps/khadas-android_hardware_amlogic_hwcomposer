/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <MesonLog.h>
#include <HwcVsync.h>
#include <HwDisplayCrtc.h>
#include <DebugHelper.h>
#include <inttypes.h>

#define SF_VSYNC_DFT_PERIOD 60
#define VT_OFFSET_TIME 5000000

HwcVsync::HwcVsync() {
    mSoftVsync = true;
    mEnabled = false;
    mVTEnabled = false;
    mMixVsync = false;
    mMixRebase = false;
    mPreTimeStamp = 0;
    mReqPeriod = 0;
    mVsyncTime = 0;
    mExit = false;
    mObserver = NULL;

    int ret;
    ret = pthread_create(&hw_vsync_thread, NULL, vsyncThread, this);
    if (ret) {
        MESON_LOGE("failed to start vsync thread: %s", strerror(ret));
        return;
    }
}

HwcVsync::~HwcVsync() {
    std::unique_lock<std::mutex> stateLock(mStatLock);
    mExit = true;
    stateLock.unlock();
    mStateCondition.notify_all();
    pthread_join(hw_vsync_thread, NULL);
}

int32_t HwcVsync::setObserver(HwcVsyncObserver * observer) {
    mObserver = observer;
    return 0;
}

int32_t HwcVsync::setSoftwareMode() {
    std::unique_lock<std::mutex> stateLock(mStatLock);
    mMixVsync = false;
    mSoftVsync = true;
    mCrtc.reset();
    stateLock.unlock();
    mStateCondition.notify_all();
    return 0;
}

int32_t HwcVsync::setMixMode() {
    std::unique_lock<std::mutex> stateLock(mStatLock);
    mMixVsync = true;
    mMixRebase = true;
    mSoftVsync = false;
    stateLock.unlock();
    mStateCondition.notify_all();
    return 0;
}

int32_t HwcVsync::setHwMode(std::shared_ptr<HwDisplayCrtc> & crtc) {
    std::unique_lock<std::mutex> stateLock(mStatLock);
    mCrtc = crtc;
    mSoftVsync = false;
    mMixVsync = false;
    stateLock.unlock();
    mStateCondition.notify_all();
    return 0;
}

int32_t HwcVsync::setPeriod(nsecs_t period) {
    if (mReqPeriod != period) {
        MESON_LOGD("Update period %" PRIx64 "->%" PRIx64 "", period, mReqPeriod);
        mReqPeriod = period;
    }
    return 0;
}

int32_t HwcVsync::setEnabled(bool enabled) {
    std::unique_lock<std::mutex> stateLock(mStatLock);
    mEnabled = enabled;
    stateLock.unlock();
    mStateCondition.notify_all();
    return 0;
}

int32_t HwcVsync::setVideoTunnelEnabled(bool enabled) {
    std::unique_lock<std::mutex> stateLock(mStatLock);
    mVTEnabled = enabled;
    stateLock.unlock();
    mStateCondition.notify_all();
    return 0;
}

void * HwcVsync::vsyncThread(void * data) {
    HwcVsync* pThis = (HwcVsync*)data;
    MESON_LOGV("HwDisplayVsync: vsyncThread start - (%p).", pThis);

    // set HwcVsync thread to SCHED_FIFO to minimize jitter
    struct sched_param param = {0};
    param.sched_priority = 2;
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        MESON_LOGE("Couldn't set SCHED_FIFO for videotunnelthread");
    }

    while (true) {
        {
            std::unique_lock<std::mutex> stateLock(pThis->mStatLock);
            while (!pThis->mEnabled && !pThis->mVTEnabled) {
                pThis->mStateCondition.wait(stateLock);
                if (pThis->mExit) {
                    MESON_LOGD("exit vsync loop");
                    pthread_exit(0);
                    return NULL;
                }
            }
        }

        nsecs_t timestamp;
        int32_t ret;
        if (pThis->mSoftVsync) {
            ret = pThis->waitSoftwareVsync(timestamp);
        } else if (pThis->mMixVsync) {
            ret = pThis->waitMixVsync(timestamp);
        } else {
            ret = pThis->waitHwVsync(timestamp);
        }

        nsecs_t period;
        if (pThis->mPreTimeStamp != 0) {
            period = timestamp - pThis->mPreTimeStamp;
        } else {
            period = pThis->mReqPeriod;
        }
        pThis->mPreTimeStamp = timestamp;

        if (DebugHelper::getInstance().enableVsyncDetail()) {
            if (pThis->mPreTimeStamp != 0)
                MESON_LOGD("wait for vsync success, peroid: %" PRId64 ", timestmap (%" PRId64 ")",
                        period, timestamp);
        }

        if (ret == 0 && pThis->mObserver) {
            if (pThis->mEnabled) {
                pThis->mObserver->onVsync(timestamp, period);
            }
            if (pThis->mVTEnabled) {
                pThis->mObserver->onVTVsync(timestamp, period);
            }
        } else {
            if (ret != 0)
                MESON_LOGE("wait for hw vsync error:%d", ret);
        }
    }
    return NULL;
}

int32_t HwcVsync::waitHwVsync(nsecs_t& vsync_timestamp) {
    int32_t ret = mCrtc->waitVBlank(mVsyncTime);
    vsync_timestamp = mVsyncTime;
    return ret;
}

int32_t HwcVsync::waitSoftwareVsync(nsecs_t& vsync_timestamp) {
    static nsecs_t vsync_time = 0;
    static nsecs_t old_vsync_period = 0;
    nsecs_t now = systemTime(CLOCK_MONOTONIC);
    mReqPeriod = (mReqPeriod == 0) ? (1e9/SF_VSYNC_DFT_PERIOD) : mReqPeriod;

    //cal the last vsync time with old period
    if (mReqPeriod != old_vsync_period) {
        if (old_vsync_period > 0) {
            vsync_time = vsync_time +
                    ((now - vsync_time) / old_vsync_period) * old_vsync_period;
        }
        old_vsync_period = mReqPeriod;
    }

    //set to next vsync time
    vsync_time += mReqPeriod;

    // we missed, find where the next vsync should be
    if (vsync_time - now < 0) {
        vsync_time = now + (mReqPeriod -
                 ((now - vsync_time) % mReqPeriod));
    }

    struct timespec spec;
    spec.tv_sec  = vsync_time / 1000000000;
    spec.tv_nsec = vsync_time % 1000000000;

    int err;
    do {
        err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
    } while (err<0 && errno == EINTR);
    vsync_timestamp = vsync_time;

    return err;
}

int32_t HwcVsync::waitMixVsync(nsecs_t& vsync_timestamp) {
    static nsecs_t cur_vsync_period = 0;
    if (cur_vsync_period != mReqPeriod || mMixRebase) {
        MESON_LOGD("[%s] waitVBlank to get hw vsync timestamp", __func__);
        mCrtc->waitVBlank(mVsyncTime);
        mVsyncTime += VT_OFFSET_TIME;
        cur_vsync_period = mReqPeriod;
        mMixRebase = false;
    } else {
        nsecs_t now = systemTime(CLOCK_MONOTONIC);
        mVsyncTime = mVsyncTime + cur_vsync_period +
            (now - mVsyncTime ) /cur_vsync_period * cur_vsync_period;

        struct timespec spec;
        spec.tv_sec  = mVsyncTime / 1000000000;
        spec.tv_nsec = mVsyncTime % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err<0 && errno == EINTR);
    }

    vsync_timestamp = mVsyncTime;
    return 0;
}

void HwcVsync::dump(String8 &dumpstr) {
    dumpstr.appendFormat("HwcVsync mode(%s) period(%" PRId64 ") \n",
        mSoftVsync ? "soft":"hw", mReqPeriod);
    dumpstr.appendFormat("    mEnabled:%d, mExit:%d\n", mEnabled, mExit);

    if (mObserver)
        dumpstr.appendFormat("    mObserver:%p\n", mObserver);
}
