/**
 * CXX/src/include/storage/record/tuple.h
 */

#pragma once

#include "common/config.h"
#include "storage/record/rid.h"

#include <span>
namespace HaruhiDB
{
namespace record
{
    class Tuple {
    public:
        Tuple() = default;

        Tuple(std::span<std::byte> data) 
            : data_(data){
        }

        uint16_t Size() const noexcept {return static_cast<uint16_t>(data_.size());}
        std::byte* Data() noexcept {return data_.data();}
        const std::byte* Data() const noexcept {return data_.data();}
    private:
        std::span<std::byte> data_;
    };
    
} // namespace record
} // namespace HaruhiDB
 
