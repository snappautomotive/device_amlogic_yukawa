/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// #define LOG_NDEBUG 0
#define LOG_TAG "C2YukawaAllocatorFactory"

#include <C2AllocatorGralloc.h>
#include <utils/Log.h>
#include <v4l2_codec2/plugin_store/V4L2AllocatorId.h>

namespace android {

::C2Allocator* CreateYukawaAllocator(::C2Allocator::id_t allocatorId) {
    ALOGW("%s(%d)", __func__, allocatorId);

    switch (allocatorId) {
        case V4L2AllocatorId::V4L2_BUFFERQUEUE:
            return new C2AllocatorGralloc(V4L2AllocatorId::V4L2_BUFFERQUEUE, true);
        case V4L2AllocatorId::V4L2_BUFFERPOOL:
            return new C2AllocatorGralloc(V4L2AllocatorId::V4L2_BUFFERPOOL, true);
        default:
            ALOGE("%s(): Unknown allocator ID: %d", __func__, allocatorId);
    }
    return nullptr;
}

}  // namespace android

extern "C" ::C2Allocator* CreateAllocator(::C2Allocator::id_t allocatorId) {
    ALOGW("%s(%d)", __func__, allocatorId);
    return ::android::CreateYukawaAllocator(allocatorId);
}
