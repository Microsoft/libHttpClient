// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"

#if HC_PLATFORM == HC_PLATFORM_IOS
#include <httpClient/httpClient.h>

HRESULT Internal_HCHttpPlatformInitialize(void* context)
{
    // No-op
    assert(context == nullptr);
    return S_OK;
}

HRESULT Interal_HCHttpPlatformCleanup()
{
    return S_OK;
}

void Internal_HCHttpCallPerform(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    )
{
    // TODO
}

#endif
