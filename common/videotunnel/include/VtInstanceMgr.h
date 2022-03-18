/*
 * Copyright (c) 2021 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef MESON_VT_INSTANCE_MGR
#define MESON_VT_INSTANCE_MGR

#include <memory>
#include <map>

#include <utils/Singleton.h>
#include <video_tunnel.h>
#include "VtConsumer.h"
#include "VtInstance.h"
#include "VtHandleEventsThread.h"
#include "VideoTunnelDev.h"


class VtInstanceMgr : public android::Singleton<VtInstanceMgr> {
public:
    VtInstanceMgr();
    ~VtInstanceMgr();

    int32_t connectInstance(int tunnelId, std::shared_ptr<VtConsumer> & consumer);
    int32_t disconnectInstance(int tunnelId, std::shared_ptr<VtConsumer> & consumer);
    void clearUpInstancesLocked();
    void clearUpInstances();

    VideoTunnelDev::VtPollStatus pollVtEvents();
    int32_t handleBuffers();
    int32_t handleCmds();

protected:
    const char *vtPollStatusToString(VideoTunnelDev::VtPollStatus status);
private:
    std::map<int, std::shared_ptr<VtInstance>> mInstances;
    std::shared_ptr<VtHandleEventsThread> mVtHandleEventsThread;
    std::mutex mMutex;
};

#endif /* MESON_VT_INSTANCE_MGR */