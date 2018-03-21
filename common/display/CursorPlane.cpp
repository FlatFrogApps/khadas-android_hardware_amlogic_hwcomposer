/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "CursorPlane.h"
#include <sys/mman.h>

CursorPlane::CursorPlane(int32_t drvFd, uint32_t id)
    : HwDisplayPlane(drvFd, id),
      mDrmFb(NULL),
      mCursorPlaneBlank(false),
      mLastTransform(0) {
      mPlaneType = CURSOR_PLANE;
    snprintf(mName, 64, "CURSOR-%d", id);
}

CursorPlane::~CursorPlane() {

}

const char * CursorPlane::getName() {
    return mName;
}

int32_t CursorPlane::setPlane(std::shared_ptr<DrmFramebuffer> &fb) {
    if (mDrvFd < 0) {
        MESON_LOGE("cursor plane fd is not valiable!");
        return -EBADF;
    }

    drm_rect_t disFrame      = fb->mDisplayFrame;
    buffer_handle_t buf      = fb->mBufferHandle;

    /* osd request plane zorder > 0 */
    mPlaneInfo.zorder        = fb->mZorder + 1;
    mPlaneInfo.transform     = fb->mTransform;
    mPlaneInfo.dst_x         = disFrame.left;//TODO reproduction rate
    mPlaneInfo.dst_y         = disFrame.top;

    mPlaneInfo.format        = am_gralloc_get_format(buf);
    mPlaneInfo.shared_fd     = am_gralloc_get_buffer_fd(buf);
    mPlaneInfo.stride        = am_gralloc_get_stride_in_pixel(buf);
    mPlaneInfo.buf_w         = am_gralloc_get_width(buf);
    mPlaneInfo.buf_h         = am_gralloc_get_height(buf);

    updateCursorBuffer();
    setCursorPosition(mPlaneInfo.dst_x, mPlaneInfo.dst_y);
    blank(false);
    mDrmFb = fb;
    return 0;
}

int32_t CursorPlane::updateCursorBuffer() {
    void *cbuffer;
    int cbwidth = 0, bppX = BPP_4;
    switch (mPlaneInfo.format)
    {
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            bppX = BPP_4;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            bppX = BPP_3;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            bppX = BPP_2;
            break;
        default:
            MESON_LOGD("Get error format, use the default value.");
            break;
    }
    cbwidth = HWC_ALIGN(bppX * mPlaneInfo.buf_w, BYTE_ALIGN_32) / bppX;

    MESON_LOGI("This is a Sprite, hnd->width is %d(%d), hnd->height is %d",
                mPlaneInfo.buf_w, cbwidth, mPlaneInfo.buf_h);
    if (mPlaneInfo.info.xres != (uint32_t)cbwidth || mPlaneInfo.info.yres != (uint32_t)mPlaneInfo.buf_h) {
        MESON_LOGI("disp: %d cursor need to redrew", mDrvFd);
        updatePlaneInfo(cbwidth, mPlaneInfo.buf_h);
        cbuffer = mmap(NULL, mPlaneInfo.fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, mDrvFd, 0);
        if (cbuffer != MAP_FAILED) {
            memset(cbuffer, 1, mPlaneInfo.fbSize);
            unsigned char *base = (unsigned char*)mmap(
                                NULL, mPlaneInfo.fbSize, PROT_READ|PROT_WRITE,
                                MAP_SHARED, mPlaneInfo.shared_fd, 0);

            uint32_t irow = 0;
            char* cpyDst = (char*)cbuffer;
            char* cpySrc = (char*)base;
            for (irow = 0; irow < mPlaneInfo.buf_h; irow++) {
                memcpy(cpyDst, cpySrc, bppX * mPlaneInfo.buf_w);
                cpyDst += bppX * cbwidth;
                cpySrc += bppX * mPlaneInfo.stride;
            }
            munmap(cbuffer, mPlaneInfo.fbSize);
            MESON_LOGI("setCursor ok");
        } else {
           MESON_LOGE("Cursor plane buffer mmap fail!");
           return -EBADF;
        }
    }

    return 0;
}

