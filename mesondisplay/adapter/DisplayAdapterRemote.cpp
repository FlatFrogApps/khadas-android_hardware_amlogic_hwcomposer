/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "DisplayAdapter.h"
#include "DisplayClient.h"
#include "DisplayAdapterRemote.h"

#define IF_SERVER_NOT_READY_RETURN(ret) \
    if (!connectServerIfNeed()) { \
        DEBUG_INFO("Can't connect with server!"); \
        return ret; \
    }

namespace meson{
using namespace std;
using ConnectorType = DisplayAdapter::ConnectorType;
using BackendType = DisplayAdapter::BackendType;

DisplayAdapter::BackendType DisplayAdapterRemote::displayType() {
    Json::Value cmd, ret;
    IF_SERVER_NOT_READY_RETURN(DisplayAdapter::DISPLAY_TYPE_MAX);
    cmd["cmd"] = "displayType";
    ipc->send_request_wait_reply(cmd, ret);
    if (ret.isMember("ret")) {
        DEBUG_INFO("Server reply error!");
        return (DisplayAdapter::BackendType)ret["ret"].asUInt();
    } else {
        return DisplayAdapter::DISPLAY_TYPE_MAX;
    }
};

bool DisplayAdapterRemote::getSupportDisplayModes(vector<DisplayModeInfo>& displayModeList, ConnectorType displayType) {
    Json::Value cmd, ret;
    Json::FastWriter write;
    IF_SERVER_NOT_READY_RETURN(false);
    cmd["cmd"] = "getSupportDisplayModes";
    cmd["p_displayType"] = displayType;
    ipc->send_request_wait_reply(cmd, ret);
    if (ret.isMember("ret") && ret["ret"].isMember("displayModeList")) {
        Json::Value& list = ret["ret"]["displayModeList"];
        displayModeList.clear();
        for (unsigned int i = 0; i < list.size(); i++) {
            Json::Value& mode_in = list[i];
            DisplayModeInfo mode;
            if (true == Json2DisplayMode(mode_in, mode)) {
                displayModeList.push_back(mode);
            } else {
                DEBUG_INFO("Get Wrong DisplayMode info");
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
};
bool DisplayAdapterRemote::getDisplayMode(string& mode, ConnectorType displayType) {
    Json::Value cmd, ret;
    Json::FastWriter write;
    IF_SERVER_NOT_READY_RETURN(false);
    cmd["cmd"] = "getDisplayMode";
    cmd["p_displayType"] = displayType;
    ipc->send_request_wait_reply(cmd, ret);
    if (ret.isMember("ret") && ret["ret"]["mode"].isString()) {
        mode = ret["ret"]["mode"].asString();
        return true;
    } else
        return false;
};
bool DisplayAdapterRemote::setDisplayMode(const string& mode, ConnectorType displayType) {
    Json::Value cmd;
    IF_SERVER_NOT_READY_RETURN(false);
    cmd["cmd"] = "setDisplayMode";
    cmd["p_mode"] = mode;
    cmd["p_displayType"] = displayType;
    ipc->send_request(cmd);
    return true;
};
bool DisplayAdapterRemote::setPrefDisplayMode(const string& mode, ConnectorType displayType) {
    Json::Value cmd;
    IF_SERVER_NOT_READY_RETURN(false);
    cmd["cmd"] = "setPrefDisplayMode";
    cmd["p_mode"] = mode;
    cmd["p_displayType"] = displayType;
    ipc->send_request(cmd);
    return true;
};

bool DisplayAdapterRemote::captureDisplayScreen(const native_handle_t **outBufferHandle) {
    IF_SERVER_NOT_READY_RETURN(false);
    return ipc->captureDisplayScreen(outBufferHandle);
}

DisplayAdapterRemote::DisplayAdapterRemote() {
    ipc = DisplayClient::create("DisplayAdapterRemote");
    if (!ipc)
        DEBUG_INFO("Error when connect with Server");
}

std::unique_ptr<DisplayAdapter> DisplayAdapterRemote::create() {
    return static_cast<std::unique_ptr<DisplayAdapter>>(std::make_unique<DisplayAdapterRemote>());
}

bool DisplayAdapterRemote::connectServerIfNeed() {
        return ipc->tryGetService();
};

std::unique_ptr<DisplayAdapter> DisplayAdapterCreateRemote() {
    return DisplayAdapterRemote::create();
};

} //namespace android
