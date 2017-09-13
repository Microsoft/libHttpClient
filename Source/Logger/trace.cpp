// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpclient/trace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace
{

//------------------------------------------------------------------------------
// CHAR overloads for printf style functions used in tracing
//------------------------------------------------------------------------------
#if HC_CHAR_IS_WIDE

template<size_t SIZE>
int stprintf_s(wCHAR(&buffer)[SIZE], _Printf_format_string_ wCHAR const* format, ...)
{
    va_list varArgs = nullptr;
    va_start(varArgs, format);
    auto result = vswprintf_s(buffer, format, varArgs);
    va_end(varArgs);
    return result;
}

int stprintf_s(wCHAR* buffer, size_t size, _Printf_format_string_ wCHAR const* format, ...)
{
    va_list varArgs = nullptr;
    va_start(varArgs, format);
    auto result = vswprintf_s(buffer, size, format, varArgs);
    va_end(varArgs);
    return result;
}

template<size_t SIZE>
int vstprintf_s(wCHAR(&buffer)[SIZE], _Printf_format_string_ wCHAR const* format, va_list varArgs)
{
    return vswprintf_s(buffer, format, varArgs);
}

void OutputDebugStringT(wCHAR const* string)
{
    OutputDebugStringW(string);
}

#else

template<size_t SIZE>
int stprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format ...)
{
    va_list varArgs = nullptr;
    va_start(varArgs, format);
    auto result = vsprintf_s(buffer, format, varArgs);
    va_end(varArgs);
    return result;
}

int stprintf_s(char* buffer, size_t size, _Printf_format_string_ char const* format ...)
{
    va_list varArgs = nullptr;
    va_start(varArgs, format);
    auto result = vsprintf_s(buffer, size, format, varArgs);
    va_end(varArgs);
    return result;
}

template<size_t SIZE>
int vstprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format, va_list varArgs)
{
    return vsprintf_s(buffer, format, varArgs);
}

void OutputDebugStringT(char const* string)
{
    OutputDebugStringA(string);
}

#endif

//------------------------------------------------------------------------------
// Trace implementation
//------------------------------------------------------------------------------
class TraceState
{
public:
    void Init()
    {
        auto previousCount = m_tracingClients.fetch_add(1);
        if (previousCount == 0)
        {
            m_initTime = std::chrono::high_resolution_clock::now();
        }
    }

    void Cleanup()
    {
        --m_tracingClients;
    }

    bool IsSetup() const
    {
        return m_tracingClients > 0;
    }

    void SetClientCallback(HCTraceCallback* callback)
    {
        m_clientCallback = callback;
    }

    HCTraceCallback* GetClientCallback() const
    {
        return m_clientCallback;
    }

    uint64_t GetTimestamp() const
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto nowMS = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_initTime.load());
        return nowMS.count();
    }

private:
    std::atomic<uint32_t> m_tracingClients = 0;
    std::atomic<std::chrono::high_resolution_clock::time_point> m_initTime =
        std::chrono::high_resolution_clock::now();
    std::atomic<HCTraceCallback*> m_clientCallback = nullptr;
};

TraceState& GetTraceState()
{
    static TraceState state;
    return state;
}

void TraceMessageToDebugger(
    char const* areaName,
    HCTraceLevel level,
    unsigned int threadId,
    uint64_t timestamp,
    char const* message
)
{
#if HC_TRACE_TO_DEBUGGER
    // Needs to match the HCTraceLevel enum
    static char const* traceLevelNames[] =
    {
        "Off",
        "E",
        "W",
        "P",
        "I",
        "V",
    };

    static size_t const BUFFER_SIZE = 4096;

    std::time_t  timeTInSec = static_cast<std::time_t>(timestamp / 1000);
    uint32_t     fractionMSec = static_cast<uint32_t>(timestamp % 1000);
    std::tm      fmtTime = {};
    localtime_s(&fmtTime, &timeTInSec);

    CHAR outputBuffer[BUFFER_SIZE] = {};
    // [threadId][level][time][area] message
    auto written = stprintf_s(outputBuffer, "[%04X][%s][%02d:%02d:%02d.%03u][%s] %s",
        threadId,
        traceLevelNames[static_cast<size_t>(level)],
        fmtTime.tm_hour,
        fmtTime.tm_min,
        fmtTime.tm_sec,
        fractionMSec,
        areaName,
        message
    );
    if (written <= 0)
    {
        return;
    }

    // Make sure there is room for the \r \n and \0
    written = std::min(written, static_cast<int>(BUFFER_SIZE - 3));
    auto remaining = BUFFER_SIZE - written;

    // Print new line
    auto written2 = stprintf_s(outputBuffer + written, remaining, "\r\n");
    if (written2 <= 0)
    {
        return;
    }

    OutputDebugStringT(outputBuffer);
#else
    (void)areaName;
    (void)level;
    (void)threadId;
    (void)timestamp;
    (void)message;
#endif
}

void TraceMessageToClient(
    CHAR const* areaName,
    HCTraceLevel level,
    unsigned int threadId,
    uint64_t timestamp,
    CHAR const* message
)
{
#if HC_TRACE_TO_CLIENT
    auto callback = GetTraceState().GetClientCallback();
    if (callback)
    {
        callback(areaName, level, threadId, timestamp, message);
    }
#else
    (void)areaName;
    (void)level;
    (void)threadId;
    (void)timestamp;
    (void)message;
#endif
}

unsigned long long GetScopeId()
{
    LARGE_INTEGER li = {};
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

}

void HCTraceSetClientCallback(HCTraceCallback* callback)
{
    GetTraceState().SetClientCallback(callback);
}

void HCTraceImplMessage(
    struct HCTraceImplArea const* area,
    enum HCTraceLevel level,
    _Printf_format_string_ CHAR const* format,
    ...
)
{
    if (!area)
    {
        return;
    }

    if (level > area->Verbosity)
    {
        return;
    }

    if (!GetTraceState().IsSetup())
    {
        return;
    }

    if (!format)
    {
        return;
    }

    auto timestamp = GetTraceState().GetTimestamp();
    auto threadId = GetCurrentThreadId();

    char message[4096] = {};

    va_list varArgs = nullptr;
    va_start(varArgs, format);
    auto result = vstprintf_s(message, format, varArgs);
    va_end(varArgs);

    if (result < 0)
    {
        return;
    }

    TraceMessageToDebugger(area->Name, level, threadId, timestamp, message);
    TraceMessageToClient(area->Name, level, threadId, timestamp, message);
}

HCTraceImplScopeHelper::HCTraceImplScopeHelper(HCTraceImplArea const* area, HCTraceLevel level, CHAR const* scope)
    : m_area{ area }, m_level{ level }, m_scope{ scope }, m_id{ GetScopeId() }
{
    HCTraceImplMessage(m_area, m_level, ">>> %s (%016llX)", m_scope, m_id);
}

HCTraceImplScopeHelper::~HCTraceImplScopeHelper()
{
    HCTraceImplMessage(m_area, m_level, "<<< %s (%016llX)", m_scope, m_id);
}

// trace_internal.h

void HCTraceImplGlobalInit()
{
    GetTraceState().Init();
}

void HCTraceImplGlobalCleanup()
{
    GetTraceState().Cleanup();
}
