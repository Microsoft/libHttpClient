// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "stdafx.h"
#include "httpClient\httpClient.h"

std::vector<std::vector<std::string>> ExtractAllHeaders(_In_ HC_CALL_HANDLE call)
{
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);

    std::vector< std::vector<std::string> > headers;
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* str;
        const char* str2;
        std::string headerName;
        std::string headerValue;
        HCHttpCallResponseGetHeaderAtIndex(call, i, &str, &str2);
        if (str != nullptr) headerName = str;
        if (str2 != nullptr) headerValue = str2;
        std::vector<std::string> header;
        header.push_back(headerName);
        header.push_back(headerValue);

        headers.push_back(header);
    }

    return headers;
}

class win32_handle
{
public:
    win32_handle() : m_handle(nullptr)
    {
    }

    ~win32_handle()
    {
        if( m_handle != nullptr ) CloseHandle(m_handle);
        m_handle = nullptr;
    }

    void set(HANDLE handle)
    {
        m_handle = handle;
    }

    HANDLE get() { return m_handle; }

private:
    HANDLE m_handle;
};

win32_handle g_stopRequestedHandle;
DWORD g_targetNumThreads = 2;
HANDLE g_hActiveThreads[10] = { 0 };
DWORD g_defaultIdealProcessor = 0;
DWORD g_numActiveThreads = 0;

DWORD WINAPI http_thread_proc(LPVOID lpParam)
{
    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForSingleObject(g_stopRequestedHandle.get(), 20);
        switch (dwResult)
        {
        case WAIT_TIMEOUT: // pending ready
            HCTaskProcessNextPendingTask();
            break;

        default:
            stop = true;
            break;
        }
    }

    return 0;
}

void InitBackgroundThread()
{
    g_stopRequestedHandle.set(CreateEvent(nullptr, false, false, nullptr));
    for (uint32_t i = 0; i < g_targetNumThreads; i++)
    {
        g_hActiveThreads[i] = CreateThread(nullptr, 0, http_thread_proc, nullptr, 0, nullptr);
        if (g_defaultIdealProcessor != MAXIMUM_PROCESSORS)
        {
            if (g_hActiveThreads[i] != nullptr)
            {
                SetThreadIdealProcessor(g_hActiveThreads[i], g_defaultIdealProcessor);
            }
        }
    }

    g_numActiveThreads = g_targetNumThreads;
}

void ShutdownActiveThreads()
{
    SetEvent(g_stopRequestedHandle.get());
    DWORD dwResult = WaitForMultipleObjectsEx(g_numActiveThreads, g_hActiveThreads, true, INFINITE, false);
    if (dwResult >= WAIT_OBJECT_0 && dwResult <= WAIT_OBJECT_0 + g_numActiveThreads - 1)
    {
        for (DWORD i = 0; i < g_numActiveThreads; i++)
        {
            CloseHandle(g_hActiveThreads[i]);
            g_hActiveThreads[i] = nullptr;
        }
        g_numActiveThreads = 0;
        ResetEvent(g_stopRequestedHandle.get());
    }
}

int main()
{
    std::string method = "GET";
    std::string url = "http://www.bing.com";
    std::string requestBody = "";
    bool retryAllowed = true;
    std::vector<std::vector<std::string>> headers;
    std::vector< std::string > header;

    header.clear();
    header.push_back("User-Agent");
    header.push_back("libHttpClient");
    headers.push_back(header);

    HCGlobalInitialize();

    InitBackgroundThread();

    HC_CALL_HANDLE call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, method.c_str(), url.c_str());
    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, retryAllowed);
    for (auto& header : headers)
    {
        std::string headerName = header[0];
        std::string headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str());
    }

    printf_s("Calling %s %s\r\n", method.c_str(), url.c_str());

    uint64_t taskGroupId = 0;
    HC_TASK_HANDLE taskHandle;
    HCHttpCallPerform(&taskHandle, taskGroupId, call, nullptr,
        [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            const char* str;
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            std::string responseString;
            std::string errMessage;

            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &str);
            if (str != nullptr) responseString = str;
            std::vector<std::vector<std::string>> headers = ExtractAllHeaders(call);

            HCHttpCallCleanup(call);

            printf_s("HTTP call done\r\n");
            printf_s("Network error code: %d\r\n", errCode);
            printf_s("Http status code: %d\r\n", statusCode);

            int i = 0;
            for (auto& header : headers)
            {
                printf_s("Header[%d] '%s'='%s'\r\n", i, header[0].c_str(), header[1].c_str());
                i++;
            }

            if (responseString.length() > 200)
            {
                std::string subResponseString = responseString.substr(0, 200);
                printf_s("Response string:\r\n%s...\r\n", subResponseString.c_str());
            }
            else
            {
                printf_s("Response string:\r\n%s\r\n", responseString.c_str());
            }
        });

    HCTaskWaitForCompleted(taskHandle, 1000*1000);
    HCTaskProcessNextCompletedTask(0);
    HCGlobalCleanup();

    return 0;
}

