/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <fcntl.h>
#include <MesonLog.h>
#include <misc.h>
#include <systemcontrol.h>
#include <inttypes.h>

#include "AmVinfo.h"
#include "ConnectorHdmi.h"
#include <sstream>

enum {
    REFRESH_24kHZ = 24,
    REFRESH_30kHZ = 30,
    REFRESH_60kHZ = 60,
    REFRESH_120kHZ = 120,
    REFRESH_240kHZ = 240
};

static const std::vector<std::string> CONTENT_TYPES = {
    "0",
    "graphics",
    "photo",
    "cinema",
    "game",
};

#define HDMI_FRAC_RATE_POLICY "/sys/class/amhdmitx/amhdmitx0/frac_rate_policy"
#define HDMI_TX_HPD_STATE   "/sys/class/amhdmitx/amhdmitx0/hpd_state"
#define HDMI_TX_ALLM_MODE   "/sys/class/amhdmitx/amhdmitx0/allm_mode"
#define HDMI_TX_CONTENT_TYPE_CAP  "/sys/class/amhdmitx/amhdmitx0/contenttype_cap"
#define HDMI_TX_CONTENT_TYPE  "/sys/class/amhdmitx/amhdmitx0/contenttype_mode"

ConnectorHdmi::ConnectorHdmi(int32_t drvFd, uint32_t id)
    :   HwDisplayConnectorFbdev(drvFd, id) {
    mIsEDIDValid = false;
    mConnected = false;
    mSecure = false;
    mFracMode = HWC_HDMI_FRAC_MODE;
    mCurrentHdrType = "sdr";
    snprintf(mName, 64, "HDMI-%d", id);
    MESON_LOGD("Connector hdmi (%s) frac mode (%d) created.", mName, mFracMode);
}

ConnectorHdmi::~ConnectorHdmi() {
}

int32_t ConnectorHdmi::update() {
    MESON_LOG_FUN_ENTER();
    mConnected = checkConnectState();
    if (mConnected) {
        loadPhysicalSize();
        loadDisplayModes();
        loadSupportedContentTypes();
        parseHdrCapabilities();
        parseEDID();
        get_hdmitx_hdcp_state(mSecure);
    }

    MESON_LOGE("ConnectorHdmi::update to %d", mConnected);

    MESON_LOG_FUN_LEAVE();
    return 0;
}

const char * ConnectorHdmi::getName() {
    return mName;
}

drm_connector_type_t ConnectorHdmi::getType() {
    return DRM_MODE_CONNECTOR_HDMIA;
}

bool ConnectorHdmi::isConnected() {
    return mConnected;
}

bool ConnectorHdmi::isSecure() {
    return mSecure;
}

bool ConnectorHdmi::checkConnectState() {
    return sysfs_get_int(HDMI_TX_HPD_STATE, 0) == 1 ? true : false;
}

int32_t ConnectorHdmi::loadDisplayModes() {
    std::vector<std::string> supportDispModes;
    std::string::size_type pos;
    mFracRefreshRates.clear();
    mDisplayModes.clear();

    if (get_hdmitx_mode_list(supportDispModes) < 0) {
        MESON_LOGE("SupportDispModeList null!!!");
        return -ENOENT;
    }

    for (size_t i = 0; i < supportDispModes.size(); i++) {
        if (!supportDispModes[i].empty()) {
            pos = supportDispModes[i].find('*');
            if (pos != std::string::npos) {
                supportDispModes[i].erase(pos, 1);
                //MESON_LOGE("modify support display mode:%s", supportDispModes[i].c_str());
            }
            addDisplayMode(supportDispModes[i]);
        }
    }

    return 0;
}

