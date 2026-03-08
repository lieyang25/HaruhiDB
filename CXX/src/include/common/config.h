/**
 * CXX/src/include/common/config.h
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>

namespace HaruhiDB
{
    constexpr size_t HEADER_SIZE = 32;
    constexpr size_t PAGE_SIZE = 4096;

    using page_data_t = std::array<std::byte, PAGE_SIZE>;
    using page_id_t = uint32_t;
    using frame_id_t = size_t;
    using seq_t = uint64_t;

    using slot_id_t = uint16_t;
    using lsn_t = uint64_t;
    
    static constexpr uint16_t SIZE = 0x0FFF;
    static constexpr uint16_t FLAG_DEL = 0x8000;   
    static constexpr uint16_t FLAG_MOV = 0x4000;       
    
    static constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
} // namespace HaruhiDB
