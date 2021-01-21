/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "FixedSizeModeMgr.h"
#include <MesonLog.h>
#include <HwcConfig.h>
#include <hardware/hwcomposer2.h>


#define DEFUALT_DPI (159)
#define DEFAULT_REFRESH_RATE (60.0f)

static drm_mode_info_t fakeInitialMode = {
    .name              = "FAKE_INITIAL_MODE",
    .dpiX              = DEFUALT_DPI,
    .dpiY              = DEFUALT_DPI,
    .pixelW            = 1920,
    .pixelH            = 1080,
    .refreshRate       = DEFAULT_REFRESH_RATE,
    .groupId           = 0,
};

FixedSizeModeMgr::FixedSizeModeMgr() {
    mPreviousMode = fakeInitialMode;
}

FixedSizeModeMgr::~FixedSizeModeMgr() {

}

hwc_modes_policy_t FixedSizeModeMgr::getPolicyType() {
    return FIXED_SIZE_POLICY;
}

const char * FixedSizeModeMgr::getName() {
    return "FixedSizeMode";
}

void FixedSizeModeMgr::setFramebufferSize(uint32_t w, uint32_t h) {
    mCurMode.pixelW = mFbWidth = w;
    mCurMode.pixelH = mFbHeight = h;
}

void FixedSizeModeMgr::setDisplayResources(
    std::shared_ptr<HwDisplayCrtc> & crtc,
    std::shared_ptr<HwDisplayConnector> & connector) {
    mConnector = connector;
    mCrtc = crtc;
}

int32_t FixedSizeModeMgr::update() {
    bool useFakeMode = true;
    drm_mode_info_t realMode;

    if (mConnector->isConnected() && 0 == mCrtc->getMode(realMode)) {
        if (realMode.name[0] != 0) {
            mCurMode.refreshRate = realMode.refreshRate;
            mCurMode.dpiX = ((float)mCurMode.pixelW/ realMode.pixelW) * realMode.dpiX;
            mCurMode.dpiY = ((float)mCurMode.pixelH/ realMode.pixelH) * realMode.dpiY;
            mCurMode.groupId = realMode.groupId;
            strncpy(mCurMode.name, realMode.name , DRM_DISPLAY_MODE_LEN);
            MESON_LOGE("ModeMgr update to (%s)", mCurMode.name);
            useFakeMode = false;
            mPreviousMode = mCurMode;
        }
    }

    if (useFakeMode) {
        mCurMode = mPreviousMode;
        strncpy(mCurMode.name, "FAKE_PREVIOUS_MODE", DRM_DISPLAY_MODE_LEN);
    }

    return 0;
}

int32_t FixedSizeModeMgr::getDisplayMode(drm_mode_info_t & mode) {
    return mCrtc->getMode(mode);
}

int32_t  FixedSizeModeMgr::getDisplayConfigs(
    uint32_t * outNumConfigs, uint32_t * outConfigs) {
    *outNumConfigs = 1;
    if (outConfigs) {
        *outConfigs = 0;
    }
    return HWC2_ERROR_NONE;
}

int32_t  FixedSizeModeMgr::getDisplayAttribute(
    uint32_t config __unused, int32_t attribute, int32_t * outValue,
    int32_t caller __unused) {
    switch (attribute) {
        case HWC2_ATTRIBUTE_WIDTH:
            *outValue = mCurMode.pixelW;
            break;
        case HWC2_ATTRIBUTE_HEIGHT:
            *outValue = mCurMode.pixelH;
            break;
        case HWC2_ATTRIBUTE_VSYNC_PERIOD:
            if (HwcConfig::isHeadlessMode()) {
                *outValue = 1e9 / HwcConfig::headlessRefreshRate();
            } else {
                *outValue = 1e9 / mCurMode.refreshRate;
            }
            break;
        case HWC2_ATTRIBUTE_DPI_X:
            *outValue = mCurMode.dpiX;
            break;
        case HWC2_ATTRIBUTE_DPI_Y:
            *outValue = mCurMode.dpiY;
            break;
        case HWC2_ATTRIBUTE_CONFIG_GROUP:
            *outValue = mCurMode.groupId;
            break;
        default:
            MESON_LOGE("Unkown display attribute(%d)", attribute);
            break;
    }

    return HWC2_ERROR_NONE;
}

int32_t FixedSizeModeMgr::getActiveConfig(
    uint32_t * outConfig, int32_t caller __unused) {
    *outConfig = 0;
    return HWC2_ERROR_NONE;
}

int32_t FixedSizeModeMgr::setActiveConfig(
    uint32_t config) {
    if (config > 0) {
        MESON_LOGE("FixedSizeModeMgr dont support config (%d)", config);
    }
    return HWC2_ERROR_NONE;
}

void FixedSizeModeMgr::dump(String8 & dumpstr) {
    dumpstr.appendFormat("FixedSizeModeMgr:(%s)\n", mCurMode.name);
    dumpstr.append("---------------------------------------------------------"
        "----------------------------------------\n");
    dumpstr.append("|   CONFIG   |   VSYNC_PERIOD   |   WIDTH   |   HEIGHT   |"
        "   DPI_X   |   DPI_Y   |   GROUP_ID   |\n");
    dumpstr.append("+------------+------------------+-----------+------------+"
        "-----------+-----------+--------------+\n");
    dumpstr.appendFormat("|     %2d     |      %.3f      |   %5d   |   %5d    |"
        "    %3d    |    %3d    |    %3d    |\n",
         0,
         mCurMode.refreshRate,
         mCurMode.pixelW,
         mCurMode.pixelH,
         mCurMode.dpiX,
         mCurMode.dpiY,
         mCurMode.groupId);
    dumpstr.append("---------------------------------------------------------"
        "----------------------------------------\n");
}

