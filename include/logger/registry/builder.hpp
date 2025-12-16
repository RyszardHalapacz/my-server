#pragma once

#include <tuple>
#include <utility>
#include <type_traits>
#include "payload_register.hpp"

namespace logger::registry {

// Builder: given a MsgTag and a *tuple* of arguments, constructs a fully-populated payload.
struct Builder {
    // Main API: MsgTag + tuple of args -> fully constructed payload object.
    // Tuple can be any std::tuple-like as long as its layout matches PayloadRegister<Tag>.
    template<MsgTag Tag, typename Tuple>
    static auto build(Tuple&& args_tuple) {
        using Reg = PayloadRegister<Tag>;
        using P   = typename Reg::payload_type;

        using TupleDecay = std::remove_reference_t<Tuple>;

        constexpr std::size_t N_fields =
            std::tuple_size_v<decltype(Reg::field_ptrs)>;

        static_assert(
            N_fields == std::tuple_size_v<TupleDecay>,
            "Builder: field_ptrs and provided args tuple size mismatch"
        );

        P payload{};
        assign_fields(payload, Reg::field_ptrs, std::forward<Tuple>(args_tuple));
        return payload;
    }

private:
    template<typename P, typename TuplePtrs, typename TupleArgs,
             std::size_t... Is>
    static void assign_fields_impl(
        P& p,
        const TuplePtrs& ptrs,
        TupleArgs&& args,
        std::index_sequence<Is...>
    ) {
        using ArgsTupleDecay = std::remove_reference_t<TupleArgs>;
        static_assert( std::is_same_v<unsigned int, std::uint32_t>);
        // Compile-time check: each argument type must exactly match the field type
        (
            []<std::size_t I, typename Payload, typename PtrTuple, typename ArgTuple>() {
                using MemberPtr = std::tuple_element_t<I, PtrTuple>;
                using MemberT   =
                    decltype(std::declval<Payload>().*std::declval<MemberPtr>());

                using ArgT = std::tuple_element_t<I, ArgTuple>;

                static_assert(
                    std::is_same_v<
                        std::remove_cvref_t<MemberT>,
                        std::remove_cvref_t<ArgT>
                    >,
                    "Builder: argument type does not exactly match payload field type"
                );
            }.template operator()<Is, P, TuplePtrs, ArgsTupleDecay>(),
            ...
        );

        // Runtime assignment: all static checks above must pass
        ((p.*(std::get<Is>(ptrs)) = std::get<Is>(args)), ...);
    }

    template<typename P, typename TuplePtrs, typename TupleArgs>
    static void assign_fields(
        P& p,
        const TuplePtrs& ptrs,
        TupleArgs&& args
    ) {
        using ArgsTupleDecay = std::remove_reference_t<TupleArgs>;

        constexpr std::size_t N_ptrs = std::tuple_size_v<TuplePtrs>;
        constexpr std::size_t N_args = std::tuple_size_v<ArgsTupleDecay>;

        static_assert(N_ptrs == N_args,
                      "Builder: mismatched args vs field_ptrs size");

        assign_fields_impl(
            p,
            ptrs,
            std::forward<TupleArgs>(args),
            std::make_index_sequence<N_ptrs>{}
        );
    }
};

} // namespace logger::registry
