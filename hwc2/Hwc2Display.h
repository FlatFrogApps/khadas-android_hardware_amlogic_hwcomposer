/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef HWC2_DISPLAY_H
#define HWC2_DISPLAY_H

#include <map>
#include <vector>
#include <unordered_map>
#include <hardware/hwcomposer2.h>
#include <condition_variable>

#include <BitsMap.h>
#include <HwcDisplay.h>
#include <HwcPowerMode.h>
#include <HwcVsync.h>

#include <ComposerFactory.h>
#include <IComposer.h>
#include <ICompositionStrategy.h>
#include <EventThread.h>

#include "Hwc2Layer.h"
#include "MesonHwc2Defs.h"
#include "HwcModeMgr.h"

#define MESON_DISPLAY_HOTPLUG_MASK    ((uint8_t)1 << 0)
#define MESON_DISPLAY_MODE_MASK       ((uint8_t)1 << 1)
#define MESON_DISPLAY_POWER_MODE_MASK ((uint8_t)1 << 2)
#define MESON_DISPLAY_ALL_MASK \
    ((uint8_t)1 << 0 | (uint8_t)1 << 1 | (uint8_t)1 << 2)

class VtDisplayThread;

/* IComposerClient@2.4::DisplayConnectionType */
enum {
    DISPLAY_TYPE_INTERNAL = 0,
    DISPLAY_TYPE_EXTERNAL = 1,
};

class Hwc2DisplayObserver  {
public:
    Hwc2DisplayObserver(){};
    virtual ~Hwc2DisplayObserver(){};
    virtual void refresh() = 0;
    virtual void onVsync(int64_t timestamp, uint32_t vsyncPeriodNanos) = 0;
    virtual void onHotplug(bool connected) = 0;
    virtual void onVsyncPeriodTimingChanged(hwc_vsync_period_change_timeline_t* updatedTimeline) = 0;
};

