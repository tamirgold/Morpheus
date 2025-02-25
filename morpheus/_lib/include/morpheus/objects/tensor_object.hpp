/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "morpheus/objects/dtype.hpp"
#include "morpheus/utilities/string_util.hpp"

#include <cuda_runtime.h>  // for cudaMemcpyDeviceToHost & cudaMemcpy
#include <glog/logging.h>  // for CHECK
#include <mrc/cuda/common.hpp>
#include <rmm/device_uvector.hpp>

#include <algorithm>
#include <array>
#include <cstddef>  // for size_t, byte
#include <cstdint>
#include <functional>
#include <memory>   // for shared_ptr
#include <numeric>  // IWYU pragma: keep
#include <ostream>
#include <stdexcept>  // for runtime_error
#include <string>
#include <utility>  // for exchange, move & pair
#include <vector>
// IWYU is confusing std::size_t with __gnu_cxx::size_t for some reason
// when we define vector<size_t>
// The <numeric> header is needed for transform_reduce but IWYU thinks we don't need it
// IWYU pragma: no_include <ext/new_allocator.h>

namespace morpheus {

/**
 * @addtogroup objects
 * @{
 * @file
 */

using TensorIndex = long long;  // NOLINT
using RankType    = int;        // NOLINT

namespace detail {

template <typename IterT>
std::string join(IterT begin, IterT end, std::string const& separator)
{
    std::ostringstream result;
    if (begin != end)
        result << *begin++;
    while (begin != end)
        result << separator << *begin++;
    return result.str();
}

template <typename IterT>
std::string array_to_str(IterT begin, IterT end)
{
    return MORPHEUS_CONCAT_STR("[" << join(begin, end, ", ") << "]");
}

template <RankType R>
void set_contiguous_stride(const std::array<TensorIndex, R>& shape, std::array<TensorIndex, R>& stride)
{
    TensorIndex ttl = 1;
    auto rank       = shape.size();
    for (int i = rank - 1; i >= 0; i--)
    {
        stride[i] = ttl;
        ttl *= shape.at(i);
    }
}

template <typename IndexT>
void validate_stride(const std::vector<IndexT>& shape, std::vector<IndexT>& stride)
{
    CHECK(stride.empty() || shape.size() == stride.size())
        << "Stride dimension should match shape dimension. Otherwise leave empty to auto calculate stride for "
           "contiguous tensor";

    IndexT ttl = 1;
    auto rank  = shape.size();

    // Fill with -1
    stride.resize(rank, -1);

    for (int i = rank - 1; i >= 0; i--)
    {
        // Only fill -1 values
        if (stride[i] < 0)
        {
            stride[i] = ttl;
        }
        ttl *= shape.at(i);
    }
}

}  // namespace detail

enum class TensorStorageType
{
    Host,
    Device
};

template <typename T>
using DeviceContainer = rmm::device_uvector<T>;  // NOLINT(readability-identifier-naming)

struct MemoryDescriptor
{};

struct ITensorStorage
{
    virtual ~ITensorStorage() = default;

    virtual void* data() const = 0;

    // virtual const void* data() const                             = 0;
    virtual std::size_t bytes() const = 0;

    virtual std::shared_ptr<MemoryDescriptor> get_memory() const = 0;
    // virtual TensorStorageType storage_type() const               = 0;
};

struct ITensor;

struct ITensorOperations
{
    virtual std::shared_ptr<ITensor> slice(const std::vector<TensorIndex>& min_dims,
                                           const std::vector<TensorIndex>& max_dims) const = 0;

    virtual std::shared_ptr<ITensor> reshape(const std::vector<TensorIndex>& dims) const = 0;

    virtual std::shared_ptr<ITensor> deep_copy() const = 0;

    virtual std::shared_ptr<ITensor> copy_rows(const std::vector<std::pair<TensorIndex, TensorIndex>>& selected_rows,
                                               TensorIndex num_rows) const = 0;

    virtual std::shared_ptr<ITensor> as_type(DType dtype) const = 0;
};

struct ITensor : public ITensorStorage, public ITensorOperations
{
    ~ITensor() override = default;

    virtual RankType rank() const = 0;

    virtual std::size_t count() const = 0;

    virtual DType dtype() const = 0;

    virtual std::size_t shape(std::size_t) const = 0;

