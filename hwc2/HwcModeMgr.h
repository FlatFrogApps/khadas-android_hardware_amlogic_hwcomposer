/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef IHWC_MODE_MGR_H
#define IHWC_MODE_MGR_H

#include <hardware/hwcomposer2.h>
#include <BasicTypes.h>
#include <HwDisplayConnector.h>
#include <HwDisplayCrtc.h>

/*
 *For different connectors, we need different manage strategy.
 *This class defines basic interface to manage display modes.
 */
class HwcModeMgr {
public:
    enum ModesPolicy {
        FIXED_SIZE_POLICY = 0,
        FULL_ACTIVE_POLICY
    };

public:
    HwcModeMgr() {}
    virtual ~HwcModeMgr() {}

    virtual ModesPolicy getPolicyType() = 0;
    virtual const char * getName() = 0;

    virtual void setDisplayResources(
        std::shared_ptr<HwDisplayCrtc> & crtc,
        std::shared_ptr<HwDisplayConnector> & connector) = 0;
    virtual int32_t updateDisplayResources() = 0;

    virtual hwc2_error_t  getDisplayConfigs(
        uint32_t* outNumConfigs, hwc2_config_t* outConfigs) = 0;
    virtual hwc2_error_t  getDisplayAttribute(
        hwc2_config_t config, int32_t attribute, int32_t* outValue) = 0;
    virtual hwc2_error_t getActiveConfig(hwc2_config_t* outConfig) = 0;
    virtual hwc2_error_t setActiveConfig(hwc2_config_t config) = 0;

    virtual void dump(String8 & dumpstr) = 0;
};

std::shared_ptr<HwcModeMgr> createModeMgr(HwcModeMgr::ModesPolicy policy);

#endif/*IHWC_MODE_MGR_H*/