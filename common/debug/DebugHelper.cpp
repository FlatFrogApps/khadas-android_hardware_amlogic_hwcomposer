/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#include <unistd.h>
#include <getopt.h>

#include <misc.h>
#include <DebugHelper.h>
#include <MesonLog.h>

ANDROID_SINGLETON_STATIC_INSTANCE(DebugHelper)

#define DEBUG_HELPER_ENABLE_PROP "vendor.hwc.debug"
#define DEBUG_HELPER_COMMAND "vendor.hwc.debug.command"

#define COMMAND_CLEAR "--clear"
#define COMMAND_NOHWC "--nohwc"
#define COMMAND_DUMP_DETAIL "--detail"
#define COMMAND_ENABLE_VSYNC_DETAIL "--vsync-detail"
#define COMMAND_LOG_COMPOSITION_DETAIL "--composition-detail"
#define COMMAND_LOG_FPS "--fps"
#define COMMAND_SAVE_LAYER "--save-layer"
#define COMMAND_IN_FENCE "--infence"
#define COMMAND_OUT_FENCE "--outfence"
#define COMMAND_SHOW_LAYER "--show-layer"
#define COMMAND_HIDE_LAYER "--hide-layer"
#define COMMAND_SHOW_PLANE "--show-plane"
#define COMMAND_HIDE_PLANE "--hide-plane"
#define COMMAND_SHOW_PATTERN_ON_PLANE "--show-pattern"
#define COMMAND_HIDE_PATTERN_ON_PLANE "--hide-pattern"
#define COMMAND_MONITOR_DEVICE_COMPOSITION "--monitor-composition"
#define COMMAND_DEVICE_COMPOSITION_THRESHOLD "--device-layers-threshold"
#define COMMAND_SCALE_LIMIT "--scale-limit"
#define COMMAND_DRM_BLOCK_MODE "--drm-block"

#define MAX_DEBUG_COMMANDS (20)

#define PLANE_DBG_IDLE (1 << 0)
#define PLANE_DBG_PATTERN (1 << 1)


#define INT_PARAMERTER_TO_BOOL(param)  \
        atoi(param) > 0 ? true : false

#define CHECK_CMD_INT_PARAMETER() \
    if (i >= paramNum) { \
        MESON_LOGE("param number is not correct.\n");   \
        break;  \
    }

DebugHelper::DebugHelper() {
    clearPersistCmd();
    clearOnePassCmd();
}

DebugHelper::~DebugHelper() {
}

void DebugHelper::clearOnePassCmd() {
    mDumpUsage = false;
}

void DebugHelper::clearPersistCmd() {
    mDisableUiHwc = false;
    mDumpDetail = true;
    mEnableVsyncDetail = false;

    mLogFps = false;
    mLogCompositionDetail = false;
    mMonitorDeviceComposition = false;
    mDeviceCompositionThreshold = 4;
    mScaleLimit = 0;

    mDiscardInFence = false;
    mDiscardOutFence = false;

    mHideLayers.clear();
    mSaveLayers.clear();
    mDebugHideLayer = false;

    mPlanesDebugFlag.clear();
    mDebugPlane = false;
    mDrmBlockMode = false;
}

void DebugHelper::addHideLayer(int id) {
    bool bExist = false;
    std::vector<int>::iterator it;
    for (it = mHideLayers.begin(); it < mHideLayers.end(); it++) {
        if (*it == id) {
            bExist = true;
        }
    }

    if (!bExist) {
        mHideLayers.push_back(id);
    }
}

void DebugHelper::removeHideLayer(int id) {
    std::vector<int>::iterator it;
    for (it = mHideLayers.begin(); it < mHideLayers.end(); it++) {
        if (*it == id) {
            mHideLayers.erase(it);
            break;
        }
    }
}

void DebugHelper::addPlaneDebugFlag(int id, int flag) {
    auto it = mPlanesDebugFlag.find(id);
    if (it != mPlanesDebugFlag.end()) {
        int val = it->second;
        val |= flag;
        mPlanesDebugFlag.emplace(id, val);
    } else {
        mPlanesDebugFlag.emplace(id, flag);
    }

    mDebugPlane = true;
}

void DebugHelper::removePlaneDebugFlag(int id, int flag) {
    auto it = mPlanesDebugFlag.find(id);
    if (it != mPlanesDebugFlag.end()) {
        int val = it->second;
        val &= ~flag;
        if (val != 0)
            mPlanesDebugFlag.emplace(id, val);
        else
            mPlanesDebugFlag.erase(id);
    } else {
        MESON_LOGE("remove plane (%d-%x) fail", id, flag);
    }
}