int32_t ConnectorHdmi::addDisplayMode(std::string& mode) {
    vmode_e vmode = vmode_name_to_mode(mode.c_str());
    const struct vinfo_s* vinfo = get_tv_info(vmode);
    if (vmode == VMODE_MAX || vinfo == NULL) {
        MESON_LOGE("addSupportedConfig meet error mode (%s, %d)", mode.c_str(), vmode);
        return -ENOENT;
    }

    uint32_t dpiX  = DEFAULT_DISPLAY_DPI, dpiY = DEFAULT_DISPLAY_DPI;
    if (mPhyWidth > 16 && mPhyHeight > 9) {
        dpiX = (vinfo->width  * 25.4f) / mPhyWidth;
        dpiY = (vinfo->height  * 25.4f) / mPhyHeight;
        MESON_LOGI("add display mode real dpi (%d, %d)", dpiX, dpiY);
    }

    drm_mode_info_t modeInfo = {
        "",
        dpiX,
        dpiY,
        vinfo->width,
        vinfo->height,
        (float)vinfo->sync_duration_num/vinfo->sync_duration_den,
        0};
    strcpy(modeInfo.name, mode.c_str());

    bool bFractionMode = false, bNonFractionMode = false;
    if (mFracMode == MODE_ALL || mFracMode == MODE_FRACTION) {
        bFractionMode = true;
    }

    // add frac refresh rate config, like 23.976hz, 29.97hz...
    if (modeInfo.refreshRate == REFRESH_24kHZ
        || modeInfo.refreshRate == REFRESH_30kHZ
        || modeInfo.refreshRate == REFRESH_60kHZ
        || modeInfo.refreshRate == REFRESH_120kHZ
        || modeInfo.refreshRate == REFRESH_240kHZ) {
        if (bFractionMode) {
            drm_mode_info_t fracMode = modeInfo;
            fracMode.refreshRate = (modeInfo.refreshRate * 1000) / (float)1001;
            //currently kernel cannot support do seamlessly in some conditions, thus every mode is a group
            //will be modified once kernel support
            fracMode.groupId = mDisplayModes.size();
            mDisplayModes.emplace(mDisplayModes.size(), fracMode);
            MESON_LOGI("add fraction display mode (%s)", fracMode.name);
            mFracRefreshRates.push_back(fracMode.refreshRate);
        }
    } else {
        /*for non fraction display mode, we also add it in MODE_FRACTION*/
        bNonFractionMode = true;
    }

    if (mFracMode == MODE_ALL || mFracMode == MODE_NON_FRACTION) {
        bNonFractionMode = true;
    }

    if (bNonFractionMode) {
        // add normal refresh rate config, like 24hz, 30hz...
        //currently kernel cannot support do seamlessly in some conditions, thus every mode is a group
        //will be modified once kernel support
        modeInfo.groupId = mDisplayModes.size();
        mDisplayModes.emplace(mDisplayModes.size(), modeInfo);
        MESON_LOGI("add non fraction display mode (%s)", mode.c_str());
    }

    return 0;
}

int32_t ConnectorHdmi::getModes(std::map<uint32_t, drm_mode_info_t> & modes) {
    return HwDisplayConnectorFbdev::getModes(modes);
}

int32_t ConnectorHdmi::setMode(drm_mode_info_t & mode) {
    if (MODE_ALL == mFracMode)
        return 0;

    /*update rate policy.*/
    for (auto it = mFracRefreshRates.begin(); it != mFracRefreshRates.end(); it ++) {
        if (*it == mode.refreshRate) {
            switchRatePolicy(true);
            return 0;
        }
    }

    switchRatePolicy(false);
    return 0;
}

int32_t ConnectorHdmi::getIdentificationData(std::vector<uint8_t>& idOut) {
    if (!mIsEDIDValid) {
        return -EAGAIN;
    }
    idOut = mEDID;
    return 0;
}

int32_t ConnectorHdmi::switchRatePolicy(bool fracRatePolicy) {
    if (fracRatePolicy) {
        if (!sysfs_set_string(HDMI_FRAC_RATE_POLICY, "1")) {
            MESON_LOGV("Switch to frac rate policy SUCCESS.");
        } else {
            MESON_LOGE("Switch to frac rate policy FAIL.");
            return -EFAULT;
        }
    } else {
        if (!sysfs_set_string(HDMI_FRAC_RATE_POLICY, "0")) {
            MESON_LOGV("Switch to normal rate policy SUCCESS.");
        } else {
            MESON_LOGE("Switch to normal rate policy FAIL.");
            return -EFAULT;
        }
    }
    return 0;
}

void ConnectorHdmi::getHdrCapabilities(drm_hdr_capabilities * caps) {
    if (caps) {
        *caps = mHdrCapabilities;
    }
}

int32_t ConnectorHdmi::loadSupportedContentTypes() {
    mSupportedContentTypes.clear();

    char supportedContentTypes[1024] = {};
    if (!sysfs_get_string(HDMI_TX_CONTENT_TYPE_CAP, supportedContentTypes, sizeof supportedContentTypes)) {
        MESON_LOGV("Read display content type SUCCESS.");
    } else {
        MESON_LOGE("Read display content type FAIL.");
        return -EFAULT;
    }

    for (int i = 0; i < CONTENT_TYPES.size(); i++) {
        if (strstr(supportedContentTypes, CONTENT_TYPES[i].c_str())) {
            MESON_LOGD("ConnectorHdmi reports support for content type %s", CONTENT_TYPES[i].c_str());
            mSupportedContentTypes.emplace_back(i);
        }
    }
    return 0;
}

