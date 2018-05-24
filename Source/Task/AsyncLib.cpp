// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#define ASYNC_STATE_SIG 0x41535445

// Used by unit tests to verify we cleanup memory correctly.
std::atomic<uint32_t> s_AsyncLibGlobalStateCount{ 0 };

struct AsyncState
{
    uint32_t signature = ASYNC_STATE_SIG;
    std::atomic<uint32_t> refs{ 1 };
    std::atomic<bool> workScheduled{ false };
    std::atomic<bool> timerScheduled{ false };
    bool canceled = false;
    AsyncProvider* provider = nullptr;
    AsyncProviderData providerData;

#ifdef _WIN32
    HANDLE waitEvent = nullptr;
    PTP_TIMER timer = nullptr;
#else
    std::mutex waitMutex;
    std::condition_variable waitCondition;
    bool waitSatisfied;
#endif

    const void* token = nullptr;
    const char* function = nullptr;

    AsyncState() noexcept
    {
        ++s_AsyncLibGlobalStateCount;
    }

    void AddRef() noexcept
    {
        ++refs;
    }

    void Release() noexcept
    {
        if (--refs == 0)
        {
            delete this;
        }
    }

private:
    ~AsyncState() noexcept
    {
#ifdef _WIN32
        if (timer != nullptr)
        {
            SetThreadpoolTimer(timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(timer, TRUE);
            CloseThreadpoolTimer(timer);
        }
#endif

        if (providerData.queue != nullptr)
        {
            CloseAsyncQueue(providerData.queue);
        }

#ifdef _WIN32
        if (waitEvent != nullptr)
        {
            CloseHandle(waitEvent);
        }
#endif

        --s_AsyncLibGlobalStateCount;
    }
};

struct AsyncBlockInternal
{
    AsyncState* state = nullptr;
    HRESULT status = E_PENDING;
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
};
static_assert(sizeof(AsyncBlockInternal) <= sizeof(AsyncBlock::internal),
    "Unexpected size for AsyncBlockInternal");
static_assert(std::alignment_of<AsyncBlockInternal>::value == std::alignment_of<void*>::value,
    "Unexpected alignment for AsyncBlockInternal");
static_assert(std::is_trivially_destructible<AsyncBlockInternal>::value,
    "Unexpected nontrivial destructor for AsyncBlockInternal");

class AsyncStateRef
{
public:
    AsyncStateRef() noexcept
        : m_state{ nullptr }
    {}
    explicit AsyncStateRef(AsyncState* state) noexcept
        : m_state{ state }
    {
        if (m_state)
        {
            m_state->AddRef();
        }
    }
    AsyncStateRef(const AsyncStateRef&) = delete;
    AsyncStateRef(AsyncStateRef&& other) noexcept
        : m_state{ other.m_state }
    {
        other.m_state = nullptr;
    }
    AsyncStateRef& operator=(const AsyncStateRef&) = delete;
    AsyncStateRef& operator=(AsyncStateRef&& other) noexcept
    {
        if (&other != this) { std::swap(m_state, other.m_state); }
        return *this;
    }
    ~AsyncStateRef() noexcept
    {
        if (m_state)
        {
            m_state->Release();
        }
    }
    AsyncState* operator->() const noexcept
    {
        return m_state;
    }
    bool operator==(nullptr_t) const noexcept
    {
        return m_state == nullptr;
    }
    bool operator!=(nullptr_t) const noexcept
    {
        return m_state != nullptr;
    }
    void Attach(AsyncState* state) noexcept
    {
        ASSERT(!m_state);
        m_state = state;
    }
    AsyncState* Detach() noexcept
    {
        AsyncState* p = m_state;
        m_state = nullptr;
        return p;
    }

    AsyncState* Get() const { return m_state; }

private:
    AsyncState * m_state;
};

class AsyncBlockInternalGuard
{
public:
    AsyncBlockInternalGuard(AsyncBlock* asyncBlock) noexcept :
        m_internal{ reinterpret_cast<AsyncBlockInternal*>(asyncBlock->internal) }
    {
        ASSERT(m_internal);
        while (m_internal->lock.test_and_set()) {}
    }

    ~AsyncBlockInternalGuard() noexcept
    {
        m_internal->lock.clear();
    }

    AsyncStateRef GetState() const noexcept
    {
        AsyncStateRef state{ m_internal->state };

        if (state != nullptr && state->signature != ASYNC_STATE_SIG)
        {
            ASSERT(false);
            return AsyncStateRef{};
        }

        return state;
    }

