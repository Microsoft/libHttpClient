// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#if !defined(__cplusplus)
    #error C++11 required
#endif

#include <jni.h>

STDAPI XTaskQueueSetJvm(_In_ JavaVM* jvm) noexcept;