void DebugHelper::resolveCmd() {
#ifdef HWC_RELEASE
    return;
#else
    clearOnePassCmd();
    mEnabled = sys_get_bool_prop(DEBUG_HELPER_ENABLE_PROP, true);

    if (mEnabled) {
        char debugCmd[128] = {0};
        if (sys_get_string_prop(DEBUG_HELPER_COMMAND, debugCmd) > 0 && debugCmd[0]) {
            mDumpUsage = false;

            int paramNum = 0;
            const char * delimit = " ";
            char * paramArray [MAX_DEBUG_COMMANDS + 1] = {NULL};
            char * param = strtok(debugCmd, delimit);

            while (param != NULL) {
                MESON_LOGE("param [%s]\n", param);
                paramArray[paramNum] = param;
                paramNum++;
                if (paramNum >= MAX_DEBUG_COMMANDS)
                    break;
                param = strtok(NULL, delimit);
            }

            for (int i = 0; i < paramNum; i++) {
                MESON_LOGE("Parse command [%s]", paramArray[i]);
                if (strcmp(paramArray[i], COMMAND_CLEAR) == 0) {
                    clearPersistCmd();
                    clearOnePassCmd();
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_NOHWC) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mDisableUiHwc = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_DUMP_DETAIL) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mDumpDetail = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_ENABLE_VSYNC_DETAIL) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mEnableVsyncDetail = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_LOG_FPS) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mLogFps = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_LOG_COMPOSITION_DETAIL) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mLogCompositionDetail = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_MONITOR_DEVICE_COMPOSITION) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mMonitorDeviceComposition = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_DEVICE_COMPOSITION_THRESHOLD) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mDeviceCompositionThreshold = atoi(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_SCALE_LIMIT) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mScaleLimit = atof(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_IN_FENCE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mDiscardInFence = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_OUT_FENCE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mDiscardOutFence = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_HIDE_LAYER) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int layerId = atoi(paramArray[i]);
                    if (layerId < 0) {
                        MESON_LOGE("Show invalid layer (%d)", layerId);
                    } else {
                        addHideLayer(layerId);
                        mDebugHideLayer = true;
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_SHOW_LAYER) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int layerId = atoi(paramArray[i]);
                    if (layerId < 0) {
                        MESON_LOGE("Show invalid layer (%d)", layerId);
                    } else {
                        removeHideLayer(layerId);
                        mDebugHideLayer = true;
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_HIDE_PLANE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int planeId = atoi(paramArray[i]);
                    if (planeId < 0) {
                        MESON_LOGE("Show invalid plane (%d)", planeId);
                    } else {
                        addPlaneDebugFlag(planeId, PLANE_DBG_IDLE);
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_SHOW_PLANE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int planeId = atoi(paramArray[i]);
                    if (planeId < 0) {
                        MESON_LOGE("Show invalid plane (%d)", planeId);
                    } else {
                        removePlaneDebugFlag(planeId, PLANE_DBG_IDLE);
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_SHOW_PATTERN_ON_PLANE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int planeId = atoi(paramArray[i]);
                    if (planeId < 0) {
                        MESON_LOGE("Show invalid plane (%d)", planeId);
                    } else {
                        addPlaneDebugFlag(planeId, PLANE_DBG_PATTERN);
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_HIDE_PATTERN_ON_PLANE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int planeId = atoi(paramArray[i]);
                    if (planeId < 0) {
                        MESON_LOGE("Show invalid plane (%d)", planeId);
                    } else {
                        removePlaneDebugFlag(planeId, PLANE_DBG_PATTERN);
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_SAVE_LAYER) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    int layerId = atoi(paramArray[i]);
                    if (layerId < 0) {
                        MESON_LOGE("Save layer (%d)", layerId);
                    } else {
                        mSaveLayers.push_back(layerId);
                    }
                    continue;
                }

                if (strcmp(paramArray[i], COMMAND_DRM_BLOCK_MODE) == 0) {
                    i++;
                    CHECK_CMD_INT_PARAMETER();
                    mDrmBlockMode = INT_PARAMERTER_TO_BOOL(paramArray[i]);
                    continue;
                }

            }

            /*Need permission to reset prop.*/
            sys_set_prop(DEBUG_HELPER_COMMAND, "");
        } else {
            mDumpUsage = true;
        }
    }
#endif
}

bool DebugHelper::isEnabled() {
#ifdef HWC_RELEASE
    return false;
#else
    return sys_get_bool_prop(DEBUG_HELPER_ENABLE_PROP, false);
#endif
}

void DebugHelper::removeDebugLayer(int id __unused) {
    #if 0/*useless now.*/
    /*remove hide layer*/
    removeHideLayer(id);

    /*remove save layer*/
    std::vector<int>::iterator it;
    for (it = mSaveLayers.begin(); it < mSaveLayers.end(); it++) {
        if (*it == id) {
            mSaveLayers.erase(it);
            break;
        }
    }
    #endif
}