    AsyncStateRef ExtractState() const noexcept
    {
        AsyncStateRef state{ m_internal->state };
        m_internal->state = nullptr;

        if (state != nullptr && state->signature != ASYNC_STATE_SIG)
        {
            ASSERT(false);
            return AsyncStateRef{};
        }

        return state;
    }

    HRESULT GetStatus() const noexcept
    {
        return m_internal->status;
    }

    bool TrySetTerminalStatus(HRESULT status) noexcept
    {
        if (m_internal->status == E_PENDING)
        {
            m_internal->status = status;
            return true;
        }
        else
        {
            return false;
        }
    }

private:
    AsyncBlockInternal * const m_internal;
};

static void CALLBACK CompletionCallbackForAsyncState(_In_ void* context);
static void CALLBACK CompletionCallbackForAsyncBlock(_In_ void* context);
static void CALLBACK WorkerCallback(_In_ void* context);
static void SignalCompletion(_In_ AsyncStateRef const& state);
static void SignalWait(_In_ AsyncStateRef const& state);
static HRESULT AllocStateNoCompletion(_Inout_ AsyncBlock* asyncBlock, _In_ AsyncBlockInternal* internal);
static HRESULT AllocState(_Inout_ AsyncBlock* asyncBlock);
static void CleanupState(_Inout_ AsyncStateRef&& state);

#ifdef _WIN32
static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE, _In_ void* context, _In_ PTP_TIMER);
#endif

static HRESULT AllocStateNoCompletion(_Inout_ AsyncBlock* asyncBlock, _In_ AsyncBlockInternal* internal)
{
    AsyncStateRef state;
    state.Attach(new (std::nothrow) AsyncState);

    RETURN_IF_NULL_ALLOC(state);

    if (asyncBlock->waitEvent != nullptr)
    {
#if _WIN32
        RETURN_IF_WIN32_BOOL_FALSE(DuplicateHandle(
            GetCurrentProcess(),
            asyncBlock->waitEvent,
            GetCurrentProcess(),
            &state->waitEvent, 0,
            FALSE,
            DUPLICATE_SAME_ACCESS));
#else
        ASSERT(false);
#endif
    }
    else
    {
#if _WIN32
        state->waitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        RETURN_LAST_ERROR_IF_NULL(state->waitEvent);
#endif
    }

    state->providerData.queue = asyncBlock->queue;
    state->providerData.async = asyncBlock;

    if (state->providerData.queue != nullptr)
    {
        ReferenceAsyncQueue(state->providerData.queue);
    }
    else
    {
#if _WIN32
        RETURN_IF_FAILED(
            CreateSharedAsyncQueue(
            (uint32_t)GetCurrentThreadId(),
                AsyncQueueDispatchMode_ThreadPool,
                AsyncQueueDispatchMode_FixedThread,
                &state->providerData.queue));
#else
        RETURN_HR(E_INVALIDARG);
#endif
    }

    internal->state = state.Detach();

    return S_OK;
}

static HRESULT AllocState(_Inout_ AsyncBlock* asyncBlock)
{
    // If the async block is already associated with another
    // call, fail.

    // There is no great way to tell if the AsyncBlockInternal has already been
    // initialized, because uninitialized memory can look like anything.
    // Here we rely on the client zeoring out the entirety of the AsyncBlock
    // object so we can check that AsyncBlock::Internal is all 0
    for (auto i = 0u; i < sizeof(asyncBlock->internal); ++i)
    {
        if (asyncBlock->internal[i] != 0)
        {
            return E_INVALIDARG;
        }
    }

    // Construction is inherently single threaded
    // (there is nothing we can do if the client tries to use the same
    // AsyncBlock in 2 calls at the same time)
    auto internal = new (asyncBlock->internal) AsyncBlockInternal{};

    HRESULT hr = AllocStateNoCompletion(asyncBlock, internal);

    if (FAILED(hr))
    {
        // Attempt to complete the call as a failure, and only return
        // a failed HR here if we couldn't complete.

        internal->status = hr;

        if (asyncBlock->queue != nullptr && asyncBlock->callback != nullptr)
        {
            hr = SubmitAsyncCallback(
                asyncBlock->queue,
                AsyncQueueCallbackType_Completion,
                asyncBlock,
                CompletionCallbackForAsyncBlock);
        }

        // The completion callback will signal the event
        if (FAILED(hr) && asyncBlock->waitEvent != nullptr)
        {
#if _WIN32
            SetEvent(asyncBlock->waitEvent);
#else
            ASSERT(false);
#endif
        }

    }

    return hr;
}