    virtual std::size_t stride(std::size_t) const = 0;

    virtual bool is_compact() const = 0;

    std::vector<std::size_t> get_shape() const
    {
        std::vector<std::size_t> v(this->rank());
        for (int i = 0; i < this->rank(); ++i)
            v[i] = this->shape(i);
        return v;
    }

    std::vector<std::size_t> get_stride() const
    {
        std::vector<std::size_t> v(this->rank());
        for (int i = 0; i < this->rank(); ++i)
            v[i] = this->stride(i);
        return v;
    }
};

/**
 * @brief Handle for interacting with Morpheus `Tensor` objects. Typically constructed using the `Tensor::create`
 * factory method.
 *
 */
struct TensorObject final
{
    TensorObject() = default;

    TensorObject(std::shared_ptr<MemoryDescriptor> md, std::shared_ptr<ITensor> tensor) :
      m_md(std::move(md)),
      m_tensor(std::move(tensor))
    {}

    TensorObject(std::shared_ptr<ITensor> tensor) : TensorObject(tensor->get_memory(), tensor) {}

    TensorObject(const TensorObject& other) = default;

    TensorObject(TensorObject&& other) :
      m_md(std::exchange(other.m_md, nullptr)),
      m_tensor(std::exchange(other.m_tensor, nullptr))
    {}

    ~TensorObject() = default;

    void* data() const
    {
        return m_tensor->data();
    }

    DType dtype() const
    {
        return m_tensor->dtype();
    }

    std::size_t count() const
    {
        return m_tensor->count();
    }

    std::size_t bytes() const
    {
        return m_tensor->bytes();
    }

    RankType rank() const
    {
        return m_tensor->rank();
    }

    std::size_t dtype_size() const
    {
        return m_tensor->dtype().item_size();
    }

    std::vector<std::size_t> get_shape() const
    {
        return m_tensor->get_shape();
    }

    std::vector<std::size_t> get_stride() const
    {
        return m_tensor->get_stride();
    }

    TensorIndex shape(std::uint32_t idx) const
    {
        return m_tensor->shape(idx);
    }

    TensorIndex stride(std::uint32_t idx) const
    {
        return m_tensor->stride(idx);
    }

    bool is_compact() const
    {
        return m_tensor->is_compact();
    }

    TensorObject slice(std::vector<TensorIndex> min_dims, std::vector<TensorIndex> max_dims) const
    {
        // Replace any -1 values
        std::replace_if(
            min_dims.begin(), min_dims.end(), [](auto x) { return x < 0; }, 0);
        std::transform(
            max_dims.begin(), max_dims.end(), this->get_shape().begin(), max_dims.begin(), [](auto d, auto s) {
                return d < 0 ? s : d;
            });

        return {m_md, m_tensor->slice(min_dims, max_dims)};
    }

    TensorObject reshape(const std::vector<TensorIndex>& dims) const
    {
        return {m_md, m_tensor->reshape(dims)};
    }

    TensorObject deep_copy() const
    {
        std::shared_ptr<ITensor> copy = m_tensor->deep_copy();

        return {copy};
    }

    template <typename T = uint8_t>
    std::vector<T> get_host_data() const
    {
        std::vector<T> out_data;

        CHECK_EQ(this->bytes() % sizeof(T), 0) << "Bytes isnt divisible by type. Check the types are correct";

        out_data.resize(this->bytes() / sizeof(T));

        MRC_CHECK_CUDA(cudaMemcpy(&out_data[0], this->data(), this->bytes(), cudaMemcpyDeviceToHost));

        return out_data;
    }

    template <typename T, size_t N>
    T read_element(const TensorIndex (&idx)[N]) const
    {
        auto stride = this->get_stride();
        auto shape  = this->get_shape();

        CHECK(shape.size() == N) << "Length of idx must match lengh of shape";

        CHECK(std::transform_reduce(
            shape.begin(), shape.end(), std::begin(idx), 1, std::logical_and<>(), std::greater<>()))
            << "Index is outsize of the bounds of the tensor. Index="
            << detail::array_to_str(std::begin(idx), std::begin(idx) + N)
            << ", Size=" << detail::array_to_str(shape.begin(), shape.end()) << "";

        CHECK(DType::create<T>() == this->dtype())
            << "read_element type must match array type. read_element type: '" << DType::create<T>().name()
            << "', array type: '" << this->dtype().name() << "'";

        size_t offset = std::transform_reduce(
                            stride.begin(), stride.end(), std::begin(idx), 0, std::plus<>(), std::multiplies<>()) *
                        this->dtype_size();

        T output;

        MRC_CHECK_CUDA(
            cudaMemcpy(&output, static_cast<uint8_t*>(this->data()) + offset, sizeof(T), cudaMemcpyDeviceToHost));

        return output;
    }