class Hwc2Display
    : public HwcDisplay, public HwcVsyncObserver, public VtDisplayObserver {
public:
    Hwc2Display(std::shared_ptr<Hwc2DisplayObserver> observer, uint32_t display);
    virtual ~Hwc2Display();

    virtual void dump(String8 & dumpstr);

    uint32_t getDisplayId() { return mDisplayId;}

/*HWC2 interfaces.*/
public:
    /*Connector releated.*/
    virtual const char * getName();
    virtual const drm_hdr_capabilities_t * getHdrCapabilities();
#ifdef HWC_HDR_METADATA_SUPPORT
    virtual hwc2_error_t getFrameMetadataKeys (
        uint32_t* outNumKeys, int32_t* outKeys);
#endif

    /*Vsync*/
    virtual hwc2_error_t setVsyncEnable(hwc2_vsync_t enabled);

    /*Layer releated.*/
    virtual std::shared_ptr<Hwc2Layer> getLayerById(hwc2_layer_t id);
    virtual hwc2_error_t createLayer(hwc2_layer_t * outLayer);
    virtual hwc2_error_t destroyLayer(hwc2_layer_t  inLayer);
    virtual hwc2_error_t setCursorPosition(hwc2_layer_t layer,
        int32_t x, int32_t y);

    virtual hwc2_error_t setColorTransform(const float* matrix,
        android_color_transform_t hint);
    virtual hwc2_error_t setPowerMode(hwc2_power_mode_t mode);

    /*Compose flow*/
    virtual hwc2_error_t validateDisplay(uint32_t* outNumTypes,
        uint32_t* outNumRequests);
    virtual hwc2_error_t presentDisplay(int32_t* outPresentFence);
    virtual hwc2_error_t acceptDisplayChanges();
    virtual hwc2_error_t getChangedCompositionTypes(
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t*  outTypes);
    virtual hwc2_error_t getDisplayRequests(
        int32_t* outDisplayRequests, uint32_t* outNumElements,
        hwc2_layer_t* outLayers,int32_t* outLayerRequests);
    virtual hwc2_error_t setClientTarget( buffer_handle_t target,
        int32_t acquireFence, int32_t dataspace, hwc_region_t damage);
    virtual hwc2_error_t getReleaseFences(uint32_t* outNumElements,
        hwc2_layer_t* outLayers, int32_t* outFences);

    /*display attrbuites*/
    virtual hwc2_error_t  getDisplayConfigs(
        uint32_t* outNumConfigs, hwc2_config_t* outConfigs);
    virtual hwc2_error_t  getDisplayAttribute(
        hwc2_config_t config, int32_t attribute, int32_t* outValue);
    virtual hwc2_error_t getActiveConfig(hwc2_config_t* outConfig);
    virtual hwc2_error_t setActiveConfig(hwc2_config_t config);
    virtual hwc2_error_t setCalibrateInfo(int32_t caliX,int32_t caliY,int32_t caliW,int32_t caliH);
    virtual void outsideChanged();
    virtual int32_t getDisplayIdentificationData(uint32_t &outPort,
            std::vector<uint8_t> &outData);
    virtual hwc2_error_t getDisplayCapabilities(
            uint32_t* outNumCapabilities, uint32_t* outCapabilities);

    virtual hwc2_error_t getDisplayVsyncPeriod(hwc2_vsync_period_t* outVsyncPeriod);
    virtual hwc2_error_t setActiveConfigWithConstraints(hwc2_config_t config,
            hwc_vsync_period_change_constraints_t* vsyncPeriodChangeConstraints,
            hwc_vsync_period_change_timeline_t* outTimeline);
    virtual hwc2_error_t setAutoLowLatencyMode(bool enabled);
    virtual hwc2_error_t getSupportedContentTypes(uint32_t* outNum, uint32_t* outSupportedContentTypes);
    virtual hwc2_error_t setContentType(uint32_t contentType);

/*HwcDisplay interface*/
public:
    virtual int32_t initialize();

    virtual int32_t setDisplayResource(
        std::shared_ptr<HwDisplayCrtc> & crtc,
        std::shared_ptr<HwDisplayConnector> & connector,
        std::vector<std::shared_ptr<HwDisplayPlane>> & planes);
    virtual int32_t setModeMgr(std::shared_ptr<HwcModeMgr> & mgr);
    virtual int32_t setPostProcessor(
        std::shared_ptr<HwcPostProcessor> processor);
    virtual int32_t setVsync(std::shared_ptr<HwcVsync> vsync);
    virtual int32_t blankDisplay(bool restLayers = false);

    virtual void onVsync(int64_t timestamp, uint32_t vsyncPeriodNanos);
    virtual void onHotplug(bool connected);
    virtual void onVsyncPeriodTimingChanged(hwc_vsync_period_change_timeline_t* updatedTimeline);
    virtual void onUpdate(bool bHdcp);
    virtual void onModeChanged(int stage);
    virtual void getDispMode(drm_mode_info_t & dispMode);
    virtual void cleanupBeforeDestroy();

/* video tunnel api*/
public:
    virtual void updateVtBuffers();
    virtual hwc2_error_t presentVtVideo(int32_t* outPresentFence);

    virtual int32_t setVtVsync(std::shared_ptr<HwcVsync> vsync);
    virtual void onVTVsync(int64_t timestamp, uint32_t vsyncPeriodNanos);
    virtual void handleVtThread();
    virtual void setVtLayersPresentTime();
    virtual void releaseVtLayers();
    virtual bool handleVtDisplayConnection();
    virtual bool newGameBuffer();
    virtual void onFrameAvailable();

/* meson display ddk */
public:
    int32_t captureDisplayScreen(buffer_handle_t hnd);
    bool getDisplayVsyncAndPeriod(int64_t& timestamp, int32_t& vsyncPeriodNanos);
    bool setFrameRateHint(std::string value);
    bool isDisplayConnected();
    std::unordered_map<hwc2_layer_t, std::shared_ptr<Hwc2Layer>> getAllLayers();
protected:
    /* For compose. */
    hwc2_error_t collectLayersForPresent();
    hwc2_error_t collectComposersForPresent();
    hwc2_error_t collectPlanesForPresent();
    hwc2_error_t collectCompositionStgForPresent();
    hwc2_error_t collectCompositionRequest(
            uint32_t* outNumTypes, uint32_t* outNumRequests);
    hwc2_error_t presentSkipValidateCheck();

    /*for calibrate display frame.*/
    int32_t loadCalibrateInfo();
    int32_t adjustDisplayFrame();

    /*Layer id sequence no.*/
    void initLayerIdGenerator();
    hwc2_layer_t createLayerId();
    void destroyLayerId(hwc2_layer_t id);

    /* For content types.*/
    bool checkIfContentTypeIsSupported(uint32_t contentType);

    /* For vsync */
    int32_t adjustVsyncMode();
    bool hasVideoLayerPresent();

    /*For debug*/
    void dumpPresentLayers(String8 & dumpstr);
    bool isLayerHideForDebug(hwc2_layer_t id);
    void dumpHwDisplayPlane(String8 &dumpstr);

protected:
    std::unordered_map<hwc2_layer_t, std::shared_ptr<Hwc2Layer>> mLayers;
    std::shared_ptr<Hwc2DisplayObserver> mObserver;
    drm_hdr_capabilities_t mHdrCaps;

    /*hw releated components*/
    std::shared_ptr<HwDisplayCrtc> mCrtc;
    std::shared_ptr<HwDisplayConnector> mConnector;
    std::vector<std::shared_ptr<HwDisplayPlane>> mPlanes;

    /*composition releated components*/
    std::map<meson_composer_t, std::shared_ptr<IComposer>> mComposers;
    std::shared_ptr<ICompositionStrategy> mCompositionStrategy;
    bool mFailedDeviceComp;

    /*display configs*/
    std::shared_ptr<HwcModeMgr> mModeMgr;
    std::shared_ptr<HwcVsync> mVsync;

    /*layer id generate*/
    std::shared_ptr<BitsMap> mLayersBitmap;
    int32_t mLayerSeq;

    /* members used in present.*/
    std::vector<std::shared_ptr<DrmFramebuffer>> mPresentLayers;
    std::vector<std::shared_ptr<IComposer>> mPresentComposers;
    std::vector<std::shared_ptr<HwDisplayPlane>> mPresentPlanes;
    std::shared_ptr<ICompositionStrategy> mPresentCompositionStg;

    std::vector<hwc2_layer_t> mChangedLayers;
    std::vector<hwc2_layer_t> mOverlayLayers;

    std::shared_ptr<DrmFramebuffer> mClientTarget;

    /*all go to client composer*/
    bool mForceClientComposer;
    float mColorMatrix[16];

    std::shared_ptr<HwcPowerMode> mPowerMode;
    bool mSkipComposition;
    bool mConfirmSkip;
    bool mSignalHpd;
    bool mValidateDisplay;
    int32_t mPresentFence;

    drm_mode_info_t mDisplayMode;
    display_zoom_info_t mCalibrateInfo;
    int mCalibrateCoordinates[4];

    std::shared_ptr<HwcPostProcessor> mPostProcessor;
    int32_t mProcessorFlags;
    std::vector<uint32_t> mSupportedContentTypes;

#ifdef HWC_HDR_METADATA_SUPPORT
    std::vector<drm_hdr_meatadata_t> mHdrKeys;
#endif
    std::mutex mMutex;

    bool mVsyncState;
    float mScaleValue;

    /* vsync timestamp */
    nsecs_t mVsyncTimestamp;
    int32_t mFRPeriodNanos;
    /* for mixVsync */
    bool mHasVideoPresent;

    /* for video tunnel mode video*/
    std::shared_ptr<VtDisplayThread> mVtDisplayThread;
    std::shared_ptr<HwcVsync> mVtVsync;
    uint8_t mDisplayState;
    bool mVtVsyncStatus;
    bool mOutsideChanged;
    std::mutex mVtMutex;

    /* for activeConfig */
    std::condition_variable mStateCondition;
    std::mutex mStateLock;
    bool mFirstPresent;

    uint32_t mDisplayId;

#if PLATFORM_SDK_VERSION == 30
    // for self-adaptive
    int mVideoLayerRegion;
#endif
};

#endif/*HWC2_DISPLAY_H*/
