/**
 * CXX/src/include/common/config.h
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace HaruhiDB
{
    constexpr size_t PAGE_SIZE = 4096;
    using page_data_t = std::array<std::byte, PAGE_SIZE>;
    using page_id_t = int32_t;
    static constexpr page_id_t INVALID_PAGE_ID = -1;
} // namespace HaruhiDB
