/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_NDEBUG 0
#define LOG_TAG "hwc_aipq"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nn_util.h>
#include <nn_sdk.h>
#include <nn_demo.h>
#include <dlfcn.h>
#include <utils/Log.h>
#include "pq_sdk.h"

static nn_input inData;
static img_classify_out_t cls_out;

static void *mSdkHandle;
static int (*func_inputSet)(void *, nn_input *);
static void* (*func_outputGet)(void* , aml_output_config_t);
static void* (*func_create)(aml_config*);
static int (*func_destory)(void*);

static void *mDemoHandle;
static void (*func_processTop)(float *, unsigned int, img_classify_out_t*);

void* process_network(void *qcontext,unsigned char *qrawdata) {
    int ret = 0;
    nn_output *outdata = NULL;

    aml_output_config_t outconfig;
    outconfig.typeSize = sizeof(aml_output_config_t);
    outconfig.mdType = CUSTOM_NETWORK;
    outconfig.format = AML_OUTDATA_FLOAT32;

    inData.input = qrawdata;

    if ((func_inputSet != NULL)
        && (func_outputGet != NULL)
        && (func_processTop != NULL)) {
        ret = func_inputSet(qcontext, &inData);
        if (ret != 0) {
            ALOGE("aml_module_input_set error\n");
            return NULL;
        }
        outdata = (nn_output *)func_outputGet(qcontext, outconfig);
        if (outdata == NULL) {
            ALOGE("aml_module_output_get error\n");
            return NULL;
        }
        func_processTop((float*)outdata->out[0].buf,
                        outdata->out[0].size/sizeof(float),
                        &cls_out);
        return (void*)(&cls_out);
    } else {
        ALOGE("%s:interface don't implement.\n", __FUNCTION__);
        return NULL;
    }
}

void* init(const char *path, int model_type) {
    if (func_create != NULL) {
        int width = 224;
        int height = 224;
        void *qcontext = NULL;

        aml_config config;
        memset(&config,0,sizeof(aml_config));
        config.path = path;

        if (model_type == 0) {
            width = 299;
            height = 299;
        } else if (model_type == 1) {
            width = 224;
            height = 224;
        } else {
            ALOGE("inceptionv4 model_type = 0 , mobilenetv2 model_type = 1\n");
        }

        config.modelType = TENSORFLOW;
        qcontext = func_create(&config);

        if (qcontext == NULL) {
            ALOGE("amlnn_init is fail\n");
            return NULL;
        }

        inData.input_index = 0;
        inData.size = width * height * 3;
        inData.input_type = BINARY_RAW_DATA;

        return qcontext;
    }else {
        ALOGE("%s:interface don't implement.\n", __FUNCTION__);
        return NULL;
    }
}

void* uninit(void* context) {
    int ret = 0;
    if (func_destory != NULL)
        ret = func_destory(context);
    else
        ALOGE("%s:interface don't implement.\n", __FUNCTION__);

    if (mSdkHandle != NULL) {
        dlclose(mSdkHandle);
        mSdkHandle = NULL;
    }

    if (mDemoHandle != NULL) {
        dlclose(mDemoHandle);
        mDemoHandle = NULL;
    }

    return NULL;
}

int isPqInterfaceImplement() {
    ALOGD("enter %s.\n", __FUNCTION__);

    int ret = 0;
    if (mSdkHandle == NULL)
        mSdkHandle = dlopen("libnnsdk.so", RTLD_NOW);

    if (mDemoHandle == NULL)
        mDemoHandle = dlopen("libnndemo.so", RTLD_NOW);

    if ((mSdkHandle == NULL) || (mDemoHandle == NULL)) {
        ALOGE("open libnnsdk.so or libnndemo.so fail: %s.\n", dlerror());
    } else {
        func_outputGet = (void *(*)(void *, aml_output_config_t))
            dlsym(mSdkHandle, "aml_module_output_get");
        if (func_outputGet == NULL)
            ALOGD("func_outputGet don't implement.\n");

        func_create = (void *(*)(aml_config*))
            dlsym(mSdkHandle, "aml_module_create");
        if (func_create == NULL)
            ALOGD("func_create don't implement.\n");

        func_destory = (int(*)(void*))dlsym(mSdkHandle, "aml_module_destroy");
        if (func_destory == NULL)
            ALOGD("func_destory don't implement.\n");

        func_inputSet = (int(*)(void*, nn_input *))
                dlsym(mSdkHandle, "aml_module_input_set");
        if (func_inputSet == NULL)
            ALOGD("func_inputSet don't implement.\n");

        func_processTop = (void(*)(float *, unsigned int, img_classify_out_t*))
                dlsym(mDemoHandle, "process_top5");
        if (func_processTop == NULL)
            ALOGD("func_processTop don't implement.\n");

        if ((func_create == NULL)
            || (func_inputSet == NULL)
            || (func_outputGet == NULL)
            || (func_processTop == NULL)
            || (func_destory == NULL)) {
            ALOGE("NN interface don't implement.\n");
        } else {
            ALOGD("NN interface is implement in.\n");
            ret = 1;
        }
    }

    return ret;
}