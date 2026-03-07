/**
 * CXX/src/include/storage/record/tuple.h
 */

#include "common/config.h"
#include "rid.h"

#include <vector>
namespace HaruhiDB
{
namespace record
{
    class Tuple {
    public:
        Tuple() = default;

        Tuple(RID rid, std::vector<std::byte> data) 
            : rid_(rid), data_(std::move(data)){}

    private:
        RID rid_;
        std::vector<std::byte> data_;
    };
    
} // namespace record
} // namespace HaruhiDB
 