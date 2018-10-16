/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

 #ifndef LEGACY_VIDEO_PLANE_H
#define LEGACY_VIDEO_PLANE_H

#include <HwDisplayPlane.h>

class LegacyVideoPlane : public HwDisplayPlane {
public:
    LegacyVideoPlane(int32_t drvFd, uint32_t id);
    ~LegacyVideoPlane();

    const char * getName();
    uint32_t getPlaneType();
    uint32_t getCapabilities();
    int32_t getFixedZorder();
    uint32_t getPossibleCrtcs();
    bool isFbSupport(std::shared_ptr<DrmFramebuffer> & fb);

    int32_t updateZoomInfo(display_zoom_info_t zoomInfo);
    int32_t setPlane(std::shared_ptr<DrmFramebuffer> & fb, uint32_t zorder);
    int32_t blank(int blankOp);

    void dump(String8 & dumpstr);

protected:
    int32_t getOmxKeepLastFrame(unsigned int & keep);
    bool shouldUpdateAxis(std::shared_ptr<DrmFramebuffer> & fb);
    int32_t setScale(drm_rect_t disPosition, char * axis);

    int32_t getMute(bool& status);
    int32_t setMute(bool status);

    int32_t getVideodisableStatus(int & status);
    int32_t setVideodisableStatus(int status);

protected:
    char mName[64];
    unsigned int mOmxKeepLastFrame;

    bool mPlaneMute;

    int32_t mBackupTransform;
    drm_rect_t mBackupDisplayFrame;
    int mZoomPercent;
    int mWindowW, mWindowH;
    bool mNeedUpdateAxis;
};

 #endif/*LEGACY_VIDEO_PLANE_H*/