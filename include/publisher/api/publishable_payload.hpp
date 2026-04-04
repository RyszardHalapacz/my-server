//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHABLE_PAYLOAD_HPP
#define MYSERVER_PUBLISHABLE_PAYLOAD_HPP

#include <type_traits>
#include <utility>

#include "publisher/api/publishable.hpp"
#include "publisher/runtime/registration_handle.hpp"

namespace publisher::api
{
    template<typename Payload>
    class PublishablePayload : public Publishable<PublishablePayload<Payload>>
    {
        static_assert(sizeof(Payload) <= 256,
                      "Payload exceeds 256B limit");
        static_assert(!std::is_polymorphic_v<Payload>,
                      "Virtual dispatch is forbidden");

    public:
        PublishablePayload(publisher::runtime::RegistrationHandle handle, Payload payload)
            : Publishable<PublishablePayload<Payload>>(std::move(handle))
            , payload_(std::move(payload))
        {}

        [[nodiscard]] const Payload& data() const noexcept
        {
            return payload_;
        }

        [[nodiscard]] Payload& data() noexcept
        {
            return payload_;
        }

        // std::string_view payload() const noexcept;
        // — TBD: serialization contract not yet established.
        // When defined, called by PublisherRuntime<Sink>::publish()
        // via SinkTraits<Sink>::write(handle, obj.payload()).

    private:
        Payload payload_;
    };
} // namespace publisher::api

#endif // MYSERVER_PUBLISHABLE_PAYLOAD_HPP
