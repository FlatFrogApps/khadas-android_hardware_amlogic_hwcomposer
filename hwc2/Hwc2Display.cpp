/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#include <hardware/hwcomposer2.h>
#include <inttypes.h>

#include "Hwc2Display.h"
#include "Hwc2Base.h"

#include <DrmTypes.h>
#include <HwcConfig.h>
#include <MesonLog.h>
#include <DebugHelper.h>
#include <Composition.h>
#include <IComposer.h>
#include <ComposerFactory.h>
#include <CompositionStrategyFactory.h>
#include <EventThread.h>

Hwc2Display::Hwc2Display(hw_display_id dspId,
    std::shared_ptr<Hwc2DisplayObserver> observer) {
    mHwId = dspId;
    mObserver = observer;
    mForceClientComposer = false;
    mIsConnected = false;
    memset(&mHdrCaps, 0, sizeof(mHdrCaps));

    mIgnorBlankInBoot = true;
}

Hwc2Display::~Hwc2Display() {
    HwDisplayManager::getInstance().unregisterObserver(mHwId);

    mLayers.clear();
    mPlanes.clear();
    mComposers.clear();

    mCrtc.reset();
    mConnector.reset();
    mObserver.reset();
    mCompositionStrategy.reset();

    mModeMgr.reset();
}

int32_t Hwc2Display::initialize() {
    if (MESON_DUMMY_DISPLAY_ID != mHwId) {
        HwDisplayManager::getInstance().registerObserver(mHwId, this);
     } else {
        MESON_LOGE("Init Hwc2Display with dummy display.");
     }

    /*get hw components.*/
    mModeMgr = createModeMgr(HwcConfig::getModePolicy());

    /*add valid composers*/
    std::shared_ptr<IComposer> composer;
    ComposerFactory::create(MESON_CLIENT_COMPOSER, composer);
    mComposers.emplace(MESON_CLIENT_COMPOSER, std::move(composer));
    ComposerFactory::create(MESON_DUMMY_COMPOSER, composer);
    mComposers.emplace(MESON_DUMMY_COMPOSER, std::move(composer));
#if defined(HWC_ENABLE_GE2D_COMPOSITION)
    //TODO: havenot finish ge2d composer.
    ComposerFactory::create(MESON_GE2D_COMPOSER, composer);
    mComposers.emplace(MESON_GE2D_COMPOSER, std::move(composer));
#endif

    initLayerIdGenerator();
    loadDisplayResources();
    mIsConnected = mConnector->isConnected();

    return 0;
}

void Hwc2Display::loadDisplayResources() {
    if (MESON_DUMMY_DISPLAY_ID == mHwId)
        return;

    HwDisplayManager::getInstance().registerObserver(mHwId, this);
    HwDisplayManager::getInstance().getCrtc(mHwId, mCrtc);
    HwDisplayManager::getInstance().getPlanes(mHwId, mPlanes);
    HwDisplayManager::getInstance().getConnector(mHwId, mConnector);
    mCrtc->loadProperities();
    mConnector->loadProperities();
    mConnector->getHdrCapabilities(&mHdrCaps);
    mModeMgr->setDisplayResources(mCrtc, mConnector);

    /*update composition strategy.*/
    uint32_t strategyFlags = 0;
    int osdPlanes = 0;
    for (auto it = mPlanes.begin(); it != mPlanes.end(); ++ it) {
        if ((*it)->getPlaneType() == OSD_PLANE) {
            osdPlanes ++;
            if (osdPlanes > 1) {
                strategyFlags |= MUTLI_OSD_PLANES;
                break;
            }
        }
    }
    mCompositionStrategy =
        CompositionStrategyFactory::create(SIMPLE_STRATEGY, strategyFlags);
    MESON_ASSERT(mCompositionStrategy, "Hwc2Display load composition strategy failed.");
}

const char * Hwc2Display::getName() {
    return mConnector->getName();
}

const drm_hdr_capabilities_t * Hwc2Display::getHdrCapabilities() {
    return &mHdrCaps;
}

hwc2_error_t Hwc2Display::setVsyncEnable(hwc2_vsync_t enabled) {
    HwDisplayManager::getInstance().enableVBlank(enabled);
    return HWC2_ERROR_NONE;
}

void Hwc2Display::onVsync(int64_t timestamp) {
    if (mObserver != NULL) {
        mObserver->onVsync(timestamp);
    }
}

