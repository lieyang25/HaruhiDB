/**
 * CXX/src/include/storage/record/tuple.h
 */

#pragma once

#include "common/config.h"
#include "storage/record/rid.h"

#include <span>
#include <utility>
#include <vector>
namespace HaruhiDB
{
namespace record
{
    class Tuple {
    public:
        Tuple() = default;

        // Own tuple bytes by default. This avoids dangling references once page
        // locks/pins are released by upper layers.
        explicit Tuple(std::span<const std::byte> data)
            : data_(data.begin(), data.end())
        {
        }

        explicit Tuple(std::vector<std::byte> data)
            : data_(std::move(data))
        {
        }

        uint16_t Size() const noexcept {return static_cast<uint16_t>(data_.size());}
        std::byte* Data() noexcept {return data_.data();}
        const std::byte* Data() const noexcept {return data_.data();}
    private:
        std::vector<std::byte> data_;
    };
    
} // namespace record
} // namespace HaruhiDB
 
