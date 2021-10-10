/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <MesonLog.h>
#include <FbProcessor.h>
#include "DummyProcessor.h"
#include "CopyProcessor.h"
#ifdef ENABLE_VIDEO_AISR
#include "NnProcessor.h"
#endif
#ifdef ENABLE_VIDEO_AIPQ
#include "AipqProcessor.h"
#endif

extern int32_t createKeystoneCorrection(
    std::shared_ptr<FbProcessor> & processor);

int32_t createFbProcessor(
    meson_fb_processor_t type,
    std::shared_ptr<FbProcessor> & processor) {
    int ret = 0;
    switch (type) {
        case FB_DUMMY_PROCESSOR:
            processor = std::make_shared<DummyProcessor>();
            break;
        case FB_COPY_PROCESSOR:
            processor = std::make_shared<CopyProcessor>();
            break;
#ifdef HWC_ENABLE_KEYSTONE_CORRECTION
        case FB_KEYSTONE_PROCESSOR:
            ret = createKeystoneCorrection(processor);
            break;
#endif
        // TODO: create real video processor when it ready
#ifdef ENABLE_VIDEO_AISR
        case FB_AISR_PROCESSOR:
            processor = std::make_shared<NnProcessor>();
            break;
#endif
#ifdef ENABLE_VIDEO_AIPQ
        case FB_AIPQ_PROCESSOR:
            processor = std::make_shared<AipqProcessor>();
            break;
#endif
        default:
            MESON_ASSERT(0, "unknown processor type %d", type);
            processor = NULL;
            ret = -ENODEV;
    };

    return ret;
}