void Hwc2Display::onHotplug(bool connected) {
    std::lock_guard<std::mutex> lock(mMutex);
    MESON_LOGD("On hot plug: [%s]", connected == true ? "Plug in" : "Plug out");

    loadDisplayResources();
    mIsConnected = connected;

    if (mObserver != NULL) {
        mObserver->onHotplug(connected);
    }
}

void Hwc2Display::onModeChanged(int stage) {
    std::lock_guard<std::mutex> lock(mMutex);
    MESON_LOGD("On mode change state: [%s]", stage == 1 ? "Begin to change" : "Complete");
    if (mIsConnected && stage == 0) {
        mModeMgr->updateDisplayResources();
        if (mObserver != NULL) {
            mObserver->refresh();

            /*Update info to surfaceflinger by hotplug.*/
            if (mModeMgr->getPolicyType() == FIXED_SIZE_POLICY) {
                mObserver->onHotplug(true);
            }
        } else {
            MESON_LOGE("No display oberserve register to display (%s)", getName());
        }
    }
}

/*
LayerId is 16bits.
Higher 8 Bits: now is 256 layer slot, the higher 8 bits is the slot index.
Lower 8 Bits: a sequence no, used to distinguish layers have same slot index (
which may happended a layer destoryed and at the same time a new layer created.)
*/
#define MAX_HWC_LAYERS (256)
#define LAYER_SLOT_BITS (8)

void Hwc2Display::initLayerIdGenerator() {
    mLayersBitmap = std::make_shared<BitsMap>(MAX_HWC_LAYERS);
    mLayerSeq = 0;
}

hwc2_layer_t Hwc2Display::createLayerId() {
    hwc2_layer_t layerId = 0;
    int idx = mLayersBitmap->getZeroBit();
    mLayersBitmap->setBit(idx);

    mLayerSeq++;
    mLayerSeq %= MAX_HWC_LAYERS;

    layerId = ((idx & (MAX_HWC_LAYERS - 1)) << LAYER_SLOT_BITS) |mLayerSeq;
    return layerId;
}

void Hwc2Display::destroyLayerId(hwc2_layer_t id) {
    int slotIdx = id >> LAYER_SLOT_BITS;
    mLayersBitmap->clearBit(slotIdx);
}

