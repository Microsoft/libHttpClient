// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "LocklessList.h"

#define TEST_CLASS_OWNER L"brianpe"

DEFINE_TEST_CLASS(LocklessListTests)
{
public:

#ifdef USING_TAEF

    BEGIN_TEST_CLASS(LocklessListTests)
    END_TEST_CLASS()

#else
    DEFINE_TEST_CLASS_PROPS(LocklessListTests);
#endif

    TEST_METHOD(VerifyBasicOps)
    {
        const uint32_t opCount = 2;
        std::unique_ptr<bool[]> ops(new bool[opCount]);
        memset(ops.get(), 0, sizeof(bool) * opCount);

        LocklessList<uint32_t> list;
        VERIFY_IS_TRUE(list.empty());

        for(uint32_t idx = 0; idx < opCount; idx++)
        {
            auto node = new uint32_t;
            *node = idx;
            VERIFY_IS_TRUE(list.push_back(node));
            VERIFY_IS_FALSE(list.empty());
        }

        while(true)
        {
            bool wasEmpty = list.empty();
            auto node = list.pop_front();

            if (node == nullptr)
            {
                VERIFY_IS_TRUE(wasEmpty);
                break;
            }

            VERIFY_IS_FALSE(wasEmpty);

            ops[*node] = true;
            delete node;
        }

        for (uint32_t idx = 0; idx < opCount; idx++)
        {
            VERIFY_IS_TRUE(ops[idx]);
        }
    }

    TEST_METHOD(VerifySeveralThreads)
    {
        const uint32_t totalPushThreads = 30;
        const uint32_t totalPopThreads = 10;
        const uint32_t callsPerThread = 50000;

        std::unique_ptr<bool[]> slots(new bool[totalPushThreads * callsPerThread]);
        memset(slots.get(), 0, sizeof(bool) * totalPushThreads * callsPerThread);

        //while(!IsDebuggerPresent()) Sleep(1000);

        LocklessList<uint32_t> list;

        std::thread pushThreads[totalPushThreads];

        bool* slotsPtr = slots.get();

        for(uint32_t threadIndex = 0; threadIndex < totalPushThreads; threadIndex++)
        {
            std::thread newThread([threadIndex, &list, slotsPtr, callsPerThread]
            {
                for(uint32_t callIndex = 0; callIndex < callsPerThread; callIndex++)
                {
                    uint32_t* node = new uint32_t;
                    *node = callIndex + (threadIndex * callsPerThread);
                    if (slotsPtr[*node])
                    {
                        // Way too much contention in the logging system to verify directly
                        VERIFY_FAIL();
                    }
                    if (!list.push_back(node))
                    {
                        VERIFY_FAIL();
                    }
                }
            });
            pushThreads[threadIndex].swap(newThread);
        }

        std::thread popThreads[totalPopThreads];

        for(uint32_t threadIndex = 0; threadIndex < totalPopThreads; threadIndex++)
        {
            std::thread newThread([&list, slotsPtr]
            {
                uint32_t* node = list.pop_front();
                while(node != nullptr)
                {
                    if (slotsPtr[*node])
                    {
                        // Way too much contention in the logging system to verify directly
                        VERIFY_FAIL();
                    }
                    slotsPtr[*node] = true;
                    delete node;
                    node = list.pop_front();
                }
            });
            popThreads[threadIndex].swap(newThread);
        }

        // Now we have a massive race between push and pop.  Wait for all the pushes to be done.

        for(uint32_t threadIndex = 0; threadIndex < totalPushThreads; threadIndex++)
        {
            pushThreads[threadIndex].join();
        }

        // And now that all the pushes are complete, wait on the pops.

        for(uint32_t threadIndex = 0; threadIndex < totalPopThreads; threadIndex++)
        {
            popThreads[threadIndex].join();
        }

        // Now that we're done, verify each call made it.

        for(uint32_t callIndex = 0; callIndex < totalPushThreads * callsPerThread; callIndex++)
        {
            if (!slots[callIndex])
            {
                VERIFY_FAIL();
            }
        }
    }

    TEST_METHOD(VerifyOutputNodes)
    {
        const uint32_t opCount = 2;
        std::unique_ptr<bool[]> ops(new bool[opCount]);
        memset(ops.get(), 0, sizeof(bool) * opCount);

        LocklessList<uint32_t> list1;

        for(uint32_t idx = 0; idx < opCount; idx++)
        {
            auto value = new uint32_t;
            *value = idx;
            VERIFY_IS_TRUE(list1.push_back(value));
        }

        LocklessList<uint32_t> list2;

        while(true)
        {
            LocklessList<uint32_t>::Node* node;
            auto value = list1.pop_front(&node);

            if (value == nullptr)
            {
                break;
            }

            VERIFY_IS_TRUE(list2.push_back(value, node));
        }

        while(true)
        {
            auto value = list2.pop_front();

            if (value == nullptr)
            {
                break;
            }

            ops[*value] = true;
            delete value;
        }

        for (uint32_t idx = 0; idx < opCount; idx++)
        {
            VERIFY_IS_TRUE(ops[idx]);
        }
    }
};
