// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../http/httpcall.h"
#include "buildver.h"
#include "singleton.h"
#include "log.h"
#include "debug_output.h"

using namespace xbox::httpclient;

static const uint32_t DEFAULT_TIMEOUT_WINDOW_IN_SECONDS = 20;
static const uint32_t DEFAULT_RETRY_DELAY_IN_SECONDS = 2;

static std::mutex g_httpSingletonLock;
static std::unique_ptr<http_singleton> g_httpSingleton;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

http_singleton::http_singleton() 
{
    m_lastHttpCallId = 0;
    m_loggingHandlersCounter = 0;
    m_performFunc = Internal_HCHttpCallPerform;
    m_logger = std::make_shared<xbox::httpclient::log::logger>();
    m_logger->add_log_output(std::make_shared<xbox::httpclient::log::debug_output>());
    m_logger->set_log_level(HC_LOG_LEVEL::LOG_OFF);
    m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    m_retryDelayInSeconds = DEFAULT_RETRY_DELAY_IN_SECONDS;
    m_enableAssertsForThrottling = true;
    m_mocksEnabled = false;
    m_lastMatchingMock = nullptr;
    m_pendingReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
}

http_singleton::~http_singleton()
{
}

http_singleton*
get_http_singleton(_In_ bool createIfRequired)
{
    if (createIfRequired)
    {
        std::lock_guard<std::mutex> guard(g_httpSingletonLock);
        if (g_httpSingleton == nullptr)
        {
            g_httpSingleton = std::make_unique<http_singleton>();
        }
    }

    return g_httpSingleton.get();
}

void cleanup_http_singleton()
{
    std::lock_guard<std::mutex> guard(g_httpSingletonLock);
    for (auto& mockCall : g_httpSingleton->m_mocks)
    {
        HCHttpCallCleanup(mockCall);
    }
    g_httpSingleton->m_mocks.clear();

    g_httpSingleton = nullptr;
}

std::shared_ptr<http_task_completed_queue> http_singleton::get_task_completed_queue_for_taskgroup(_In_ uint64_t taskGroupId)
{
    std::lock_guard<std::mutex> lock(m_taskCompletedQueueLock);
    auto it = m_taskCompletedQueue.find(taskGroupId);
    if (it != m_taskCompletedQueue.end())
    {
        return it->second;
    }

    std::shared_ptr<http_task_completed_queue> taskQueue = std::make_shared<http_task_completed_queue>();
    taskQueue->m_completeReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));

    m_taskCompletedQueue[taskGroupId] = taskQueue;
    return taskQueue;
}

function_context http_singleton::add_logging_handler(_In_ std::function<void(HC_LOG_LEVEL, const std::string&, const std::string&)> handler)
{
    std::lock_guard<std::mutex> lock(m_loggingWriteLock);

    function_context context = -1;
    if (handler != nullptr)
    {
        context = ++m_loggingHandlersCounter;
        m_loggingHandlers[m_loggingHandlersCounter] = std::move(handler);
    }

    return context;
}

void http_singleton::remove_logging_handler(_In_ function_context context)
{
    std::lock_guard<std::mutex> lock(m_loggingWriteLock);
    m_loggingHandlers.erase(context);
}

void http_singleton::raise_logging_event(_In_ HC_LOG_LEVEL level, _In_ const std::string& category, _In_ const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_loggingWriteLock);

    for (auto& handler : m_loggingHandlers)
    {
        HC_ASSERT(handler.second != nullptr);
        if (handler.second != nullptr)
        {
            try
            {
                handler.second(level, category, message);
            }
            catch (...)
            {
            }
        }
    }
}

void verify_http_singleton()
{
#if ENABLE_ASSERTS
    if (g_httpSingleton == nullptr)
    {
#if ENABLE_LOGS
        LOG_ERROR("Call HCGlobalInitialize() first");
#endif
        assert(g_httpSingleton != nullptr);
    }
#endif
}

#if UWP_API
HANDLE http_singleton::get_pending_ready_handle()
{
    return m_pendingReadyHandle.get();
}

void http_singleton::set_task_pending_ready()
{
    SetEvent(get_pending_ready_handle());
}
#endif

#if UWP_API
HANDLE http_task_completed_queue::get_complete_ready_handle()
{
    return m_completeReadyHandle.get();
}

void http_task_completed_queue::set_task_completed_event()
{
    SetEvent(get_complete_ready_handle());
}
#endif

http_internal_queue<HC_TASK*>& http_task_completed_queue::get_completed_queue()
{
    return m_completedQueue;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
