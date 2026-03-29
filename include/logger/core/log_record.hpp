#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <cstddef>

namespace logger::core::detail
{
    struct MpscNode
    {
        std::atomic<MpscNode *> next{nullptr};
    };

    struct FreeNode
    {
        FreeNode *free_next{nullptr};
    };

    struct alignas(64) LogRecord : MpscNode, FreeNode
    {
        static constexpr std::size_t StorageSize = 256;
        static constexpr std::size_t StorageAlign = 64;

        alignas(StorageAlign) unsigned char storage[StorageSize];

        using DestroyFn = void (*)(void *storage);
        using SubmitFn  = void (*)(void *storage);

        DestroyFn destroy_fn{nullptr};
        SubmitFn  submit_fn{nullptr};

        void *storage_ptr() noexcept { return static_cast<void *>(storage); }
    };
}
