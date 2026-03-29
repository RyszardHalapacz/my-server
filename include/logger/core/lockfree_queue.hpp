#pragma once
#include "log_record.hpp"

namespace logger::core::detail
{
    // NOTE: This is a Treiber stack with a theoretical ABA risk.
    // In practice acceptable: this is a logger (not safety-critical),
    // ABA would corrupt at most one log entry, and the window is small
    // (worker must recycle a node before a stale CAS completes).
    // A tagged-pointer fix would require 128-bit CAS (x86-64 only) or
    // bit manipulation — both undesirable for a Raspberry Pi target.
    class FreeList
    {
        std::atomic<FreeNode*> head_{nullptr};

    public:
        FreeList() = default;

        void push(FreeNode* n) noexcept
        {
            FreeNode* h = head_.load(std::memory_order_relaxed);
            do
            {
                n->free_next = h;
            } while (!head_.compare_exchange_weak(h, n,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
        }

        FreeNode* try_pop() noexcept
        {
            FreeNode* h = head_.load(std::memory_order_acquire);
            while (h)
            {
                FreeNode* next = h->free_next;
                if (head_.compare_exchange_weak(h, next,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire))
                {
                    return h;
                }
            }
            return nullptr;
        }

        bool empty() const noexcept
        {
            return head_.load(std::memory_order_acquire) == nullptr;
        }
    };

    class MpscQueue
    {
        std::atomic<MpscNode *> tail_;
        MpscNode *head_;
        MpscNode stub_;

    public:
        MpscQueue()
        {
            stub_.next.store(nullptr, std::memory_order_relaxed);
            head_ = &stub_;
            tail_.store(&stub_, std::memory_order_relaxed);
        }

        // PRODUCER: lock-free O(1)
        void push(MpscNode *n) noexcept
        {
            n->next.store(nullptr, std::memory_order_relaxed);
            MpscNode *prev = tail_.exchange(n, std::memory_order_acq_rel);
            prev->next.store(n, std::memory_order_release);
        }

        // CONSUMER: single-thread pop FIFO
        MpscNode *pop() noexcept
        {
            MpscNode *head = head_;
            MpscNode *next = head->next.load(std::memory_order_acquire);
            if (!next)
            {
                if (tail_.load(std::memory_order_acquire) == head)
                    return nullptr;
                // wait until the producer publishes the next pointer
                do
                {
                    next = head->next.load(std::memory_order_acquire);
                } while (!next);
            }
            head_ = next;
            return next;
        }

        bool empty() const noexcept
        {
            MpscNode *head = head_;
            MpscNode *next = head->next.load(std::memory_order_acquire);
            if (next)
                return false;
            return tail_.load(std::memory_order_acquire) == head;
        }

        // Restore stub_ as the dummy node. Call after draining the queue
        // (worker shutdown) and before recycling the last pending_recycle node,
        // to prevent a self-loop when that node is re-enqueued in the next run.
        void reset() noexcept
        {
            stub_.next.store(nullptr, std::memory_order_relaxed);
            head_ = &stub_;
            tail_.store(&stub_, std::memory_order_relaxed);
        }
    };
}