int32_t ConnectorHdmi::setAutoLowLatencyMode(bool on) {
    if (on) {
        if (!sysfs_set_string(HDMI_TX_ALLM_MODE, "1")) {
            MESON_LOGV("setAutoLowLatencyMode on SUCCESS.");
        } else {
            MESON_LOGE("setAutoLowLatencyMode on FAIL.");
            return -EFAULT;
        }
    } else {
        if (!sysfs_set_string(HDMI_TX_ALLM_MODE, "0")) {
            MESON_LOGV("setAutoLowLatencyMode off SUCCESS.");
        } else {
            MESON_LOGE("setAutoLowLatencyMode off FAIL.");
            return -EFAULT;
        }
    }
    return 0;
}

int32_t ConnectorHdmi::setContentType(uint32_t contentType) {
    if (!sysfs_set_string(HDMI_TX_CONTENT_TYPE, CONTENT_TYPES[contentType].c_str())) {
        MESON_LOGV("setContentType SUCCESS.");
    } else {
        MESON_LOGE("setContentType %s FAIL.", CONTENT_TYPES[contentType].c_str());
        return -EFAULT;
    }

    return 0;
}

std::string ConnectorHdmi::getCurrentHdrType() {
    loadCurrentHdrType();
    return mCurrentHdrType;
}

void ConnectorHdmi::dump(String8 & dumpstr) {
    dumpstr.appendFormat("Connector (HDMI, %d x %d, %s, %s)\n",
        mPhyWidth, mPhyHeight, isSecure() ? "secure" : "unsecure",
        isConnected() ? "Connected" : "Removed");

    //dump display config.
    dumpstr.append("   CONFIG   |   VSYNC_PERIOD   |   WIDTH   |   HEIGHT   |"
        "   DPI_X   |   DPI_Y   \n");
    dumpstr.append("------------+------------------+-----------+------------+"
        "-----------+-----------\n");

    auto it = mDisplayModes.begin();
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

    // HDR info
    dumpstr.append("  HDR Capabilities:\n");
    dumpstr.appendFormat("    DolbyVision1=%d\n",
        mHdrCapabilities.DolbyVisionSupported ? 1 : 0);
    dumpstr.appendFormat("    HLG=%d\n",
        mHdrCapabilities.HLGSupported ?  1 : 0);
    dumpstr.appendFormat("    HDR10=%d, HDR10+=%d, "
        "maxLuminance=%d, avgLuminance=%d, minLuminance=%d\n",
        mHdrCapabilities.HDR10Supported ? 1 : 0,
        mHdrCapabilities.HDR10PlusSupported ? 1 : 0,
        mHdrCapabilities.maxLuminance,
        mHdrCapabilities.avgLuminance,
        mHdrCapabilities.minLuminance);
}

int32_t ConnectorHdmi::getLineValue(const char *lineStr, const char *magicStr) {
    int len = 0;
    char value[100] = {0};
    const char *pos = NULL;

    if ((NULL == lineStr) || (NULL == magicStr)) {
        MESON_LOGE("line string: %s, magic string: %s\n", lineStr, magicStr);
        return 0;
    }

    if (NULL != (pos = strstr(lineStr, magicStr))) {
        pos = pos + strlen(magicStr);
        const char* start = pos;
        while (*start != '\n' && (strlen(start) > 0))
            start++;

        len = start - pos;
        strncpy(value, pos, len);
        value[len] = '\0';
        return atoi(value);
    }

    return 0;
}

