// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include <new>
#include <stddef.h>

NAMESPACE_XBOX_LIBHCBEGIN

class http_memory
{
public:
    static _Ret_maybenull_ _Post_writable_byte_size_(dwSize) void* mem_alloc(
        _In_ size_t dwSize
        );

    static void mem_free(
        _In_ void* pAddress
        );

    http_memory() = delete;
    http_memory(const http_memory&) = delete;
    http_memory& operator=(const http_memory&) = delete;
};

class http_memory_buffer
{
public:
    http_memory_buffer(_In_ size_t dwSize)
    {
        m_pBuffer = http_memory::mem_alloc(dwSize);
    }

    ~http_memory_buffer()
    {
        http_memory::mem_free(m_pBuffer);
        m_pBuffer = nullptr;
    }

    void* get()
    {
        return m_pBuffer;
    }

private:
    void* m_pBuffer;
};

NAMESPACE_XBOX_LIBHCEND

template<typename T>
class http_stl_allocator
{
public:
    typedef T value_type;

    http_stl_allocator() = default;
    template<class U>
    http_stl_allocator(http_stl_allocator<U> const&) {}

    T* allocate(size_t n)
    {
        T* p = static_cast<T*>(xbox::livehttpclient::http_memory::mem_alloc(n * sizeof(T)));

        if (p == nullptr)
        {
            throw std::bad_alloc();
        }
        return p;
    }

    void deallocate(_In_opt_ void* p, size_t)
    {
        xbox::livehttpclient::http_memory::mem_free(p);
    }
};

template<typename T1, typename T2>
inline bool operator==(const http_stl_allocator<T1>&, const http_stl_allocator<T2>&)
{
    return true;
}

template<typename T1, typename T2>
bool operator!=(const http_stl_allocator<T1>&, const http_stl_allocator<T2>&)
{
    return false;
}

template<class T>
using http_internal_vector = std::vector<T, http_stl_allocator<T>>;

template<class K, class V, class LESS = std::less<K>>
using http_internal_map = std::map<K, V, LESS, http_stl_allocator<std::pair<K const, V>>>;

template<class K, class V, class HASH = std::hash<K>, class EQUAL = std::equal_to<K>>
using http_internal_unordered_map = std::unordered_map<K, V, HASH, EQUAL, http_stl_allocator<std::pair<K const, V>>>;

template<class C, class TRAITS = std::char_traits<C>>
using http_internal_basic_string = std::basic_string<C, TRAITS, http_stl_allocator<C>>;

using http_internal_string = http_internal_basic_string<char_t>;

template<class T>
using http_internal_dequeue = std::deque<T, http_stl_allocator<T>>;

template<class T>
using http_internal_queue = std::queue<T, http_internal_dequeue<T>>;
