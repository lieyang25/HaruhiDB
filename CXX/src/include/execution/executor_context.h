#pragma once

#include "catalog/catalog.h"

namespace HaruhiDB
{
namespace execution
{

class ExecutorContext
{
public:
    explicit ExecutorContext(catalog::Catalog* catalog)
        : catalog_(catalog)
    {
    }

    catalog::Catalog* GetCatalog() const noexcept
    {
        return catalog_;
    }

private:
    catalog::Catalog* catalog_{nullptr};
};

} // namespace execution
} // namespace HaruhiDB
