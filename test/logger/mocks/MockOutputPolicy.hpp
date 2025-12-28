#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <type_traits>
#include <concepts>

namespace logger::test
{

    template <typename T>
    concept HasFormat = requires(T, std::string_view sv) {
        { T::format(sv) } -> std::convertible_to<std::string>;
    };

    template <typename Sink>
    class MockPolicyTemplate
    {
    public:
        using view_type = std::string_view;
        using sink_type = Sink;

        MockPolicyTemplate() = default;

        template <typename S = Sink>
            requires(!std::is_void_v<S>)
        explicit MockPolicyTemplate(S)
        {}

        void publish(view_type line)
        {
            if constexpr (HasFormat<Sink>)
            {
                std::string formatted = sink_type::format(line);
                get_output().emplace_back(formatted);
            }
            else
            {
                get_output().emplace_back(std::string(line));
            }
        }

        static std::vector<std::string> &get_output()
        {
            static std::vector<std::string> output;
            return output;
        }

        static void clear()
        {
            get_output().clear();
        }
    };

    using MockPolicy = MockPolicyTemplate<void>;

} // namespace logger::test