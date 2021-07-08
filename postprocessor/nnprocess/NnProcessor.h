/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef NN_PROCESSOR_H
#define NN_PROCESSOR_H

#include <FbProcessor.h>
#include <queue>
#include <linux/ion.h>
#include <ion/ion.h>

#define SR_OUT_BUF_COUNT 5

#define META_DATA_SIZE      (256)
#define NN_MODE_COUNT       (3)

enum buf_status_e {
    BUF_INVALID = 0,
    BUF_NN_START = 1,
    BUF_NN_DONE = 2,
};

struct sr_buffer_t {
    int index;
    int fd;
    void *fd_ptr; //only for non-nativebuffer!
    ion_user_handle_t ion_hnd; //only for non-nativebuffer!
    int fence_fd;
    int fence_fd_last;
    int64_t phy;
    int size;
    std::shared_ptr<DrmFramebuffer> outFb;
    int shared_fd;
    int status;
};

enum nn_status_e {
    NN_INVALID = 0,
    NN_WAIT_DOING = 1,
    NN_START_DOING = 2,
    NN_DONE = 3,
    NN_DISPLAYED = 4
};

enum nn_mode_e {
    NN_MODE_2X2 = 1,
    NN_MODE_3X3 = 2,
    NN_MODE_4X4 = 3,
};

enum get_info_type_e {
    GET_INVALID = 0,
    GET_HF_INFO = 1,
    GET_HF_DATA = 2,
};

struct time_info_t {
    int64_t count;
    uint64_t max_time;
    uint64_t min_time;
    uint64_t total_time;
    uint64_t avg_time;
};

struct uvm_ai_sr_info {
    int32_t shared_fd;
    int32_t nn_out_fd;
    int32_t fence_fd;
    int64_t nn_out_phy_addr;
    int32_t nn_out_width;
    int32_t nn_out_height;
    int64_t hf_phy_addr;
    int32_t hf_width;
    int32_t hf_height;
    int32_t hf_align_w;
    int32_t hf_align_h;
    int32_t nn_status;
    int32_t nn_index;
    int32_t nn_mode;
    int32_t get_info_type;
};

enum uvm_hook_mod_type {
    VF_SRC_DECODER,
    VF_SRC_VDIN,
    VF_PROCESS_V4LVIDEO,
    VF_PROCESS_DI,
    VF_PROCESS_VIDEOCOMPOSER,
    VF_PROCESS_DECODER,
    PROCESS_NN,
    PROCESS_GRALLOC,
};

struct uvm_hf_info_t {
    enum uvm_hook_mod_type mode_type;
    int shared_fd;
    struct uvm_ai_sr_info ai_sr_info;
};

struct uvm_hook_data {
    enum uvm_hook_mod_type mode_type;
    int shared_fd;
    char data_buf[META_DATA_SIZE + 1];
};

union uvm_ioctl_arg {
    struct uvm_hook_data hook_data;
    struct uvm_hf_info_t uvm_hf_info;
};

class NnProcessor : public FbProcessor {
public:
    NnProcessor();
    ~NnProcessor();

    int32_t setup();
    int32_t process(
        std::shared_ptr<DrmFramebuffer> & inputfb,
        std::shared_ptr<DrmFramebuffer> & outfb);
    int32_t asyncProcess(
        std::shared_ptr<DrmFramebuffer> & inputfb,
        std::shared_ptr<DrmFramebuffer> & outfb,
        int & processFence);
    int32_t onBufferDisplayed(
        std::shared_ptr<DrmFramebuffer> & outfb,
        int releaseFence);
    int32_t teardown();
    int allocDmaBuffer();
    int freeDmaBuffers();
    void triggerEvent();
    void threadProcess();
    int32_t waitEvent(int microseconds);
    static void *mNn_qcontext[NN_MODE_COUNT];
    static bool mModelLoaded;
    mutable std::mutex mMutex;
    std::queue<int> mBuf_index_q;
    static void * threadMain(void * data);
    int LoadNNModel();
    pthread_t mThread;
    bool mExitThread;
    bool mInited;
    int32_t ai_sr_process(
    int input_fd,
    struct sr_buffer_t *sr_buf,
    int nn_bypass);
    void dump_nn_out(struct sr_buffer_t *sr_buf);
    int PropGetInt(const char* str, int def);
    int nn_check_D();
    pthread_mutex_t m_waitMutex;
    pthread_cond_t m_waitCond;
    struct sr_buffer_t mSrBuf[SR_OUT_BUF_COUNT];
    int mIonFd;
    int32_t mBuf_index;
    int32_t mBuf_index_cur;
    int32_t mBuf_index_last;
    int mUvmHander;
    int mNn_Index;
    int mNn_mode;
    int mDumpHf;
    buffer_handle_t mLast_buf;
    static struct time_info_t mTime[NN_MODE_COUNT];
    static int mInstanceID;
    static int log_level;
    bool mBuf_Alloced;
    bool mNeed_fence;
    int64_t mFence_receive_count;
    int64_t mFence_wait_count;
    bool mIsModelInterfaceExist;
};

#endif
