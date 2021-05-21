/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H
#include <errno.h>
#include <BasicTypes.h>

#if PLATFORM_SDK_VERSION >=  26
#include <vendor/amlogic/hardware/systemcontrol/1.1/ISystemControl.h>
using ::vendor::amlogic::hardware::systemcontrol::V1_1::ISystemControl;
#else
#include <ISystemControlService.h>
#include <binder/IServiceManager.h>
#endif

int32_t sc_get_hdmitx_mode_list(std::vector<std::string>& edidlist);
int32_t sc_get_hdmitx_hdcp_state(bool & val);
int32_t sc_get_display_mode(std::string &dispmode);
int32_t sc_set_display_mode(std::string &dispmode);
int32_t sc_get_osd_position(std::string &dispmode, int *position);

int32_t sc_write_sysfs(const char * path, std::string & val);
int32_t sc_read_sysfs(const char * path, std::string & val);

int32_t sc_read_bootenv(const char * key, std::string & val);
bool sc_set_bootenv(const char *key, const std::string &val);
bool sc_get_property_boolean(const char * prop, bool val);
int32_t sc_get_property_string(const char * prop, std::string & val, const std::string & def);
int32_t sc_set_property(const char *prop, const char *val);

int32_t sc_sink_support_dv(std::string &mode, bool &val);
int32_t sc_get_dolby_version_type();
bool sc_is_dolby_version_enable();

bool sc_get_pref_display_mode(std::string & dispmode);

int32_t get_hdmitx_mode_list(std::vector<std::string>& edidlist);
int32_t get_hdmitx_hdcp_state(bool & val);
int32_t read_sysfs(const char * path, std::string & val);

int32_t sc_notify_hdmi_plugin();
int32_t sc_set_hdmi_allm(bool on);
#if PLATFORM_SDK_VERSION == 30
// for self-adaptive
int32_t sc_frame_rate_display(bool on,  const ISystemControl::Rect& rect);
#endif
#endif/*SYSTEM_CONTROL_H*/
