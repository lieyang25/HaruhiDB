/**
 * CXX/src/include/storage/page/b_plus_tree_page.h
 */

#include "page.h"

namespace HaruhiDB
{
namespace storage
{
    class BPlusTreePage
    {
    public:
        BPlusTreePage() = delete;
        ~BPlusTreePage() = delete;
        
    private:
        Page* page_;
    };
} // namespace storage
} // namespace HaruhiDB
