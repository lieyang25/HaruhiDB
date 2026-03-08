/**
 * CXX/src/include/storage/record/tuple.h
 */

#include "common/config.h"
#include "rid.h"

#include <span>
#include <cassert>
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

        uint16_t Size(){return data_.size();}
        std::byte* Data(){return data_.data();}
    private:
        std::span<std::byte> data_;
    };
    
} // namespace record
} // namespace HaruhiDB
 