/*******************************************
* cat /sys/class/amhdmitx/amhdmitx0/hdr_cap
* HDR10Plus Supported: 1
* HDR Static Metadata:
*     Supported EOTF:
*         Traditional SDR: 1
*         Traditional HDR: 0
*         SMPTE ST 2084: 1
*         Hybrif Log-Gamma: 1
*     Supported SMD type1: 1
*     Luminance Data
*         Max: 0
*         Avg: 0
*         Min: 0
* cat /sys/class/amhdmitx/amhdmitx0/dv_cap
* DolbyVision1 RX support list:
*     2160p30hz: 1
*     global dimming
*     colorimetry
*     IEEEOUI: 0x00d046
*     DM Ver: 1
*******************************************/
int32_t ConnectorHdmi::parseHdrCapabilities() {
    // DolbyVision1
    const char *DV_PATH = "/sys/class/amhdmitx/amhdmitx0/dv_cap";
    // HDR
    const char *HDR_PATH = "/sys/class/amhdmitx/amhdmitx0/hdr_cap";

    char buf[1024+1] = {0};
    char* pos = buf;
    int fd, len;

    memset(&mHdrCapabilities, 0, sizeof(drm_hdr_capabilities));
    if ((fd = open(DV_PATH, O_RDONLY)) < 0) {
        MESON_LOGE("open %s fail.", DV_PATH);
        goto exit;
    }

    len = read(fd, buf, 1024);
    if (len < 0) {
        MESON_LOGE("read error: %s, %s\n", DV_PATH, strerror(errno));
        goto exit;
    }
    close(fd);

    if ((NULL != strstr(pos, "2160p30hz")) || (NULL != strstr(pos, "2160p60hz")))
        mHdrCapabilities.DolbyVisionSupported= true;
    // dobly version parse end

    memset(buf, 0, 1024);
    if ((fd = open(HDR_PATH, O_RDONLY)) < 0) {
        MESON_LOGE("open %s fail.", HDR_PATH);
        goto exit;
    }

    len = read(fd, buf, 1024);
    if (len < 0) {
        MESON_LOGE("read error: %s, %s\n", HDR_PATH, strerror(errno));
        goto exit;
    }

    if (pos != NULL) pos = strstr(pos, "HDR10Plus Supported: ");
    if ((NULL != pos) && ('1' == *(pos + strlen("HDR10Plus Supported: ")))) {
        mHdrCapabilities.HDR10PlusSupported = true;
    }

    if (pos != NULL) pos = strstr(pos, "SMPTE ST 2084: ");
    if ((NULL != pos) && ('1' == *(pos + strlen("SMPTE ST 2084: ")))) {
        mHdrCapabilities.HDR10Supported = true;

        mHdrCapabilities.maxLuminance = getLineValue(pos, "Max: ");
        mHdrCapabilities.avgLuminance = getLineValue(pos, "Avg: ");
        mHdrCapabilities.minLuminance = getLineValue(pos, "Min: ");
    }

    if (pos != NULL) pos = strstr(buf, "Hybrif Log-Gamma: ");
    if ((NULL != pos) && ('1' == *(pos + strlen("Hybrif Log-Gamma: ")))) {
        mHdrCapabilities.HLGSupported = true;
    }

    MESON_LOGD("dolby version:%d, hlg:%d, hdr10:%d, hdr10+:%d max:%d, avg:%d, min:%d\n",
        mHdrCapabilities.DolbyVisionSupported ? 1:0,
        mHdrCapabilities.HLGSupported ? 1:0,
        mHdrCapabilities.HDR10Supported ? 1:0,
        mHdrCapabilities.HDR10PlusSupported ? 1:0,
        mHdrCapabilities.maxLuminance,
        mHdrCapabilities.avgLuminance,
        mHdrCapabilities.minLuminance);

exit:
    close(fd);
    return NO_ERROR;
}

void ConnectorHdmi::parseEDID() {
    std::string edid;
    int i, pos, len, ret = 0;
    unsigned int xData;
    const char* HDMI_TX_RAWEEDID = "/sys/class/amhdmitx/amhdmitx0/rawedid";
    std::string str = "0123456789ABCDEF";
    std::stringstream ss;
    mIsEDIDValid = false;

    if (0 != read_sysfs(HDMI_TX_RAWEEDID, edid)) {
        MESON_LOGE("Get raw EDIE FAIL.");
        return;
    }
    if (edid.length() % 2 != 0) {
        MESON_LOGE("Can't to parse the EDIE:(len=%" PRIuFAST16 ")%s", edid.length(), edid.c_str());
        return;
    }
    len = edid.length() / 2;
    mEDID.resize(len);
    pos = 0;
    for (i = 0; i < len; i++) {
        ret = sscanf(edid.c_str() + pos, "%2x", &xData);
        if (ret != 1) {
            MESON_LOGE("Fail to parse EDIE:(len=%" PRIuFAST16 ")%s", edid.length(), edid.c_str());
            return;
        }
        pos = pos + 2;
        mEDID[i] = (uint8_t) xData;
    }
    for (auto i : mEDID) {
        ss << str[(unsigned char)i >> 4] << str[(unsigned char)i &0xf];
    }
    MESON_LOGV("Update EDID:%s", ss.str().c_str());
    mIsEDIDValid = true;
    return;
}

/*
* cat /sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status
* output is one of the following line
*
* SDR
* DolbyVision-Std
* DolbyVision-Lowlatency
* HDR10Plus-VSIF
* HDR10-GAMMA_ST2084
* HDR10-others
* HDR10-GAMMA_HLG
*/
bool ConnectorHdmi::loadCurrentHdrType() {
    const char *HDR_STATUS = "/sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status";

    if (read_sysfs(HDR_STATUS, mCurrentHdrType) != 0) {
        // default set to sdr
        mCurrentHdrType = "sdr";
        return false;
    }

    return true;
}