int32_t CursorPlane::updatePlaneInfo(int xres, int yres) {
    struct fb_fix_screeninfo finfo;
    if (ioctl(mDrvFd, FBIOGET_FSCREENINFO, &finfo) != 0)
        return -EINVAL;

    struct fb_var_screeninfo info;
    if (ioctl(mDrvFd, FBIOGET_VSCREENINFO, &info) != 0)
        return -EINVAL;

    MESON_LOGI("vinfo. %d %d", info.xres, info.yres);

    info.xoffset = info.yoffset = 0;
    info.bits_per_pixel = 32;

    /*
    * Explicitly request 8/8/8/8
    */
    info.bits_per_pixel = 32;
    info.red.offset     = 0;
    info.red.length     = 8;
    info.green.offset   = 8;
    info.green.length   = 8;
    info.blue.offset    = 16;
    info.blue.length    = 8;
    info.transp.offset  = 24;
    info.transp.length  = 8;

    info.xres_virtual = info.xres = xres;
    info.yres_virtual = info.yres = yres;

    if (ioctl(mDrvFd, FBIOPUT_VSCREENINFO, &info) != 0)
        return -EINVAL;

    if (ioctl(mDrvFd, FBIOGET_VSCREENINFO, &info) != 0)
        return -EINVAL;

    if (int(info.width) <= 0 || int(info.height) <= 0)
    {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    if (ioctl(mDrvFd, FBIOGET_FSCREENINFO, &finfo) != 0)
        return -EINVAL;

    MESON_LOGI("using (fd=%d)\n"
        "id           = %s\n"
        "xres         = %d px\n"
        "yres         = %d px\n"
        "xres_virtual = %d px\n"
        "yres_virtual = %d px\n"
        "bpp          = %d\n",
        mDrvFd,
        finfo.id,
        info.xres,
        info.yres,
        info.xres_virtual,
        info.yres_virtual,
        info.bits_per_pixel);

    MESON_LOGI("width        = %d mm \n"
        "height       = %d mm \n",
        info.width,
        info.height);

    if (finfo.smem_len <= 0)
        return -EBADF;

    mPlaneInfo.info = info;
    mPlaneInfo.finfo = finfo;
    MESON_LOGD("updatePlaneInfo: finfo.line_length is 0x%x,info.yres_virtual is 0x%x",
                    finfo.line_length, info.yres_virtual);
    mPlaneInfo.fbSize = round_up_to_page_size(finfo.line_length * info.yres_virtual);

    return 0;
}

int32_t CursorPlane::setCursorPosition(int32_t x, int32_t y) {
    fb_cursor cinfo;
    int32_t transform = mPlaneInfo.transform;
    if (mLastTransform != transform) {
        MESON_LOGD("setCursorPosition: mLastTransform: %d, transform: %d.",
                    mLastTransform, transform);
        int arg = 0;
        switch (transform) {
            case HAL_TRANSFORM_ROT_90:
                arg = 2;
            break;
            case HAL_TRANSFORM_ROT_180:
                arg = 1;
            break;
            case HAL_TRANSFORM_ROT_270:
                arg = 3;
            break;
            default:
                arg = 0;
            break;
        }
        if (ioctl(mDrvFd, FBIOPUT_OSD_REVERSE, arg) != 0)
            MESON_LOGE("set cursor reverse ioctl return(%d)", errno);
        mLastTransform = transform;
    }
    cinfo.hot.x = x;
    cinfo.hot.y = y;
    MESON_LOGI("setCursorPosition x_pos=%d, y_pos=%d", cinfo.hot.x, cinfo.hot.y);
    if (ioctl(mDrvFd, FBIOPUT_OSD_CURSOR, &cinfo) != 0)
        MESON_LOGE("set cursor position ioctl return(%d)", errno);

    return 0;
}

int32_t CursorPlane::updateOsdPosition(const char * axis) {
    int soc_w, soc_h, dst_w, dst_h;
    sysfs_set_string(DISPLAY_FB1_SCALE_AXIS, axis);
    MESON_LOGD("CursorPlane updateScaleAxis: %s", axis);
    sscanf(axis, "%d %d %d %d", &soc_w, &soc_h, &dst_w ,&dst_h);
    if ((soc_w != dst_w) || (soc_h != dst_h)) {
        sysfs_set_string(DISPLAY_FB1_SCALE, "0x10001");
    } else {
        sysfs_set_string(DISPLAY_FB1_SCALE, "0");
    }

    return 0;
}

int32_t CursorPlane::blank(bool blank) {
    //MESON_LOGD("cursor plane blank: %d(%d)", blank, mCursorPlaneBlank);

    if (mDrvFd < 0) {
        MESON_LOGE("cursor plane fd is not valiable!");
        return -EBADF;
    }

    if (mCursorPlaneBlank != blank) {
        uint32_t val = blank ? 1 : 0;
        if (ioctl(mDrvFd, FBIOPUT_OSD_SYNC_BLANK, &val) != 0) {
            MESON_LOGE("cursor plane blank ioctl (%d) return(%d)", blank, errno);
            return -EINVAL;
        }
        mCursorPlaneBlank = blank;
    }

    return 0;
}

void CursorPlane::dump(String8 & dumpstr) {
    if (!mCursorPlaneBlank) {
        dumpstr.appendFormat("  osd%1d | %3d |\n",
                 mId - 30,
                 mPlaneInfo.zorder);
    }
}

