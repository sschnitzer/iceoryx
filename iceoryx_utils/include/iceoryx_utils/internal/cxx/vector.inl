// Copyright (c) 2019, 2021 by Robert Bosch GmbH, Apex.AI Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef IOX_UTILS_CXX_VECTOR_INL
#define IOX_UTILS_CXX_VECTOR_INL

#include "iceoryx_utils/cxx/vector.hpp"

#include <iostream>

namespace iox
{
namespace cxx
{
// Generic implementation for Capacity > 0
template <typename T, uint64_t Capacity>
inline vector<T, Capacity>::vector(const uint64_t count, const T& value)
{
    if (count > Capacity)
    {
        std::cerr << "Attemting to initialize a vector of capacity " << Capacity << " with " << count
                  << " elements. This exceeds the capacity and only " << Capacity << " elements will be created!"
                  << std::endl;
    }

    for (uint64_t i = 0u; i < count && i < Capacity; ++i)
    {
        emplace_back(value);
    }
}

template <typename T, uint64_t Capacity>
inline vector<T, Capacity>::vector(const uint64_t count)
{
    if (count > Capacity)
    {
        std::cerr << "Attemting to initialize a vector of capacity " << Capacity << " with " << count
                  << " elements. This exceeds the capacity and only " << Capacity << " elements will be created!"
                  << std::endl;
    }

    m_size = std::min(count, Capacity);
    for (uint64_t i = 0U; i < m_size; ++i)
    {
        new (&at(i)) T();
    }
}

template <typename T, uint64_t Capacity>
inline vector<T, Capacity>::vector(const vector& rhs)
{
    *this = rhs;
}

template <typename T, uint64_t Capacity>
inline vector<T, Capacity>::vector(vector&& rhs)
{
    *this = std::move(rhs);
}

template <typename T, uint64_t Capacity>
inline vector<T, Capacity>::~vector()
{
    clear();
}

template <typename T, uint64_t Capacity>
inline vector<T, Capacity>& vector<T, Capacity>::operator=(const vector& rhs)
{
    if (this != &rhs)
    {
        uint64_t i = 0u;
        // copy using copy assignment
        for (; i < std::min(rhs.size(), this->size()); ++i)
        {
            at(i) = rhs.at(i);
        }

        // copy using copy ctor
        for (; i < rhs.size(); ++i)
        {
            emplace_back(rhs.at(i));
        }

        // delete remaining elements
        for (; i < this->size(); ++i)
        {
            at(i).~T();
        }
        m_container.set_size(rhs.size());
    }
    return *this;
}

template <typename T, uint64_t Capacity>
inline vector<T, Capacity>& vector<T, Capacity>::operator=(vector&& rhs)
{
    if (this != &rhs)
    {
        uint64_t i = 0u;
        // move using move assignment
        for (; i < std::min(rhs.size(), this->size()); ++i)
        {
            at(i) = std::move(rhs.at(i));
        }

        // move using move ctor
        for (; i < rhs.size(); ++i)
        {
            emplace_back(std::move(rhs.at(i)));
        }

        // delete remaining elements
        for (; i < this->size(); ++i)
        {
            at(i).~T();
        }

        m_container.set_size(rhs.size());
        rhs.clear();
    }
    return *this;
}

template <typename T, uint64_t Capacity>
inline bool vector<T, Capacity>::empty() const noexcept
{
    return m_container.empty();
}

template <typename T, uint64_t Capacity>
inline bool vector<T, Capacity>::full() const noexcept
{
    return m_container.full();
}


template <typename T, uint64_t Capacity>
inline uint64_t vector<T, Capacity>::size() const
{
    return m_container.size();
}

template <typename T, uint64_t Capacity>
inline uint64_t vector<T, Capacity>::capacity() const
{
    return Capacity;
}

template <typename T, uint64_t Capacity>
inline void vector<T, Capacity>::clear() noexcept
{
    for (uint64_t i = 0u; i < this->size(); ++i)
    {
        at(i).~T();
    }
    m_container.set_size(0u);
}

template <typename T, uint64_t Capacity>
template <typename... Targs>
inline bool vector<T, Capacity>::emplace_back(Targs&&... args)
{
    if (this->size() < Capacity)
    {
        auto index = this->size();
        m_container.set_size(index + 1u);
        new (&at(index)) T(std::forward<Targs>(args)...);
        return true;
    }
    return false;
}

template <typename T, uint64_t Capacity>
bool vector<T, Capacity>::push_back(const T& value)
{
    return emplace_back(value);
}

template <typename T, uint64_t Capacity>
bool vector<T, Capacity>::push_back(T&& value)
{
    return emplace_back(std::forward<T>(value));
}

template <typename T, uint64_t Capacity>
void vector<T, Capacity>::pop_back()
{
    if (this->size() > 0)
    {
        back().~T();
        m_container.set_size(this->size() - 1U);
    }
}

template <typename T, uint64_t Capacity>
inline T* vector<T, Capacity>::data() noexcept
{
    return m_container.data();
}

template <typename T, uint64_t Capacity>
inline const T* vector<T, Capacity>::data() const noexcept
{
    return m_container.data();
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::reference vector<T, Capacity>::at(const uint64_t index) noexcept
{
    // PRQA S 3066 1 # const cast to avoid code duplication
    return const_cast<T&>(const_cast<const vector<T, Capacity>*>(this)->at(index));
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::const_reference vector<T, Capacity>::at(const uint64_t index) const noexcept
{
    if (index + 1u > this->size())
    {
        std::cerr << "out of bounds access, current size is " << this->size() << " but given index is " << index
                  << std::endl;
        std::terminate();
    }
    return reinterpret_cast<const T*>(this->data())[index];
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::reference vector<T, Capacity>::operator[](const uint64_t index) noexcept
{
    return at(index);
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::const_reference vector<T, Capacity>::operator[](const uint64_t index) const
    noexcept
{
    return at(index);
}


template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::reference vector<T, Capacity>::front() noexcept
{
    if (empty())
    {
        std::cerr << "Attempting to access the front of an empty vector!" << std::endl;
        std::terminate();
    }
    return at(0);
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::const_reference vector<T, Capacity>::front() const noexcept
{
    if (empty())
    {
        std::cerr << "Attempting to access the front of an empty vector!" << std::endl;
        std::terminate();
    }
    return at(0);
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::reference vector<T, Capacity>::back() noexcept
{
    if (empty())
    {
        std::cerr << "Attempting to access the back of an empty vector!" << std::endl;
        std::terminate();
    }
    return at(this->size() - 1u);
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::const_reference vector<T, Capacity>::back() const noexcept
{
    if (empty())
    {
        std::cerr << "Attempting to access the back of an empty vector!" << std::endl;
        std::terminate();
    }
    return at(this->size() - 1);
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::iterator vector<T, Capacity>::begin() noexcept
{
    return reinterpret_cast<iterator>(this->data());
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::const_iterator vector<T, Capacity>::begin() const noexcept
{
    return reinterpret_cast<const_iterator>(this->data());
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::iterator vector<T, Capacity>::end() noexcept
{
    return this->data() + this->size();
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::const_iterator vector<T, Capacity>::end() const noexcept
{
    return reinterpret_cast<const_iterator>(this->data() + this->size());
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::iterator vector<T, Capacity>::erase(const_iterator position)
{
    /// If position < begin(), this shall return end(), which is guaranteed:
    /// WLOG, let vector v have Capacity=(2^64-begin())/sizeof(T)-sizeof(m_size)
    /// (note that iterators, index, and Capacity are all limited by 64 bit address space)
    /// With position=0, position-begin()==(2^64-begin())/sizeof(T) > Capacity => returns end()
    uint64_t index = static_cast<uint64_t>(position - begin());
    return erase(index);
}

template <typename T, uint64_t Capacity>
inline typename vector<T, Capacity>::iterator vector<T, Capacity>::erase(uint64_t index)
{
    uint64_t n = index;
    if (n >= this->size())
    {
        return end();
    }
    at(index).~T();
    for (; n + 1u < this->size(); ++n)
    {
        at(n) = std::move(at(n + 1u));
    }
    m_container.set_size(m_container.size() - 1u);
    return this->begin() + index;
}

} // namespace cxx
} // namespace iox

template <typename T, uint64_t CapacityLeft, uint64_t CapacityRight>
bool operator==(const iox::cxx::vector<T, CapacityLeft>& lhs, const iox::cxx::vector<T, CapacityRight>& rhs) noexcept
{
    uint64_t vectorSize = lhs.size();
    if (vectorSize != rhs.size())
    {
        return false;
    }

    for (uint64_t i = 0u; i < vectorSize; ++i)
    {
        if (lhs[i] != rhs[i])
        {
            return false;
        }
    }
    return true;
}

template <typename T, uint64_t CapacityLeft, uint64_t CapacityRight>
bool operator!=(const iox::cxx::vector<T, CapacityLeft>& lhs, const iox::cxx::vector<T, CapacityRight>& rhs) noexcept
{
    return !(lhs == rhs);
}

#endif // IOX_UTILS_CXX_VECTOR_INL
