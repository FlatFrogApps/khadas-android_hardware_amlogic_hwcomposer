/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

 #ifndef OSD_PLANE_H
#define OSD_PLANE_H

#include <HwDisplayPlane.h>
#include <MesonLog.h>
#include <misc.h>
#include "AmFramebuffer.h"

class OsdPlane : public HwDisplayPlane {
public:
    OsdPlane(int32_t drvFd, uint32_t id);
    ~OsdPlane();

    const char * getName();
    uint32_t getPlaneType();
    uint32_t getCapabilities();
    int32_t getFixedZorder();

    int32_t updateZoomInfo(display_zoom_info_t zoomInfo __unused) {return 0;}
    int32_t setPlane(std::shared_ptr<DrmFramebuffer> & fb);
    int32_t blank(int blankOp);

    void dump(String8 & dumpstr);

protected:
    int32_t getProperties();

private:
    bool mFirstPresent;
    bool mBlank;

    osd_plane_info_t mPlaneInfo;
    std::shared_ptr<DrmFramebuffer> mDrmFb;

    char mName[64];
};


 #endif/*OSD_PLANE_H*/
