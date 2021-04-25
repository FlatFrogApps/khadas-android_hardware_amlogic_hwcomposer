/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _CONNECTORPANEL_H
#define _CONNECTORPANEL_H
#include "HwDisplayConnectorFbdev.h"

class ConnectorPanel :public HwDisplayConnectorFbdev {
public:
    ConnectorPanel(int32_t drvFd, uint32_t id);
    virtual ~ConnectorPanel();

    virtual int32_t update();

    virtual const char * getName();
    virtual drm_connector_type_t getType();
    virtual bool isConnected();
    virtual bool isSecure();

    virtual void updateHdrCaps() {};
    virtual void getHdrCapabilities(drm_hdr_capabilities * caps);
    virtual void dump(String8 & dumpstr);

protected:
    int32_t loadDisplayModes();
    int32_t parseHdrCapabilities();
    int32_t parseLcdInfo();

    enum {
        LCD_WIDTH = 0,
        LCD_HEIGHT,
        LCD_SYNC_DURATION_NUM,
        LCD_SYNC_DURATION_DEN,
        LCD_SCREEN_REAL_WIDTH,
        LCD_SCREEN_REAL_HEIGHT,
        LCD_VALUE_MAX
    };
    bool mTabletMode;
    uint32_t mLcdValues[LCD_VALUE_MAX];

    char mName[64];
    std::string mModeName;

private:
    drm_hdr_capabilities mHdrCapabilities;
};

#endif
