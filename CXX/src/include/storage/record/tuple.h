/**
 * CXX/src/include/storage/record/tuple.h
 */

#include "common/config.h"
#include "rid.h"

#include <vector>
#include <cassert>
namespace HaruhiDB
{
namespace record
{
    class Tuple {
    public:
        Tuple() = default;

        Tuple(std::vector<std::byte> data) 
            : data_(std::move(data)){
        }

        uint16_t Size(){return data_.size();}
    private:
        std::vector<std::byte> data_;
    };
    
} // namespace record
} // namespace HaruhiDB
 