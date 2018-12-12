// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

// Base class for platform specific implementations
struct hc_websocket_impl 
{
    hc_websocket_impl() {}
    virtual ~hc_websocket_impl() {}
};

typedef struct HC_WEBSOCKET
{
    HC_WEBSOCKET() :
        id(0),
        refCount(1),
        connectCalled(false)
    {
    }

    uint64_t id;
    std::atomic<int> refCount;
    bool connectCalled;
    http_internal_map<http_internal_string, http_internal_string> connectHeaders;
    http_internal_string proxyUri;
    http_internal_string uri;
    http_internal_string subProtocol;
    std::shared_ptr<hc_websocket_impl> impl;
} HC_WEBSOCKET;

HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ hc_websocket_handle_t websocket,
    _Inout_ XAsyncBlock* asyncBlock
    );

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
    );

HRESULT CALLBACK Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle_t websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    );
