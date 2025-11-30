#pragma once

#include <tuple>
#include <utility>
#include "payload_register.hpp"

namespace logger::registry {

// Builder: given a MsgTag and a *tuple* of arguments, constructs a fully-populated payload.
struct Builder {
    // Main API: MsgTag + args_tuple_type -> fully constructed payload object.
    // All mapping from (Tag, index) -> member comes from PayloadRegister<Tag>::field_ptrs.
    template<MsgTag Tag>
    static auto build(typename PayloadRegister<Tag>::args_tuple_type args_tuple) {
        using Reg = PayloadRegister<Tag>;
        using P   = typename Reg::payload_type;

        constexpr std::size_t N_fields =
            std::tuple_size_v<decltype(Reg::field_ptrs)>;

        static_assert(
            N_fields == std::tuple_size_v<typename Reg::args_tuple_type>,
            "field_ptrs and args_tuple_type size mismatch"
        );

        P payload{};

        assign_fields(payload, Reg::field_ptrs, args_tuple);
        return payload;
    }

private:
    template<typename P, typename TuplePtrs, typename TupleArgs,
             std::size_t... Is>
    static void assign_fields_impl(
        P& p,
        const TuplePtrs& ptrs,
        TupleArgs& args,
        std::index_sequence<Is...>
    ) {
        ((p.*(std::get<Is>(ptrs)) = std::get<Is>(args)), ...);
    }

    template<typename P, typename TuplePtrs, typename TupleArgs>
    static void assign_fields(
        P& p,
        const TuplePtrs& ptrs,
        TupleArgs& args
    ) {
        constexpr std::size_t N_ptrs  = std::tuple_size_v<TuplePtrs>;
        constexpr std::size_t N_args  = std::tuple_size_v<TupleArgs>;

        static_assert(N_ptrs == N_args,
                      "Mismatched args vs field_ptrs size");

        assign_fields_impl(
            p,
            ptrs,
            args,
            std::make_index_sequence<N_ptrs>{}
        );
    }
};

} // namespace logger::registry