static void CleanupState(_Inout_ AsyncStateRef&& state)
{

    if (state != nullptr)
    {
        (void)state->provider(AsyncOp_Cleanup, &state->providerData);

        auto removePredicate = [](void* pCxt, void* cCxt)
        {
            if (pCxt == cCxt)
            {
                AsyncState* state = static_cast<AsyncState*>(pCxt);
                state->Release();
                return true;
            }
            return false;
        };

        RemoveAsyncQueueCallbacks(state->providerData.queue, AsyncQueueCallbackType_Work, WorkerCallback, state.Get(), removePredicate);

        state->Release();
    }
}

static void SignalCompletion(_In_ AsyncStateRef const& state)
{
    if (state->providerData.async->callback != nullptr)
    {
        AsyncStateRef callbackState(state.Get());

        HRESULT hr = SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Completion,
            callbackState.Get(),
            CompletionCallbackForAsyncState);

        if (SUCCEEDED(hr))
        {
            callbackState.Detach();
        }
        else
        {
            FAIL_FAST_MSG("Failed to submit competion callback: 0x%08x", hr);
        }
    }
    else
    {
        SignalWait(state);
    }
}

static void SignalWait(_In_ AsyncStateRef const& state)
{
#if _WIN32
    if (state->waitEvent)
    {
        SetEvent(state->waitEvent);
    }
#else
    {
        std::lock_guard<std::mutex> lock(state->waitMutex);
        state->waitSatisfied = true;
    }
    state->waitCondition.notify_all();
#endif
}

static void CALLBACK CompletionCallbackForAsyncBlock(
    _In_ void* context)
{
    AsyncBlock* asyncBlock = static_cast<AsyncBlock*>(context);
    if (asyncBlock->callback != nullptr)
    {
        asyncBlock->callback(asyncBlock);
    }
    if (asyncBlock->waitEvent)
    {
#if _WIN32
        SetEvent(asyncBlock->waitEvent);
#else
        ASSERT(false);
#endif
    }
}

static void CALLBACK CompletionCallbackForAsyncState(
    _In_ void* context)
{
    AsyncStateRef state;
    state.Attach(static_cast<AsyncState*>(context));

    AsyncBlock* asyncBlock = state->providerData.async;
    if (asyncBlock->callback != nullptr)
    {
        asyncBlock->callback(asyncBlock);
    }

    SignalWait(state);
}

static void CALLBACK WorkerCallback(
    _In_ void* context)
{
    AsyncStateRef state;
    state.Attach(static_cast<AsyncState*>(context));
    AsyncBlock* asyncBlock = state->providerData.async;
    state->workScheduled = false;

    if (state->canceled)
    {
        return;
    }

    HRESULT result = state->provider(AsyncOp_DoWork, &state->providerData);

    // Work routine can return E_PENDING if there is more work to do.  Otherwise
    // it either needs to be a failure or it should have called CompleteAsync, which
    // would have set a new value into the status.
    if (result != E_PENDING && !state->canceled)
    {
        if (SUCCEEDED(result))
        {
            result = E_UNEXPECTED;
        }

        bool completedNow = false;
        {
            AsyncBlockInternalGuard internal{ asyncBlock };
            completedNow = internal.TrySetTerminalStatus(result);
        }
        if (completedNow)
        {
            SignalCompletion(state);
        }
    }
}

#if _WIN32
static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE, _In_ void* context, _In_ PTP_TIMER)
{
    AsyncStateRef state;
    state.Attach(static_cast<AsyncState*>(context));
    state->timerScheduled = false;

    if (state->canceled)
    {
        return;
    }

    HRESULT hr = SubmitAsyncCallback(
        state->providerData.queue,
        AsyncQueueCallbackType_Work,
        state.Get(),
        WorkerCallback);

    if (SUCCEEDED(hr))
    {
        state.Detach(); // state is still in use so let it go
    }
    else
    {
        CompleteAsync(state->providerData.async, hr, 0);
    }
}
#endif

//
// Public APIs
//

