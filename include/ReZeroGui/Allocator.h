#pragma once

#include <cstddef>
#include <concepts>

namespace ReZeroGui {
    template <class A>
    concept Allocator = requires(A &a, std::size_t size, std::size_t alignment) {
        { a.allocate(size, alignment) } noexcept -> std::same_as<void *>;
    };

    template <class A>
    concept DeallocatingAllocator = Allocator<A> && requires(A &a, void *ptr) {
        { a.deallocate(ptr) } noexcept;
    };
    /// A non-owning, type-erased view of an allocator.
    struct AllocatorView {
      private:
        struct VTable {
            auto (*allocate)(void *self, std::size_t size, std::size_t alignment) noexcept -> void *;
            auto (*deallocate)(void *self, void *ptr) noexcept -> void;
        };
        // Type Erasure
        template <DeallocatingAllocator A> static const VTable &vtable() noexcept {
            static constexpr VTable vt = {
                .allocate = [](void *self, std::size_t size, std::size_t alignment) noexcept -> void * {
                    return static_cast<A *>(self)->allocate(size, alignment);
                },
                .deallocate = [](void *self, void *ptr) noexcept { static_cast<A *>(self)->deallocate(ptr); }};
            return vt;
        }

      public:
        AllocatorView() = delete;

        // Do not allow AllocatorView to wrap another AllocatorView.
        template <DeallocatingAllocator A>
            requires(!std::same_as<std::remove_cvref_t<A>, AllocatorView>)
        explicit AllocatorView(A &allocator) : self(std::addressof(allocator)), vt(&vtable<A>()) {}
        auto allocate(std::size_t size, std::size_t alignment) const noexcept -> void * {
            return vt->allocate(self, size, alignment);
        }
        auto deallocate(void *ptr) const noexcept -> void { vt->deallocate(self, ptr); }

      private:
        void *self;
        const VTable *vt;
    };

    struct FrameArena {
        struct Block {
            std::uint8_t *base{nullptr};
            std::size_t size{0};
            std::size_t offset{0};
            Block *next{nullptr};
        };

        /**
         * @brief Allocates memory from the arena
         *
         * @param size
         * @param alignment
         * @return
         */
        [[nodiscard]] auto allocate(std::size_t size, std::size_t alignment) noexcept -> void * {
            if (size == 0)
                return nullptr;
            if (alignment < alignof(std::max_align_t))
                alignment = alignof(std::max_align_t);

            while (current != nullptr) {
                const std::size_t aligned = alignUp(current->offset, alignment);
                // Check available space without overflow.
                if (aligned <= current->size && (current->size - aligned) >= size) {
                    current->offset = aligned + size;
                    return current->base + aligned;
                }
                current = current->next;
            }

            return allocateBlock(size, alignment);
        }
        /**
         * @brief Does nothing
         *
         * @return
         */
        auto deallocate(void *) noexcept {} // arena 语义

        /**
         * @brief Reset the arena for reuse
         *
         * @return
         */
        auto reset() noexcept {
            for (Block *b = head; b != nullptr; b = b->next) {
                b->offset = 0;
            }
            current = head;
        }

        /**
         * @brief Releases all memory owned by the arena
         *
         * @return
         */
        auto release() noexcept {
            Block *b = head;
            while (b != nullptr) {
                Block *next = b->next;
                if (b->base != nullptr) {
                    allocator.deallocate(b->base);
                }
                allocator.deallocate(b);
                b = next;
            }
            head = current = nullptr;
        }

      private:
        static constexpr std::size_t BlockAlignment = 64;
        static constexpr std::size_t MinimumBlockSize = 64 * 1024;
        static constexpr auto alignUp(std::size_t value, std::size_t alignment) -> std::size_t {
            return (value + alignment - 1) & ~(alignment - 1);
        }
        auto allocateBlock(std::size_t size, std::size_t alignment) noexcept -> void * {
            std::size_t blockSize = MinimumBlockSize;
            while (blockSize < size + alignment) {
                blockSize *= 2;
            }
            const std::size_t blockAlign = alignment > BlockAlignment ? alignment : BlockAlignment;

            // 先分配数据区
            auto *base = static_cast<std::uint8_t *>(allocator.allocate(blockSize, blockAlign));
            if (base == nullptr)
                return nullptr;

            auto *block = static_cast<Block *>(allocator.allocate(sizeof(Block), alignof(Block)));
            if (block == nullptr) {
                allocator.deallocate(base);
                return nullptr;
            }

            block->base = base;
            block->size = blockSize;
            block->next = nullptr;

            const std::size_t aligned = alignUp(0, alignment);
            block->offset = aligned + size;

            // Add the block to the list.
            if (head == nullptr) {
                head = current = tail = block;
            } else {
                tail->next = block;
                tail = block;
                current = block;
            }

            return base + aligned;
        }

        AllocatorView allocator;
        Block *head = nullptr;    // First block.
        Block *current = nullptr; // current block
        Block *tail = nullptr;    // last block
    };
} // namespace ReZeroGui