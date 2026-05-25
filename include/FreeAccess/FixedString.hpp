#pragma once

#include <cstdint>
#include <string_view>

namespace FreeAccess
{
// Non-null-terminated string.
template<std::size_t N>
struct FixedString
{
    char str_[N];
    
    constexpr FixedString(const char (&input)[N + 1])
    {
        for (std::size_t i = 0; i < N; i++)
            str_[i] = input[i];
    }

    template<typename T>
    constexpr FixedString(T&& begin, T&& end)
    {
        for (std::size_t i = 0; i < N && begin < end; ++i, ++begin)
            str_[i] = *begin;
    }

    template<typename T>
    constexpr FixedString(T&& range) : FixedString{ 
        std::forward<T>(range).begin(), std::forward<T>(range).end()
    } { }

    constexpr std::string_view View() const {
        return std::string_view{ str_, str_ + N };
    }

    template<std::size_t M>
    constexpr bool operator==(const FixedString<M>& another) const
    {
        if constexpr (M != N)
            return false;
        else
            return View() == another.View();
    }
};

template<std::size_t N>
requires (N != 1)
FixedString(const char (&input)[N]) -> FixedString<N - 1>;

} // namespace FreeAccess