/// <summary>
/// Returns the status of the asynchronous operation, optionally waiting
/// for it to complete.  Once complete, you may call GetAsyncResult if
/// the async call has a resulting data payload. If it doesn't, calling
/// GetAsyncResult is unneeded.
/// </summary>
STDAPI GetAsyncStatus(
    _In_ AsyncBlock* asyncBlock,
    _In_ bool wait)
{
    HRESULT result = E_PENDING;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        result = internal.GetStatus();
        state = internal.GetState();
    }

    // If we are being asked to wait, always check the wait state before
    // looking at the hresult.  Our wait waits until the completion runs
    // so we may need to wait past when the status is set.

    if (wait)
    {
        if (state == nullptr)
        {
            ASSERT(result != E_PENDING);
            RETURN_HR_IF(E_INVALIDARG, result == E_PENDING);
        }
        else
        {
#if _WIN32
            DWORD waitResult;
            do
            {
                waitResult = WaitForSingleObjectEx(state->waitEvent, INFINITE, TRUE);
            } while (waitResult == WAIT_IO_COMPLETION);

            if (waitResult == WAIT_OBJECT_0)
            {
                result = GetAsyncStatus(asyncBlock, false);
            }
            else
            {
                result = HRESULT_FROM_WIN32(GetLastError());
            }
#else
            {
                std::unique_lock<std::mutex> lock(state->waitMutex);

                if (!state->waitSatisfied)
                {
                    AsyncState* s = state.Get();
                    state->waitCondition.wait(lock, [s] { return s->waitSatisfied; });
                }
            }

            result = GetAsyncStatus(asyncBlock, false);
#endif
        }
    }

    return result;
}

/// <summary>
/// Returns the required size of the buffer to pass to GetAsyncResult.
/// </summary>
STDAPI GetAsyncResultSize(
    _In_ AsyncBlock* asyncBlock,
    _Out_ size_t* bufferSize)
{
    HRESULT result = E_PENDING;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        result = internal.GetStatus();
        state = internal.GetState();
    }

    if (SUCCEEDED(result))
    {
        ASSERT(state != nullptr);
        RETURN_HR_IF(E_INVALIDARG, state == nullptr);

        *bufferSize = state->providerData.bufferSize;
    }

    return result;
}

/// <summary>
/// Cancels an asynchronous operation. The status result will return E_ABORT,
/// the completion callback will be invoked and the event in the async block will be
/// signaled.
/// </summary>
STDAPI_(void) CancelAsync(
    _Inout_ AsyncBlock* asyncBlock)
{
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        if (!internal.TrySetTerminalStatus(E_ABORT))
        {
            return;
        }
        state = internal.ExtractState();
        state->canceled = true;
    }

#if _WIN32
    if (state->timer != nullptr)
    {
        SetThreadpoolTimer(state->timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(state->timer, TRUE);

        if (state->timerScheduled)
        {
            // The timer callback was never invoked so release this reference
            state->Release();
        }
    }
#endif

    (void)state->provider(AsyncOp_Cancel, &state->providerData);
    SignalCompletion(state);
    // At this point asyncBlock is unsafe to touch

    CleanupState(std::move(state));
}

/// <summary>
/// Runs the given callback asynchronously.
/// </summary>
STDAPI RunAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ AsyncWork* work)
{
    RETURN_IF_FAILED(BeginAsync(
        asyncBlock,
        reinterpret_cast<void*>(work),
        reinterpret_cast<void*>(RunAsync),
        __FUNCTION__,
        [](AsyncOp op, const AsyncProviderData* data)
    {
        if (op == AsyncOp_DoWork)
        {
            AsyncWork* work = reinterpret_cast<AsyncWork*>(data->context);
            HRESULT hr = work(data->async);
            CompleteAsync(data->async, hr, 0);
        }
        return S_OK;
    }));

    RETURN_HR(ScheduleAsync(asyncBlock, 0));
}

//
// AsyncProvider APIs
//

/// <summary>
/// Initializes an async block for use.  Once begun calls such
/// as GetAsyncStatus will provide meaningful data. It is assumed the
/// async work will begin on some system defined thread after this call
/// returns.
/// </summary>
STDAPI BeginAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ const void* token,
    _In_opt_ const char* function,
    _In_ AsyncProvider* provider)
{
    RETURN_IF_FAILED(AllocState(asyncBlock));

    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        state = internal.GetState();
    }
    state->provider = provider;
    state->providerData.context = context;
    state->token = token;
    state->function = function;

    return S_OK;
}

