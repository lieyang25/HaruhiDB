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
    constexpr uint32_t CATALOG_META_MAGIC = 0x4341544DU;         // "CATM"
    constexpr uint32_t CATALOG_META_VERSION = 1U;
    constexpr uint32_t CATALOG_META_PAGE_MAGIC = 0x434D4554U;    // "CMET"
    constexpr uint32_t CATALOG_META_PAGE_VERSION = 1U;


    constexpr size_t HEADER_SIZE = 32;
    constexpr size_t PAGE_SIZE = 4096;

    using page_data_t = std::array<std::byte, PAGE_SIZE>;
    using page_id_t = uint32_t;
    using frame_id_t = size_t;
    using seq_t = uint64_t;
    using slot_id_t = uint16_t;
    using lsn_t = uint64_t;
    using table_oid_t = uint32_t;
    using index_oid_t = uint32_t;
    
    static constexpr uint32_t DB_MAGIC = 0x48415255;
    static constexpr uint32_t DB_VERSION = 1;
    static constexpr uint32_t WAL_MAGIC = 0x57414C31U;
    static constexpr uint32_t WAL_VERSION = 1U;
    static constexpr uint32_t WAL_PAYLOAD_LEN = static_cast<uint32_t>(PAGE_SIZE);

    static constexpr uint16_t TUPLE_LENGTH = 0x0FFF;
    static constexpr uint16_t TUPLE_FLAG_DEL = 0x8000;   
    static constexpr uint16_t TUPLE_FLAG_MOV = 0x4000;       
    
    static constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
    static constexpr slot_id_t INVALID_SLOT_ID = std::numeric_limits<slot_id_t>::max();
} // namespace HaruhiDB
