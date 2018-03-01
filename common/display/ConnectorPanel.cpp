/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "ConnectorPanel.h"
#include "AmVinfo.h"
#include <MesonLog.h>

ConnectorPanel::ConnectorPanel(int32_t drvFd, uint32_t id)
    : HwDisplayConnector(drvFd, id) {

}

ConnectorPanel::~ConnectorPanel() {
}

drm_connector_type_t ConnectorPanel::getType() {
    return DRM_MODE_CONNECTOR_PANEL;
}

bool ConnectorPanel::isRemovable() {
    return false;
}

bool ConnectorPanel::isConnected(){
    return true;
}

bool ConnectorPanel::isSecure(){
    return true;
}

void ConnectorPanel::getHdrCapabilities(drm_hdr_capabilities * caps) {
    if (caps) {
        memset(caps, 0, sizeof(drm_hdr_capabilities));
    }
}

void ConnectorPanel:: dump(String8& dumpstr) {
    dumpstr.appendFormat("Connector (Panel,  %d)\n",1);
    dumpstr.append("   CONFIG   |   VSYNC_PERIOD   |   WIDTH   |   HEIGHT   |"
        "   DPI_X   |   DPI_Y   \n");
    dumpstr.append("------------+------------------+-----------+------------+"
        "-----------+-----------\n");

    std::map<uint32_t, drm_mode_info_t>::iterator it = mDisplayModes.begin();
    for ( ; it != mDisplayModes.end(); ++it) {
        dumpstr.appendFormat(" %2d     |      %.3f      |   %5d   |   %5d    |"
            "    %3d    |    %3d    \n",
                 it->first,
                 it->second.refreshRate,
                 it->second.pixelW,
                 it->second.pixelH,
                 it->second.dpiX,
                 it->second.dpiY);
    }
}

