// test/logger/header_args_test.cpp

#include <tuple>
#include <string>
#include <type_traits>

#include <gtest/gtest.h>

#include "logger/registry/header_args.hpp"

using logger::registry::pack_header_args;

TEST(PackHeaderArgs, ReturnsTupleOfDecayedTypes)
{
    auto t = pack_header_args(1, 2.5, "abc");

    
    static_assert(std::is_same_v<
        decltype(t),
        std::tuple<int, double, const char*>
    >);
}

TEST(PackHeaderArgs, DecaysReferencesAndConst)
{
    int x = 42;
    const int y = 7;

    auto t = pack_header_args(x, y);


    static_assert(std::is_same_v<
        decltype(t),
        std::tuple<int, int>
    >);

    EXPECT_EQ(std::get<0>(t), 42);
    EXPECT_EQ(std::get<1>(t), 7);
}

TEST(PackHeaderArgs, PreservesValuesAndOrder)
{
    int a = 10;
    std::string s = "hello";

    auto t = pack_header_args(a, 3.14, s);

    EXPECT_EQ(std::get<0>(t), 10);
    EXPECT_DOUBLE_EQ(std::get<1>(t), 3.14);
    EXPECT_EQ(std::get<2>(t), "hello");
}

TEST(PackHeaderArgs, IsUsableInConstexprContext)
{
    constexpr auto t = pack_header_args(1, 2);

    using T = std::remove_cv_t<decltype(t)>;
    static_assert(std::is_same_v<T, std::tuple<int, int>>);

    static_assert(std::get<0>(t) == 1);
    static_assert(std::get<1>(t) == 2);
}

