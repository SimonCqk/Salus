/*
 * Copyright 2019 Peifeng Yu <peifeng@umich.edu>
 * 
 * This file is part of Salus
 * (see https://github.com/SymbioticLab/Salus).
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SALUS_SSTL_CPP17_H
#define SALUS_SSTL_CPP17_H

#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sstl {

// TODO: use macro check

struct from_chars_result
{
    const char *ptr;
    std::error_code ec;
};

template<typename T>
from_chars_result from_chars(const char *first, const char *last, T &value, int base = 10) noexcept
{
    size_t pos;
    from_chars_result fcr{first, {}};
    try {
        auto val = std::stoll(std::string(first, last), &pos, base);
        value = static_cast<T>(val);
        fcr.ptr = first + pos;
    } catch (std::invalid_argument &ex) {
        fcr.ec = std::make_error_code(std::errc::invalid_argument);
    } catch (std::out_of_range &ex) {
        fcr.ec = std::make_error_code(std::errc::result_out_of_range);
    } catch (...) {
        // ignore
    }

    return fcr;
}

template<typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) noexcept
{
    return static_cast<typename std::underlying_type<E>::type>(e);
}

/**
 * @brief Check whether `val' is in `args'
 * @tparam T
 * @tparam Args
 * @param val
 * @param args
 * @return
 */
template<typename T, typename... Args>
constexpr bool is_in(const T &val, const Args &... args)
{
    if constexpr (sizeof...(args) == 0) {
        return false;
    } else {
        return [](const auto &... p) { return (... || p); }((val == args)...);
    }
}

namespace detial {
template<class Tup, class Func, std::size_t... Is>
constexpr void static_for_impl(Tup &&t, Func &&f, std::index_sequence<Is...>)
{
    (f(std::integral_constant<std::size_t, Is>{}, std::get<Is>(std::forward<Tup>(t))), ...);
}
} // namespace detial

/**
 * @brief static for loop over a tuple
 * @tparam T
 * @tparam Func
 * @param t the tuple to loop over
 * @param f the functor, must be generic if the tuple is heterogeneous.
 * It has the signature functor(size_t index, T element)
 */
template<class... T, class Func>
constexpr void static_for(std::tuple<T...> &&t, Func &&f)
{
    detial::static_for_impl(std::forward<decltype(t)>(t), std::forward<Func>(f),
                            std::make_index_sequence<sizeof...(T)>{});
}

template<typename T,
         typename TIter = decltype(std::begin(std::declval<T>())),
         typename = decltype(std::end(std::declval<T>()))>
constexpr auto enumerate(T &&iterable)
{
    struct iterator
    {
        size_t i;
        TIter iter;
        bool operator != (const iterator & other) const { return iter != other.iter; }
        void operator ++ () { ++i; ++iter; }
        auto operator * () const { return std::tie(i, *iter); }
    };
    struct iterable_wrapper
    {
        T iterable;
        auto begin() { return iterator{ 0, std::begin(iterable) }; }
        auto end() { return iterator{ 0, std::end(iterable) }; }
    };
    return iterable_wrapper{ std::forward<T>(iterable) };
}

} // namespace sstl

#endif // SALUS_SSTL_CPP17_H