hwc2_error_t Hwc2Display::createLayer(hwc2_layer_t * outLayer) {
    std::lock_guard<std::mutex> lock(mMutex);

    std::shared_ptr<Hwc2Layer> layer = std::make_shared<Hwc2Layer>();
    uint32_t idx = createLayerId();
    *outLayer = idx;
    layer->setUniqueId(*outLayer);
    mLayers.emplace(*outLayer, layer);
    MESON_LOGD("createLayer (%d-%p)", idx,  layer.get());

    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::destroyLayer(hwc2_layer_t  inLayer) {
    std::lock_guard<std::mutex> lock(mMutex);

    MESON_LOGD("destoryLayer (%" PRId64 ")", inLayer);

    DebugHelper::getInstance().removeDebugLayer((int)inLayer);

    mLayers.erase(inLayer);
    destroyLayerId(inLayer);
    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::setCursorPosition(hwc2_layer_t layer __unused,
    int32_t x __unused, int32_t y __unused) {
    MESON_LOG_EMPTY_FUN();
    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::setColorTransform(const float* matrix,
    android_color_transform_t hint) {

    if (hint == HAL_COLOR_TRANSFORM_IDENTITY) {
        mForceClientComposer = false;
        memset(mColorMatrix, 0, sizeof(float) * 16);
    } else {
        mForceClientComposer = true;
        memcpy(mColorMatrix, matrix, sizeof(float) * 16);
    }
    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::setPowerMode(hwc2_power_mode_t mode __unused) {
    MESON_LOG_EMPTY_FUN();
    return HWC2_ERROR_NONE;
}

std::shared_ptr<Hwc2Layer> Hwc2Display::getLayerById(hwc2_layer_t id) {
    std::unordered_map<hwc2_layer_t, std::shared_ptr<Hwc2Layer>>::iterator it =
        mLayers.find(id);

    if (it != mLayers.end())
        return it->second;

    return NULL;
}

hwc2_error_t Hwc2Display::collectLayersForPresent() {
    /*
    * 1) add reference to Layers to keep it alive during display.
    * 2) for special layer, update its composition type.
    * 3) sort Layers by zorder for composition.
    */
    mPresentLayers.reserve(10);

    for (auto it = mLayers.begin(); it != mLayers.end(); it++) {
        std::shared_ptr<Hwc2Layer> layer = it->second;
        std::shared_ptr<DrmFramebuffer> buffer = layer;
        mPresentLayers.push_back(buffer);

        if (isLayerHideForDebug(it->first)) {
            layer->mCompositionType = MESON_COMPOSITION_DUMMY;
            continue;
        }

#ifdef HWC_ENABLE_HEADLESS_MODE
        layer->mCompositionType = MESON_COMPOSITION_DUMMY;
#else
        if (layer->mHwcCompositionType == HWC2_COMPOSITION_CLIENT) {
            layer->mCompositionType = MESON_COMPOSITION_CLIENT;
        } else {
            /*
            * Other layers need further handle:
            * 1) HWC2_COMPOSITION_DEVICE
            * 2) HWC2_COMPOSITION_SOLID_COLOR
            * 3) HWC2_COMPOSITION_CURSOR
            * 4) HWC2_COMPOSITION_SIDEBAND
            */
            /*composition type unknown, set to none first.*/
            layer->mCompositionType = MESON_COMPOSITION_UNDETERMINED;
        }
#endif
    }

    if (mPresentLayers.size() > 1) {
        /* Sort mComposeLayers by zorder. */
        struct {
            bool operator() (std::shared_ptr<DrmFramebuffer> a,
                std::shared_ptr<DrmFramebuffer> b) {
                return a->mZorder > b->mZorder;
            }
        } zorderCompare;
        std::sort(mPresentLayers.begin(), mPresentLayers.end(), zorderCompare);
    }

    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::collectPlanesForPresent() {
    mPresentPlanes = mPlanes;
    for (auto  it = mPresentPlanes.begin(); it != mPresentPlanes.end(); it++) {
        std::shared_ptr<HwDisplayPlane> plane = *it;
        if (isPlaneHideForDebug(plane->getPlaneId())) {
            plane->setIdle(true);
        } else {
            plane->setIdle(false);
        }
    }

    return  HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::collectComposersForPresent() {
    for (auto it = mComposers.begin(); it != mComposers.end(); it++) {
        mPresentComposers.push_back(it->second);
    }

    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::validateDisplay(uint32_t* outNumTypes,
    uint32_t* outNumRequests) {
    MESON_LOG_FUN_ENTER();

    /*clear data used in composition.*/
    mPresentLayers.clear();
    mPresentComposers.clear();
    mPresentPlanes.clear();
    mChangedLayers.clear();
    mOverlayLayers.clear();
    mFailedDeviceComp = false;

    hwc2_error_t ret = collectLayersForPresent();
    if (ret != HWC2_ERROR_NONE) {
        return ret;
    }
    /* No layers means post blank,
    * when system boot, we ignor this blank and keep logo displayed*/
    if (mIgnorBlankInBoot && mPresentLayers.size() == 0) {
        MESON_LOGE("ignor blank when boot.");
        return HWC2_ERROR_NONE;
    } else {
        MESON_LOGE("ignor blank finished..");
        mIgnorBlankInBoot = false;
    }

    ret = collectComposersForPresent();
    if (ret != HWC2_ERROR_NONE) {
        return ret;
    }
    ret = collectPlanesForPresent();
    if (ret != HWC2_ERROR_NONE) {
        return ret;
    }

    /*collect composition flag*/
    uint32_t compositionFlags = 0;
    if (DebugHelper::getInstance().disableUiHwc() || mForceClientComposer) {
        compositionFlags |= COMPOSE_FORCE_CLIENT;
    }

#ifdef HWC_ENABLE_SECURE_LAYER
    if (!mConnector->isSecure()) {
        compositionFlags |= COMPOSE_HIDE_SECURE_FB;
    }
#endif

    /*setup composition strategy.*/
    mCompositionStrategy->setup(mPresentLayers,
        mPresentComposers, mPresentPlanes, compositionFlags);
    if (mCompositionStrategy->decideComposition() < 0) {
        return HWC2_ERROR_NO_RESOURCES;
    }

    /*collect changed dispplay, layer, compostiion.*/
    ret = collectCompositionRequest(outNumTypes, outNumRequests);

    /*dump at end of validate, for we need check by some composition info.*/
    bool dumpLayers = false;
    if (DebugHelper::getInstance().logCompositionDetail()) {
        MESON_LOGE("***CompositionFlow (%s):\n", __func__);
        dumpLayers = true;
    } else if (mFailedDeviceComp) {
        MESON_LOGE("***MonitorFailedDeviceComposition: \n");
        dumpLayers = true;
    }
    if (dumpLayers) {
        String8 layersDump;
        dumpPresentLayers(layersDump);
        MESON_LOGE("%s", layersDump.string());
    }
    return ret;
}

hwc2_error_t Hwc2Display::collectCompositionRequest(
    uint32_t* outNumTypes, uint32_t* outNumRequests) {
    Hwc2Layer *layer;
    /*collect display requested, and changed composition type.*/
    for (auto it = mPresentLayers.begin() ; it != mPresentLayers.end(); it++) {
        layer = (Hwc2Layer*)(it->get());
        /*record composition changed layer.*/
        hwc2_composition_t expectedHwcComposition =
            mesonComp2Hwc2Comp(layer);
        if (expectedHwcComposition  != layer->mHwcCompositionType) {
            mChangedLayers.push_back(layer->getUniqueId());
            /*For debug.*/
            if (DebugHelper::getInstance().monitorDeviceComposition() &&
                (mPresentLayers.size() <= DebugHelper::getInstance().deviceCompositionThreshold()) &&
                (expectedHwcComposition == HWC2_COMPOSITION_CLIENT)) {
                mFailedDeviceComp = true;
            }
        }
    }

    /*collcet client clear layer.*/
    std::shared_ptr<IComposer> clientComposer =
        mComposers.find(MESON_CLIENT_COMPOSER)->second;
    std::vector<std::shared_ptr<DrmFramebuffer>> overlayLayers;
    if (0 == clientComposer->getOverlyFbs(overlayLayers) ) {
        auto it = overlayLayers.begin();
        for (; it != overlayLayers.end(); ++it) {
            layer = (Hwc2Layer*)(it->get());
            mOverlayLayers.push_back(layer->getUniqueId());
        }
    }

    *outNumRequests = mOverlayLayers.size();
    *outNumTypes    = mChangedLayers.size();
    MESON_LOGV("layer requests: %d, type changed: %d",
        *outNumRequests, *outNumTypes);

    return ((*outNumTypes) > 0) ? HWC2_ERROR_HAS_CHANGES : HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::getDisplayRequests(
    int32_t* outDisplayRequests, uint32_t* outNumElements,
    hwc2_layer_t* outLayers,int32_t* outLayerRequests) {
    *outDisplayRequests = HWC2_DISPLAY_REQUEST_FLIP_CLIENT_TARGET;

    /*check if need HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET*/
    *outNumElements = mOverlayLayers.size();
    if (outLayers && outLayerRequests) {
        for (uint32_t i = 0; i < mOverlayLayers.size(); i++) {
            outLayers[i] = mOverlayLayers[i];
            outLayerRequests[i] = HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET;
        }
    }

    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::getChangedCompositionTypes(
    uint32_t * outNumElements, hwc2_layer_t * outLayers,
    int32_t *  outTypes) {
    *outNumElements = mChangedLayers.size();
    if (outLayers && outTypes) {
        for (uint32_t i = 0; i < mChangedLayers.size(); i++) {
            std::shared_ptr<Hwc2Layer> layer = mLayers.find(mChangedLayers[i])->second;
            outTypes[i] = mesonComp2Hwc2Comp(layer.get());
            outLayers[i] = mChangedLayers[i];
        }
    }

    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::acceptDisplayChanges() {
   /* commit composition type */
    for (auto it = mPresentLayers.begin() ; it != mPresentLayers.end(); it++) {
        Hwc2Layer * layer = (Hwc2Layer*)(it->get());
        layer->commitCompType(mesonComp2Hwc2Comp(layer));
    }

    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::presentDisplay(int32_t* outPresentFence) {
    MESON_LOG_FUN_ENTER();
    int32_t outFence = -1;

    if (mIgnorBlankInBoot) {
        *outPresentFence = -1;
        return HWC2_ERROR_NONE;
    }

    /*Pre page flip, set display position*/
    if (mCrtc->prePageFlip() != 0) {
        return HWC2_ERROR_NOT_VALIDATED;
    }

    /*Start to compose, set up plane info.*/
    if (mCompositionStrategy->commit() != 0) {
        return HWC2_ERROR_NOT_VALIDATED;
    }

    /* Page flip */
    if (mCrtc->pageFlip(outFence) < 0) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    *outPresentFence = outFence;

    /*dump debug informations.*/
    bool dumpComposition = false;
    if (DebugHelper::getInstance().logCompositionDetail()) {
        MESON_LOGE("***CompositionFlow (%s):\n", __func__);
        dumpComposition = true;
    } else if (mFailedDeviceComp) {
        MESON_LOGE("***MonitorFailedDeviceComposition: \n");
        dumpComposition = true;
    }
    if (dumpComposition) {
        String8 compDump;
        mCompositionStrategy->dump(compDump);
        MESON_LOGE("%s", compDump.string());
    }
    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::getReleaseFences(uint32_t* outNumElements,
    hwc2_layer_t* outLayers, int32_t* outFences) {
    uint32_t num = 0;
    bool needInfo = false;
    if (outLayers && outFences)
        needInfo = true;

    for (auto it = mPresentLayers.begin(); it != mPresentLayers.end(); it++) {
        Hwc2Layer *layer = (Hwc2Layer*)(it->get());
        if (HWC2_COMPOSITION_DEVICE == layer->mHwcCompositionType) {
            num++;
            if (needInfo) {
                int32_t releaseFence = layer->getReleaseFence();
                *outLayers = layer->getUniqueId();
                *outFences = releaseFence;
                outLayers++;
                outFences++;
            }
        }
    }

    *outNumElements = num;
    return HWC2_ERROR_NONE;
}

hwc2_error_t Hwc2Display::setClientTarget(buffer_handle_t target,
    int32_t acquireFence, int32_t dataspace, hwc_region_t damage) {
    /*create DrmFramebuffer for client target.*/
    std::shared_ptr<DrmFramebuffer> clientFb =  std::make_shared<DrmFramebuffer>(
        target, acquireFence);
    clientFb->mFbType = DRM_FB_SCANOUT;
    clientFb->mBlendMode = DRM_BLEND_MODE_PREMULTIPLIED;
    clientFb->mPlaneAlpha = 1.0f;
    clientFb->mTransform = 0;
    clientFb->mDataspace = dataspace;

    /*set framebuffer to client composer.*/
    std::shared_ptr<IComposer> clientComposer =
        mComposers.find(MESON_CLIENT_COMPOSER)->second;
    if (clientComposer.get() &&
        clientComposer->setOutput(clientFb, damage) == 0) {
        return HWC2_ERROR_NONE;
    }

    return HWC2_ERROR_BAD_PARAMETER;
}

hwc2_error_t  Hwc2Display::getDisplayConfigs(
    uint32_t* outNumConfigs,
    hwc2_config_t* outConfigs) {
    if (mModeMgr != NULL) {
        return mModeMgr->getDisplayConfigs(outNumConfigs, outConfigs);
    } else {
        MESON_LOGE("Hwc2Display getDisplayConfigs (%s) miss valid DisplayConfigure.",
            getName());
        return HWC2_ERROR_BAD_DISPLAY;
    }
}

hwc2_error_t  Hwc2Display::getDisplayAttribute(
    hwc2_config_t config,
    int32_t attribute,
    int32_t* outValue) {
    if (mModeMgr != NULL) {
        return mModeMgr->getDisplayAttribute(config, attribute, outValue);
    } else {
        MESON_LOGE("Hwc2Display (%s) getDisplayAttribute miss valid DisplayConfigure.",
            getName());
        return HWC2_ERROR_BAD_DISPLAY;
    }
}

hwc2_error_t Hwc2Display::getActiveConfig(
    hwc2_config_t* outConfig) {
    if (mModeMgr != NULL) {
        return mModeMgr->getActiveConfig(outConfig);
    } else {
        MESON_LOGE("Hwc2Display (%s) getActiveConfig miss valid DisplayConfigure.",
            getName());
        return HWC2_ERROR_BAD_DISPLAY;
    }
}

hwc2_error_t Hwc2Display::setActiveConfig(
    hwc2_config_t config) {
    if (mModeMgr != NULL) {
        return mModeMgr->setActiveConfig(config);
    } else {
        MESON_LOGE("Display (%s) setActiveConfig miss valid DisplayConfigure.",
            getName());
        return HWC2_ERROR_BAD_DISPLAY;
    }
}

bool Hwc2Display::isLayerHideForDebug(hwc2_layer_t id) {
    std::vector<int> hideLayers;
    DebugHelper::getInstance().getHideLayers(hideLayers);
    if (hideLayers.empty())
        return false;

    for (auto it = hideLayers.begin(); it < hideLayers.end(); it++) {
        if (*it == (int)id)
            return true;
    }

    return false;
}

bool Hwc2Display::isPlaneHideForDebug(int id) {
    std::vector<int> hidePlanes;
    DebugHelper::getInstance().getHidePlanes(hidePlanes);
    if (hidePlanes.empty())
        return false;

    for (auto it = hidePlanes.begin(); it < hidePlanes.end(); it++) {
        if (*it == id) {
            return true;
        }
    }

    return false;
}

void Hwc2Display::dumpPresentLayers(String8 & dumpstr) {
    dumpstr.append("----------------------------------------------------------"
        "-------------------------------\n");
    dumpstr.append("|  id  |  z  |  type  |blend| alpha  |t|"
        "  AFBC  |    Source Crop    |    Display Frame  |\n");
    for (auto it = mPresentLayers.begin(); it != mPresentLayers.end(); it++) {
        Hwc2Layer *layer = (Hwc2Layer*)(it->get());
        dumpstr.append("+------+-----+--------+-----+--------+-+--------+"
            "-------------------+-------------------+\n");
        dumpstr.appendFormat("|%6llu|%5d|%8s|%5d|%8f|%1d|%8x|%4d %4d %4d %4d"
            "|%4d %4d %4d %4d|\n",
            layer->getUniqueId(),
            layer->mZorder,
            drmFbTypeToString(layer->mFbType),
            layer->mBlendMode,
            layer->mPlaneAlpha,
            layer->mTransform,
            am_gralloc_get_vpu_afbc_mask(layer->mBufferHandle),
            layer->mSourceCrop.left,
            layer->mSourceCrop.top,
            layer->mSourceCrop.right,
            layer->mSourceCrop.bottom,
            layer->mDisplayFrame.left,
            layer->mDisplayFrame.top,
            layer->mDisplayFrame.right,
            layer->mDisplayFrame.bottom
            );
    }
    dumpstr.append("----------------------------------------------------------"
        "-------------------------------\n");
}

void Hwc2Display::dump(String8 & dumpstr) {
    /*update for debug*/
    if (DebugHelper::getInstance().debugHideLayers() ||
        DebugHelper::getInstance().debugHidePlanes()) {
        //ask for auto refresh for debug layers update.
        if (mObserver != NULL) {
            mObserver->refresh();
        }
    }

    /*dump*/
    dumpstr.append("---------------------------------------------------------"
        "-----------------------------\n");
    dumpstr.appendFormat("Display %d (%s, %s, %s):\n",
        mHwId, getName(), mModeMgr->getName(),
        mForceClientComposer ? "Client-Comp" : "HW-Comp");
    dumpstr.append("\n");

    dumpstr.append("ColorMatrix:\n");
    int matrixRow;
    for (matrixRow = 0; matrixRow < 4; matrixRow ++) {
        dumpstr.appendFormat("| %f %f %f %f |\n",
            mColorMatrix[matrixRow*4], mColorMatrix[matrixRow*4 + 1],
            mColorMatrix[matrixRow*4 + 2], mColorMatrix[matrixRow*4 + 3]);
    }
    dumpstr.append("\n");

    /* HDR info */
    dumpstr.append("HDR Capabilities:\n");
    dumpstr.appendFormat("    DolbyVision1=%d\n",
        mHdrCaps.DolbyVisionSupported ?  1 : 0);
    dumpstr.appendFormat("    HDR10=%d, maxLuminance=%d,"
        "avgLuminance=%d, minLuminance=%d\n",
        mHdrCaps.HDR10Supported ? 1 : 0,
        mHdrCaps.maxLuminance,
        mHdrCaps.avgLuminance,
        mHdrCaps.minLuminance);
    dumpstr.append("\n");

    /* dump display configs*/
     mModeMgr->dump(dumpstr);
    dumpstr.append("\n");

    /*dump detail debug info*/
    if (DebugHelper::getInstance().dumpDetailInfo()) {
        /* dump composers*/
        dumpstr.append("Valid composers:\n");
        for (auto it = mPresentComposers.begin(); it != mPresentComposers.end(); it++) {
            dumpstr.appendFormat("%s-Composer (%p)\n", it->get()->getName(), it->get());
        }
        dumpstr.append("\n");

        /* dump present layers info*/
        dumpstr.append("Present layers:\n");
        dumpPresentLayers(dumpstr);
        dumpstr.append("\n");

        /* dump composition stragegy.*/
        mCompositionStrategy->dump(dumpstr);
        dumpstr.append("\n");
    }

    dumpstr.append("\n");
}

