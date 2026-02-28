#pragma once

#include "common/config.h"
#include <string>

namespace HaruhiDB
{
namespace storage
{
    struct IOErr { std::string mes;};

    class disk_manager
    {
    private:
        /* data */
    public:
        disk_manager(/* args */);
        ~disk_manager();
    };
    
    disk_manager::disk_manager(/* args */)
    {
    }
    
    disk_manager::~disk_manager()
    {
    }
    
} // namespace storage
} // namespace HaruhiDB