    template <typename T, size_t N>
    T read_element(const std::array<TensorIndex, N> idx) const
    {
        auto stride = this->get_stride();
        auto shape  = this->get_shape();

        CHECK(shape.size() == N) << "Length of idx must match lengh of shape";

        CHECK(
            std::transform_reduce(shape.begin(), shape.end(), std::begin(idx), 1, std::logical_and<>(), std::less<>()))
            << "Index is outsize of the bounds of the tensor. Index="
            << detail::array_to_str(std::begin(idx), std::begin(idx) + N)
            << ", Size=" << detail::array_to_str(shape.begin(), shape.end()) << "";

        CHECK(DType::create<T>() == this->dtype())
            << "read_element type must match array type. read_element type: '" << DType::create<T>().name()
            << "', array type: '" << this->dtype().name() << "'";

        size_t offset = std::transform_reduce(
                            stride.begin(), stride.end(), std::begin(idx), 0, std::plus<>(), std::multiplies<>()) *
                        this->dtype_size();

        T output;

        MRC_CHECK_CUDA(
            cudaMemcpy(&output, static_cast<uint8_t*>(this->data()) + offset, sizeof(T), cudaMemcpyDeviceToHost));

        return output;
    }

    // move assignment
    TensorObject& operator=(TensorObject&& other) noexcept
    {
        // Guard self assignment
        if (this == &other)
            return *this;

        m_md     = std::exchange(other.m_md, nullptr);  // leave other in valid state
        m_tensor = std::exchange(other.m_tensor, nullptr);
        return *this;
    }

    // copy assignment
    TensorObject& operator=(const TensorObject& other)
    {
        // Guard self assignment
        if (this == &other)
            return *this;

        // Check for valid assignment
        if (this->get_shape() != other.get_shape())
        {
            throw std::runtime_error("Left and right shapes do not match");
        }

        if (this->get_stride() != other.get_stride())
        {
            throw std::runtime_error(
                "Left and right strides do not match. At this time, only uniform strides are allowed");
        }

        // Inefficient but should be sufficient
        if (this->get_numpy_typestr() != other.get_numpy_typestr())
        {
            throw std::runtime_error("Left and right types do not match");
        }

        DCHECK(this->bytes() == other.bytes()) << "Left and right bytes should be the same if all other test passed";

        // Perform the copy operation
        MRC_CHECK_CUDA(cudaMemcpy(this->data(), other.data(), this->bytes(), cudaMemcpyDeviceToDevice));

        return *this;
    }

    [[maybe_unused]] std::shared_ptr<ITensor> get_tensor() const
    {
        return m_tensor;
    }

    [[maybe_unused]] std::shared_ptr<MemoryDescriptor> get_memory() const
    {
        return m_md;
    }

    std::string get_numpy_typestr() const
    {
        return m_tensor->dtype().type_str();
    }

    TensorObject as_type(DType dtype) const
    {
        if (dtype == m_tensor->dtype())
        {
            // Shallow copy
            return {*this};
        }

        return {m_tensor->as_type(dtype)};
    }

    /**
     * @brief Creates a deep copy of the rows specified in the exclusive ranges of vector<pair<start, stop>>
     * of the stop row.
     *
     * @param selected_rows
     * @param num_rows
     * @return TensorObject
     */
    TensorObject copy_rows(const std::vector<std::pair<TensorIndex, TensorIndex>>& selected_rows,
                           TensorIndex num_rows) const
    {
        return {m_tensor->copy_rows(selected_rows, num_rows)};
    }

  protected:
    [[maybe_unused]] void throw_on_invalid_storage();

  private:
    std::shared_ptr<MemoryDescriptor> m_md;
    std::shared_ptr<ITensor> m_tensor;
};

/** @} */  // end of group
}  // namespace morpheus
