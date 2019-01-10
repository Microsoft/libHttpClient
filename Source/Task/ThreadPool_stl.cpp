#include "pch.h"
#include "ThreadPool.h"

class ThreadPoolImpl
{
public:

    ~ThreadPoolImpl() noexcept
    {
        Terminate();
    }

    void AddRef()
    {
        m_refs++;
    }

    void Release()
    {
        if (--m_refs == 0)
        {
            delete this;
        }
    }

    HRESULT Initialize(
        _In_opt_ void* context,
        _In_ ThreadPoolCallback* callback) noexcept
    {
        m_context = context;
        m_callback = callback;

        uint32_t numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0)
        {
            numThreads = 1;
        }

        try
        {
            while(numThreads != 0)
            {
                numThreads--;
                m_pool.emplace_back(std::thread([this]
                {
                    std::unique_lock<std::mutex> lock(m_wakeLock);
                    while(true)
                    {
                        m_wake.wait(lock);

                        if (m_terminate)
                        {
                            break;
                        }

                        if (m_calls != 0)
                        {
                            m_calls--;

                            // ActionComplete is an optional call
                            // the callback can make to indicate 
                            // all portions of the call have finished
                            // and it is safe to release the
                            // thread pool, even if the callback has
                            // not totally unwound.  This is neccessary
                            // to allow users to close a task queue from
                            // within a callback.  Task queue guards with an 
                            // extra ref to ensure a safe point where 
                            // member state is no longer accessed, but the
                            // final release does need to wait on outstanding
                            // calls.
                            
                            ActionCompleteImpl ac(this);

                            lock.unlock();
                            AddRef();
                            m_callback(m_context, ac);
                            lock.lock();

                            if (!ac.Invoked)
                            {
                                ac();
                            }

                            if (m_terminate)
                            {
                                lock.unlock();
                                Release(); // This could destroy us
                                break;
                            }
                            else
                            {
                                Release();
                            }
                        }
                    }
                }));
            }
        }
        catch(const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }

    void Terminate() noexcept
    {
        std::unique_lock<std::mutex> lock(m_activeLock);
        m_terminate = true;
        m_wake.notify_all();

        // Wait for the active call count
        // to go to zero.
        while (m_activeCalls != 0)
        {
            m_active.wait(lock);
        }
        lock.unlock();

        for (auto &t : m_pool)
        {
            if (t.get_id() == std::this_thread::get_id())
            {
                t.detach();
            }
            else
            {
                t.join();
            }
        }

        m_pool.clear();
    }

    void Submit() noexcept
    {
        m_calls++;
        m_activeCalls++;
        m_wake.notify_all();
    }

private:

    struct ActionCompleteImpl : ThreadPoolActionComplete
    {
        ActionCompleteImpl(ThreadPoolImpl* owner) :
            m_owner(owner)
        {
        }

        bool Invoked = false;

        void operator()() override
        {
            Invoked = true;
            m_owner->m_activeCalls--;
            m_owner->m_active.notify_all();
        }

    private:
        ThreadPoolImpl * m_owner = nullptr;
    };

    std::atomic<uint32_t> m_refs { 1 };

    std::mutex m_wakeLock;
    std::condition_variable m_wake;
    std::atomic<uint32_t> m_calls { 0 };

    std::mutex m_activeLock;
    std::condition_variable m_active;
    std::atomic<uint32_t> m_activeCalls { 0 };

    std::atomic<bool> m_terminate = { false };
    std::vector<std::thread> m_pool;
    void* m_context = nullptr;
    ThreadPoolCallback* m_callback = nullptr;
};

ThreadPool::ThreadPool() noexcept :
    m_impl(nullptr)
{
}

ThreadPool::~ThreadPool() noexcept
{
    Terminate();
}

HRESULT ThreadPool::Initialize(_In_opt_ void* context, _In_ ThreadPoolCallback* callback) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, m_impl != nullptr);
    
    std::unique_ptr<ThreadPoolImpl> impl(new (std::nothrow) ThreadPoolImpl);
    RETURN_IF_NULL_ALLOC(impl);

    RETURN_IF_FAILED(impl->Initialize(context, callback));

    m_impl = impl.release();
    return S_OK;
}

void ThreadPool::Terminate() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->Terminate();
        m_impl->Release();
        m_impl = nullptr;
    }
}

void ThreadPool::Submit()
{
    m_impl->Submit();
}
