/**
 * CXX/src/table/scope_guard.h
 */
#pragma once

#include <utility>

namespace HaruhiDB
{
namespace table
{

template <typename F>
class ScopeGuard
{
public:
    explicit ScopeGuard(F&& fn)
        : fn_(std::forward<F>(fn)), active_(true)
    {
    }

    ScopeGuard(ScopeGuard&& other) noexcept
        : fn_(std::move(other.fn_)), active_(std::exchange(other.active_, false))
    {
    }

    ~ScopeGuard()
    {
        if (active_) {
            fn_();
        }
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    F fn_;
    bool active_;
};

template <typename F>
ScopeGuard<F> MakeScopeGuard(F&& fn)
{
    return ScopeGuard<F>(std::forward<F>(fn));
}

} // namespace table
} // namespace HaruhiDB