/// <summary>
/// Schedules a callback to do async work.  Calling this is optional.  If the async work can be done
/// through a system - async mechanism like overlapped IO or async COM, there is no need to schedule
/// work.  If work should be scheduled after a delay, pass the number of ms ScheduleAsync should wait
/// before it schedules work.
/// </summary>
STDAPI ScheduleAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ uint32_t delayInMs)
{
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        state = internal.GetState();
    }
    RETURN_HR_IF(E_INVALIDARG, state == nullptr);

    if (delayInMs != 0)
    {
#if _WIN32
        if (state->timer == nullptr)
        {
            state->timer = CreateThreadpoolTimer(TimerCallback, state.Get(), nullptr);
            RETURN_LAST_ERROR_IF_NULL(state->timer);
        }
#else
        ASSERT(false);
        RETURN_HR(E_INVALIDARG);
#endif
    }

    bool priorScheduled = false;
    state->workScheduled.compare_exchange_strong(priorScheduled, true);

    if (priorScheduled)
    {
        RETURN_HR(E_UNEXPECTED);
    }

    if (delayInMs == 0)
    {
        RETURN_IF_FAILED(SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Work,
            state.Get(),
            WorkerCallback));
    }
    else
    {
#ifdef _WIN32
        int64_t ft = (int64_t)delayInMs * (int64_t)(-10000);
        state->timerScheduled = true;
        SetThreadpoolTimer(state->timer, (PFILETIME)&ft, 0, delayInMs);
#else
        RETURN_HR(E_INVALIDARG);
#endif
    }

    // State object is now referenced by the work or timer callback
    state->AddRef();
    return S_OK;
}

/// <summary>
/// Called when async work is completed and the results can be returned.
/// The caller should supply the resulting data payload size.  If the call
/// has no data payload, pass zero.
/// </summary
STDAPI_(void) CompleteAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ HRESULT result,
    _In_ size_t requiredBufferSize)
{
    // E_PENDING is special -- if you still have work to do don't complete.

    if (result == E_PENDING)
    {
        return;
    }

    bool completedNow = false;
    bool doCleanup = false;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        HRESULT priorStatus = internal.GetStatus();

        completedNow = internal.TrySetTerminalStatus(result);

        // If the required buffer is zero, there is no payload and we need to
        // clean up now. Also clean up if the status is abort, as that indicates
        // the user cancelled the call.
        if (requiredBufferSize == 0 || priorStatus == E_ABORT)
        {
            // If we are going to cleanup steal the reference from the block.
            doCleanup = true;
            state = internal.ExtractState();
        }
        else
        {
            state = internal.GetState();
        }
    }

    // If prior status was not pending, we either already completed or were canceled.
    if (completedNow)
    {
        state->providerData.bufferSize = requiredBufferSize;
        SignalCompletion(state);
    }
    // At this point asyncBlock may be unsafe to touch

    if (doCleanup)
    {
        CleanupState(std::move(state));
    }
}

/// <summary>
/// Returns the result data for the asynchronous operation.  After this call
/// the async block is completed and no longer associated with the
/// operation.
/// </summary>
STDAPI GetAsyncResult(
    _Inout_ AsyncBlock* asyncBlock,
    _In_opt_ const void* token,
    _In_ size_t bufferSize,
    _Out_writes_bytes_to_opt_(bufferSize, *bufferUsed) void* buffer,
    _Out_opt_ size_t* bufferUsed)
{
    HRESULT result = E_PENDING;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        result = internal.GetStatus();
        state = internal.ExtractState();
    }

    if (SUCCEEDED(result))
    {
        if (state == nullptr)
        {
            result = E_INVALIDARG;
        }
        else if (token != state->token)
        {
            // Call/Result mismatch.  This AsyncBlock was initiated by state->function
            char buf[100];
            int sprintfResult;
            if (state->function != nullptr)
            {
                sprintfResult = snprintf(
                    buf,
                    sizeof(buf),
                    "Call/Result mismatch.  This AsyncBlock was initiated by '%s'.\r\n",
                    state->function);
            }
            else
            {
                sprintfResult = snprintf(buf, sizeof(buf), "Call/Result mismatch\r\n");
            }

            result = E_INVALIDARG;
            ASYNC_LIB_TRACE(result, buf);
            ASSERT(false);
            ASSERT(sprintfResult > 0);
        }
        else if (state->providerData.bufferSize == 0)
        {
            // Caller has not supplied a payload
            result = E_NOT_SUPPORTED;
        }
        else if (buffer == nullptr)
        {
            return E_INVALIDARG;
        }
        else if (bufferSize < state->providerData.bufferSize)
        {
            return E_NOT_SUFFICIENT_BUFFER;
        }
        else
        {
            if (bufferUsed != nullptr)
            {
                *bufferUsed = state->providerData.bufferSize;
            }

            state->providerData.bufferSize = bufferSize;
            state->providerData.buffer = buffer;
            result = state->provider(AsyncOp_GetResult, &state->providerData);
        }
    }

    // Cleanup state if needed
    if (result != E_PENDING)
    {
        CleanupState(std::move(state));
    }

    return result;
}
