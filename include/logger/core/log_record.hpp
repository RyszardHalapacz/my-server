#pragma once
#include <atomic>
#include <iostream>
#include <cstdint>
#include <memory>
#include <cstddef>

namespace logger::core::detail
{

    struct  alignas(64) LogRecord
    {
        std::atomic<LogRecord *> next{nullptr};
        LogRecord *free_next{nullptr};

        // Generic in-LogRecord storage for the packet/envelope
        static constexpr std::size_t StorageSize = 256;
        static constexpr std::size_t StorageAlign = 64;

        alignas(StorageAlign) unsigned char storage[StorageSize];

        using ProcessFn = void (*)(void *storage, std::ostream &os);
        using DestroyFn = void (*)(void *storage);
         using SubmitFn = void (*)(void* storage);

        ProcessFn process_fn{nullptr};
        DestroyFn destroy_fn{nullptr};
        SubmitFn submit_fn{nullptr};               

        void *storage_ptr() noexcept { return static_cast<void *>(storage); }
    };
}