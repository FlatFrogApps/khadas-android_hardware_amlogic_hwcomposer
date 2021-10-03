/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description: video tunnel functions for videotunnel device
 */


#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sync/sync.h>

#include <log/log.h>

#include <linux/videotunnel.h>
#include <video_tunnel.h>

#define VIDEO_TUNNEL_DEV "/dev/videotunnel"

#ifdef __cplusplus
extern "C" {
#endif

/* open /dev/videotunnel device and return the opened fd */
int meson_vt_open() {
    int fd = open(VIDEO_TUNNEL_DEV, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("[%s] open /dev/videotunnel failed:%s", __func__, strerror(errno));
    }

    return fd;
}

/*
 * close the videotunnel device
 *
 * @param fd [in] videotunnel device fd
 */
int meson_vt_close(int fd) {
    int ret = close(fd);
    if (ret < 0) {
        ALOGE("[%s] close %d failed:%s", __func__, fd, strerror(errno));
        return -errno;
    }

    return ret;
}

/*
 * ioctl to videotunnel device
 */
static int meson_vt_ioctl(int fd, int req, void* arg) {
    if (fd < 0)
        return -ENODEV;

    int ret = ioctl(fd, req, arg);
    if (ret < 0) {
        return -errno;
    }

    return ret;
}

/*
 * videtotunnel control, set role etc.
 */
static int meson_vt_control(int fd, int tunnel_id, enum vt_role_e role, enum vt_ctrl_cmd_e cmd) {
    struct vt_ctrl_data data = {
        .tunnel_id = tunnel_id,
        .role = role,
        .ctrl_cmd = cmd,
    };

    return meson_vt_ioctl(fd, VT_IOC_CTRL, &data);
}

/*
 * Allocate an unique videotunnel id.
 * If successfully, the tunnnel id returned by param *tunnel_id.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [out] allocated tunnel id
 *
 * Return of a value other than 0 means an error has occurred:
 * -ENOMEM - memory allocation failed
 * -ENOSPC - tunnel_id invalid, no free spaces
 */
int meson_vt_alloc_id(int fd, int *tunnel_id) {
    int ret = 0;

    struct vt_alloc_id_data data = {
        .tunnel_id = -1,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_ALLOC_ID, &data);
    if (ret < 0)
        return ret;


    *tunnel_id = data.tunnel_id;
    return ret;
}

/*
 * Free the videotunnel id previous allocated by meson_vt_alloc_id
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to free
 *
 * Return of a value other than 0 means an eror has occurred:
 * -EINVAL - the videotunnel is has not found.
 */
int meson_vt_free_id(int fd, int tunnel_id) {
    struct vt_alloc_id_data data = {
        .tunnel_id = tunnel_id,
    };

    return meson_vt_ioctl(fd, VT_IOC_FREE_ID, &data);
}

/*
 * Connect a consumer or a producer to the sepecific videotunnel.
 * Only one consumer or one producer may be connected to a videotunnel.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 * @param role          [in] 0 for producer, non 0 for consumer
 *
 * Return of a value other than 0 means an error has occurred:
 * -EINVAL - the videotunnel already has a consumer or producer
 * -ENOMEM - memory allocation failed
 * -ENOSPC - tunnel_id invalid, no free spaces
 */
int meson_vt_connect(int fd, int tunnel_id, int role) {

    vt_role_e role_e = (role == 0 ? VT_ROLE_PRODUCER : VT_ROLE_CONSUMER);
    return meson_vt_control(fd, tunnel_id, role_e, VT_CTRL_CONNECT);
}

/*
 * Disconnect a consumer or a producer from the sepecific videotunnel.
 * All the videotunnel buffers of the disconnected side will be freed.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 * @param role          [in] 0 for producer, non 0 for consumer
 *
 * Return of a value other than 0 means an error has occurred:
 * -EINVAL - invalid parameter, the videotunnel may already has no conusmer/producer
 */
int meson_vt_disconnect(int fd, int tunnel_id, int role) {
    vt_role_e role_e = (role == 0 ? VT_ROLE_PRODUCER : VT_ROLE_CONSUMER);
    return meson_vt_control(fd, tunnel_id, role_e, VT_CTRL_DISCONNECT);
}

/*
 * QueueBuffer indicates the producer has finished filling in the contents of the buffer
 * and transfer ownership of the buffer to consumer.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 * @param buffer_fd     [in] buffer fd to transfer
 * @param fence_fd      [in] fence fd (acquire fence) currently not used
 * @param expected_present_time     [in] monotonic time timestamp (in us)
 *
 * Return of a value other than 0 means an error has occurred:
 * -EINVAL - invalid parameter, one of the bellow conditions occurred:
 *         * the videotunnel is not connected
 *         * the wrong videotunnel
 * -EBADF - transfer buffer fd is invalid
 */
int meson_vt_queue_buffer(int fd, int tunnel_id, int buffer_fd,
        int fence_fd, int64_t expected_present_time) {
    struct vt_buffer_data data = {
        .tunnel_id = tunnel_id,
        .buffer_fd = buffer_fd,
        .fence_fd = fence_fd,
        .time_stamp = expected_present_time,
    };

    return meson_vt_ioctl(fd, VT_IOC_QUEUE_BUFFER, &data);
}

/*
 * DequeueBuffer requests a buffer from videotunnel to use. Ownership of the buffer is transfered
 * to the producer. The returned buffer is previous queued use meson_vt_queue_buffer to videotunnel.
 * If no buffer is pending then it returns -EAGAIN with a default 4ms timeout. If a buffer is
 * successfully dequeued, the infomation is returned in *buffer_fd.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 * @param buffer_fd     [out] buffer fd get from videotunnel
 * @param fence_fd      [out] release fence currently not used,
 *                            release fence wil be waited in driver now
 *
 * Return of a value other than 0 means an error has occurred:
 * -EAGAIN - no buffer is pending (a timeout 4ms may be happened)
 * -EINVAL - invalid param, no videotunnel or has not connect
 */
int meson_vt_dequeue_buffer(int fd, int tunnel_id, int *buffer_fd, int *fence_fd) {
    int ret = -1;

    struct vt_buffer_data data = {
        .tunnel_id = tunnel_id,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_DEQUEUE_BUFFER, &data);
    if (ret < 0)
        return ret;

    *buffer_fd = data.buffer_fd;
    *fence_fd = data.fence_fd;

    return ret;
}

/*
 * cancelBuffer returns all the dequeued buffers to the BufferQueue, but doesn't
 * queue it for use by the consumer.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 *
 * Return of a value other than 0 means an error has occurred:
 * -EINVAL - invalid param, no videotunnel or has not connect
 */
int meson_vt_cancel_buffer(int fd, int tunnel_id) {
    return meson_vt_control(fd, tunnel_id, VT_ROLE_PRODUCER, VT_CTRL_CANCEL_BUFFER);
}

/* Set the video source crop
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id
 *
 * @param buffer_fd     [out] buffer fd acquired form videotunnel
 * @param fence_fd      [out] acquire fence, not used now
 * @param expected_present_time [out] monotonic time timestamp
 *
 * Return of 0 means the operation completed as normal.
 * Return of a negative value means an error has occurred:
 */
int meson_vt_set_sourceCrop(int fd, int tunnel_id, struct vt_rect rect) {
    enum vt_video_cmd_e vcmd = VT_VIDEO_SET_SOURCE_CROP;
    struct vt_krect crop = {
        .left = rect.left,
        .top = rect.top,
        .right = rect.right,
        .bottom = rect.bottom,
    };

    struct vt_ctrl_data data = {
        .tunnel_id = tunnel_id,
        .ctrl_cmd = VT_CTRL_SEND_CMD,
        .video_cmd = vcmd,
        .source_crop = crop,
    };

    return meson_vt_ioctl(fd, VT_IOC_CTRL, &data);
}

/* get display vsync timestam and period from videotunnel
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id
 *
 * @param timestamp     [out] timestamp for the vsync
 * @param period        [out] vsync period in nanos
 *
 * Return of 0 means the operation completed as normal.
 * Return of a negative value means an error has occurred:
 */
int meson_vt_getDisplayVsyncAndPeroid(int fd, int tunnel_id, uint64_t *timestamp, uint32_t *period) {
    int ret = -1;
    struct vt_display_vsync data = {
        .tunnel_id = tunnel_id,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_GET_VSYNCTIME, &data);

    if (ret < 0)
        return ret;

    *timestamp = data.timestamp;
    *period = data.period;

    return ret;
}

/*
 * Acquire buffer attemps to acquire ownership of the next pending buffer in the videotunnel.
 * If no buffer is pending then it returns -EAGAIN with a default 4ms timeout. If a buffer is
 * successfully acquired, the infomation is returned in *buffer_fd.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 * @param buffer_fd     [out] buffer fd acquired form videotunnel
 * @param fence_fd      [out] acquire fence, not used now
 * @param expected_present_time [out] monotonic time timestamp
 *
 * Return of 0 means the operation completed as normal.
 * Return of a negative value means an error has occurred:
 * -EAGAIN - no buffer is pending (a timeout 4ms may be happened)
 * -EINVAL - invalid param, no videotunnel or has not connect
 */
int meson_vt_acquire_buffer(int fd, int tunnel_id, int *buffer_fd,
        int *fence_fd, int64_t *expected_present_time) {
    int ret;

    struct vt_buffer_data data = {
        .tunnel_id = tunnel_id,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_ACQUIRE_BUFFER, &data);
    if (ret < 0)
        return ret;

    *buffer_fd = data.buffer_fd;
    *fence_fd = data.fence_fd;
    *expected_present_time = data.time_stamp;

    return ret;
}

/*
 * Release buffer releases a buffer from the cosumer back to the videotunnel.
 * The fence_fd will signal when the buffer is no longer in use by consumer.
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id to connect
 * @param buffer_fd     [in] buffer fd to transfer
 * @param fence_fd      [in] release fence
 *
 * Return of 0 means the operation completed as normal.
 * -EINVAL - invalid param, no videotunnel or has not connect
 */
int meson_vt_release_buffer(int fd, int tunnel_id, int buffer_fd, int fence_fd) {
    int ret = -1;
    struct vt_buffer_data data = {
        .tunnel_id = tunnel_id,
        .buffer_fd = buffer_fd,
        .fence_fd = fence_fd,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_RELEASE_BUFFER, &data);

    /* fence fd has transfered to prodouce, now close it */
    if (fence_fd >= 0)
        close(fence_fd);

    return ret;
}

/*
 * Wait for the vt session to become ready to recieve cmds
 *
 * @param fd            [in] Videotunnel device fd
 * @param time_out      [in] time out in ms
 *
 * On success, a positive number is return.
 * Return of 0 indicates  that the call timed out
 * -EINVAL - invalid param, no videotunnel or has not connect
 */
int meson_vt_poll_cmd(int fd, int time_out) {
    int ret;

    struct vt_ctrl_data data = {
        .ctrl_cmd = VT_CTRL_POLL_CMD,
        .video_cmd_data = time_out,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_CTRL, &data);

    return ret;
}

/* set display vsync timestam and period to videotunnel
 *
 * @param fd            [in] Videotunnel device fd
 * @param tunnel_id     [in] tunnel id
 *
 * @param timestamp     [in] timestamp for the vsync
 * @param period        [in] vsync period in nanos
 *
 * Return of 0 means the operation completed as normal.
 * Return of a negative value means an error has occurred:
 */
int meson_vt_setDisplayVsyncAndPeroid(int fd, int tunnel_id, uint64_t timestamp, uint32_t period) {
    struct vt_display_vsync data = {
        .tunnel_id = tunnel_id,
        .timestamp = timestamp,
        .period = period,
    };

    return meson_vt_ioctl(fd, VT_IOC_SET_VSYNCTIME, &data);
}

/*
 * Set the videotunnel driver to block mode or not
 *
 * @param fd            [in] Videotunnel device fd
 * @param block_mode    [in] block mode or no
 *
 * Return of 0 means the operation completed as normal.
 * -EINVAL - invalid param, has no connected
 */
int meson_vt_set_mode(int fd, int block_mode) {
    enum vt_ctrl_cmd_e vcmd = (block_mode == 0 ?
            VT_CTRL_SET_NONBLOCK_MODE : VT_CTRL_SET_BLOCK_MODE);

    struct vt_ctrl_data data = {
        .ctrl_cmd = vcmd,
    };

    return meson_vt_ioctl(fd, VT_IOC_CTRL, &data);
}

/*
 * Send video cmd to server (hwc) and may wait for reply on some GET_STATUS cmd
 *
 * @param fd            [in] Videotunnel device fd
 * @param cmd           [in] video cmd to send to hwc
 * @param cmd_data      [in/out] cmd_data
 *
 * Return of 0 means the operation completed as normal.
 * -EINVAL - invalid param
 * -EAGAIN - no reply from server (a timeout 4ms may be happened)
 */
int meson_vt_send_cmd(int fd, int tunnel_id, enum vt_cmd cmd, int cmd_data) {
    enum vt_video_cmd_e vcmd = (enum vt_video_cmd_e) cmd;

    struct vt_ctrl_data data = {
        .tunnel_id = tunnel_id,
        .ctrl_cmd = VT_CTRL_SEND_CMD,
        .video_cmd = vcmd,
        .video_cmd_data = cmd_data,
    };

    return meson_vt_ioctl(fd, VT_IOC_CTRL, &data);
}

/*
 * get video cmd from client
 *
 * @param fd            [in] Videotunnel device fd
 * @param cmd           [in] video cmd to send to hwc
 * @param cmd_data      [out] cmd_data
 * @param client_id     [out] client pid who send the cmd
 *
 * Return of 0 means the operation completed as normal.
 * -EAGAIN - no cmd from client (a timeout 4ms may be happened)
 */
int meson_vt_recv_cmd(int fd, int tunnel_id, enum vt_cmd *cmd, struct vt_cmd_data *cmd_data) {
    int ret;

    struct vt_ctrl_data data = {
        .tunnel_id = tunnel_id,
        .ctrl_cmd = VT_CTRL_RECV_CMD,
    };

    ret = meson_vt_ioctl(fd, VT_IOC_CTRL, &data);

    if (ret < 0)
        return ret;

    *cmd = (enum vt_cmd) data.video_cmd;

    if (*cmd == VT_CMD_SET_SOURCE_CROP) {
        cmd_data->crop = {
            .left = data.source_crop.left,
            .top = data.source_crop.top,
            .right = data.source_crop.right,
            .bottom = data.source_crop.bottom,
        };
    } else  {
        cmd_data->data = data.video_cmd_data;
    }

    cmd_data->client = data.client_id;

    return ret;
}

#ifdef __cplusplus
}
#endif