void DebugHelper::dump(String8 & dumpstr) {
#ifdef HWC_RELEASE
    UNUSED(dumpstr);
#else
    if (!mEnabled)
        return;

    if (mDumpUsage) {
        static const char * usage =
            "Pass command string to prop " DEBUG_HELPER_COMMAND " to debug.\n"
            "Supported commands:\n"
            "\t " COMMAND_CLEAR ": clear all debug flags.\n"
            "\t " COMMAND_NOHWC " 0|1:  choose osd/UI hwcomposer.\n"
            "\t " COMMAND_DUMP_DETAIL " 0|1: enable/dislabe dump detail internal info.\n"
            "\t " COMMAND_IN_FENCE " 0 | 1: pass in fence to display, or handle it in hwc.\n"
            "\t " COMMAND_OUT_FENCE " 0 | 1: return display out fence, or handle it in hwc.\n"
            "\t " COMMAND_ENABLE_VSYNC_DETAIL " 0 | 1: enable/disable hwcVsync thread detail log.\n"
            "\t " COMMAND_LOG_COMPOSITION_DETAIL " 0|1: enable/disable composition detail info.\n"
            "\t " COMMAND_HIDE_LAYER "/" COMMAND_SHOW_LAYER " [layerId]: hide/unhide specific layers by zorder. \n"
            "\t " COMMAND_HIDE_PLANE "/" COMMAND_SHOW_PLANE " [planeId]: hide/unhide specific plane by plane id. \n"
            "\t " COMMAND_SHOW_PATTERN_ON_PLANE "/" COMMAND_HIDE_PATTERN_ON_PLANE " [planeId]: set/unset test pattern on plane id. \n"
            "\t " COMMAND_LOG_FPS " 0|1: start/stop log fps.\n"
            "\t " COMMAND_SAVE_LAYER " [layerId]: save specific layer's raw data by layer id. \n"
            "\t " COMMAND_MONITOR_DEVICE_COMPOSITION " 0|1: monitor non device composition. \n"
            "\t " COMMAND_SCALE_LIMIT " [float]: vpu scale limit factor. \n"
            "\t " COMMAND_DRM_BLOCK_MODE " 0|1: enable/dislabe drm-block commit mode.\n";

        dumpstr.append("\nMesonHwc debug helper:\n");
        dumpstr.append(usage);
        dumpstr.append("\n");
    } else {
        dumpstr.append("Debug Command:\n");
        dumpstr.appendFormat(COMMAND_NOHWC " (%d)\n", mDisableUiHwc);
        dumpstr.appendFormat(COMMAND_DUMP_DETAIL " (%d)\n", mDumpDetail);
        dumpstr.appendFormat(COMMAND_ENABLE_VSYNC_DETAIL " (%d)\n", mEnableVsyncDetail);
        dumpstr.appendFormat(COMMAND_IN_FENCE " (%d)\n", mDiscardInFence);
        dumpstr.appendFormat(COMMAND_OUT_FENCE " (%d)\n", mDiscardOutFence);
        dumpstr.appendFormat(COMMAND_LOG_COMPOSITION_DETAIL " (%d)\n", mLogCompositionDetail);
        dumpstr.appendFormat(COMMAND_LOG_FPS " (%d)\n", mLogFps);
        dumpstr.appendFormat(COMMAND_MONITOR_DEVICE_COMPOSITION " (%d)\n", mMonitorDeviceComposition);
        dumpstr.appendFormat(COMMAND_DEVICE_COMPOSITION_THRESHOLD " (%d)\n", mDeviceCompositionThreshold);
        dumpstr.appendFormat(COMMAND_SCALE_LIMIT " (%.2f)\n", mScaleLimit);
        dumpstr.appendFormat(COMMAND_DRM_BLOCK_MODE " (%d)\n", mDrmBlockMode);

        dumpstr.append(COMMAND_HIDE_PLANE "/" COMMAND_SHOW_PATTERN_ON_PLANE " (");

        for (auto planeFlag = mPlanesDebugFlag.begin(); planeFlag != mPlanesDebugFlag.end(); planeFlag++) {
            dumpstr.appendFormat("%d-%d   ", planeFlag->first, planeFlag->second);
        }
        dumpstr.append(")\n");

        dumpstr.append(COMMAND_HIDE_LAYER " (");
        for (auto it = mHideLayers.begin(); it < mHideLayers.end(); it++) {
            dumpstr.appendFormat("%d    ", *it);
        }
        dumpstr.append(")\n");

        dumpstr.append(COMMAND_SAVE_LAYER " (");
        for (auto it = mSaveLayers.begin(); it < mSaveLayers.end(); it++) {
            dumpstr.appendFormat("%d    ", *it);
        }
        dumpstr.append(")\n");
    }
#endif
}

