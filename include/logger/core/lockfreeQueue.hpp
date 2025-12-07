#pragma once
#include "logRecord.hpp"

namespace logger::core::detail
{
    using Node = LogRecord;
    class FreeList
    {
        std::atomic<Node *> head_{nullptr};

    public:
        FreeList() = default;

        void push(Node *n) noexcept
        {
            // Treiber stack
            Node *h = head_.load(std::memory_order_relaxed);
            do
            {
                n->free_next = h;
            } while (!head_.compare_exchange_weak(h, n,
                                                  std::memory_order_release, std::memory_order_relaxed));
        }

        Node *try_pop() noexcept
        {
            Node *h = head_.load(std::memory_order_acquire);
            while (h)
            {
                Node *next = h->free_next;
                if (head_.compare_exchange_weak(h, next,
                                                std::memory_order_acq_rel, std::memory_order_acquire))
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
        std::atomic<Node *> tail_;
        Node *head_;
        Node stub_;

    public:
        MpscQueue()
        {
            stub_.next.store(nullptr, std::memory_order_relaxed);
            head_ = &stub_;
            tail_.store(&stub_, std::memory_order_relaxed);
        }

        // PRODUCER: lock-free O(1)
        void push(Node *n) noexcept
        {
            n->next.store(nullptr, std::memory_order_relaxed);         // init link
            Node *prev = tail_.exchange(n, std::memory_order_acq_rel); // append
            prev->next.store(n, std::memory_order_release);            // publish
        }

        // CONSUMER: single-thread pop FIFO
        Node *pop() noexcept
        {
            Node *head = head_;
            Node *next = head->next.load(std::memory_order_acquire);
            if (!next)
            {
                if (tail_.load(std::memory_order_acquire) == head)
                {
                    return nullptr; // pusto
                }
                // wait until the producer publishes the next pointer
                do
                {
                    next = head->next.load(std::memory_order_acquire);
                } while (!next);
            }
            head_ = next; // advance the head
            return next;  // this is the node containing the data
        }

        bool empty() const noexcept
        {
            Node *head = head_;
            Node *next = head->next.load(std::memory_order_acquire);
            if (next)
                return false;
            return tail_.load(std::memory_order_acquire) == head;
        }
    };

}