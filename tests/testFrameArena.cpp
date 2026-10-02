#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include <vector>
#include <cstdint>
#include <cstring>
#include <new>
#include <memory>

#include "FrameArena.hpp" // 你的头文件路径

using namespace ReZeroGui;

// ============================================================
// 简单的测试用分配器（满足 DeallocatingAllocator）
// ============================================================
struct TestAllocator {
    struct Allocation {
        void *ptr;
        std::size_t size;
        std::size_t alignment;
    };

    std::vector<Allocation> allocations;
    std::size_t total_allocated = 0;
    bool fail_next = false; // 用于模拟分配失败

    void *allocate(std::size_t size, std::size_t alignment) noexcept {
        if (fail_next) {
            fail_next = false;
            return nullptr;
        }
        // 使用标准库对齐分配
        void *p = ::operator new(size, std::align_val_t{alignment}, std::nothrow);
        if (p) {
            allocations.push_back({p, size, alignment});
            total_allocated += size;
        }
        return p;
    }

    void deallocate(void *ptr) noexcept {
        if (!ptr)
            return;
        for (auto it = allocations.begin(); it != allocations.end(); ++it) {
            if (it->ptr == ptr) {
                total_allocated -= it->size;
                ::operator delete(ptr, std::align_val_t{it->alignment});
                allocations.erase(it);
                return;
            }
        }
        // 如果找不到，仍然尝试释放（防止测试崩溃）
        ::operator delete(ptr);
    }

    // 辅助：检查某个指针是否由本分配器分配
    bool owns(void *ptr) const {
        for (const auto &a : allocations)
            if (a.ptr == ptr)
                return true;
        return false;
    }
};

// ============================================================
// 辅助函数
// ============================================================
static bool is_aligned(void *ptr, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
}

// ============================================================
// 测试用例
// ============================================================

TEST_CASE("AllocatorView basic type erasure", "[AllocatorView]") {
    TestAllocator raw;
    AllocatorView view(raw);

    void *p = view.allocate(128, 16);
    REQUIRE(p != nullptr);
    REQUIRE(is_aligned(p, 16));
    REQUIRE(raw.owns(p));

    view.deallocate(p);
    REQUIRE(raw.allocations.empty());
}

TEST_CASE("FrameArena - zero size allocation returns nullptr", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    REQUIRE(arena.allocate(0, 8) == nullptr);
    REQUIRE(arena.allocate(0, 64) == nullptr);
}

TEST_CASE("FrameArena - basic allocation and alignment", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    SECTION("small allocation with default alignment") {
        void *p = arena.allocate(32, 8);
        REQUIRE(p != nullptr);
        REQUIRE(is_aligned(p, alignof(std::max_align_t)));
    }

    SECTION("explicit 16-byte alignment") {
        void *p = arena.allocate(64, 16);
        REQUIRE(p != nullptr);
        REQUIRE(is_aligned(p, 16));
    }

    SECTION("128-byte alignment (should succeed after BlockAlignment fix)") {
        void *p = arena.allocate(100, 128);
        REQUIRE(p != nullptr);
        REQUIRE(is_aligned(p, 128));
    }

    SECTION("multiple sequential allocations stay in same block") {
        void *p1 = arena.allocate(100, 8);
        void *p2 = arena.allocate(200, 16);
        void *p3 = arena.allocate(50, 32);

        REQUIRE(p1 != nullptr);
        REQUIRE(p2 != nullptr);
        REQUIRE(p3 != nullptr);

        // 它们应该都来自同一个底层块（raw 只分配了一次数据区 + 一次 Block 节点）
        // 具体次数取决于实现，但至少不应该每次都新分配
        REQUIRE(raw.allocations.size() >= 2); // 至少 base + Block
    }
}

TEST_CASE("FrameArena - reset reuses memory", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    void *p1 = arena.allocate(1024, 16);
    REQUIRE(p1 != nullptr);

    const std::size_t allocs_after_first = raw.allocations.size();

    arena.reset();

    void *p2 = arena.allocate(1024, 16);
    REQUIRE(p2 != nullptr);

    // reset 后再次分配相同大小，不应该向底层要新内存
    REQUIRE(raw.allocations.size() == allocs_after_first);

    // 地址通常会相同（从同一块的开头重新 bump）
    REQUIRE(p1 == p2);
}

TEST_CASE("FrameArena - large allocation creates new block", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    // 先分配一个小块
    void *small = arena.allocate(128, 8);
    REQUIRE(small != nullptr);
    const auto allocs_after_small = raw.allocations.size();

    // 分配一个超过 MinimumBlockSize 的大块
    constexpr std::size_t big_size = 128 * 1024; // 128KB
    void *big = arena.allocate(big_size, 64);
    REQUIRE(big != nullptr);
    REQUIRE(is_aligned(big, 64));

    // 应该新分配了数据区 + Block 节点
    REQUIRE(raw.allocations.size() > allocs_after_small);
}

TEST_CASE("FrameArena - release frees all memory", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    arena.allocate(100, 8);
    arena.allocate(200, 16);
    arena.allocate(50 * 1024, 32); // 触发可能的新块

    REQUIRE_FALSE(raw.allocations.empty());

    arena.release();

    REQUIRE(raw.allocations.empty());
    REQUIRE(raw.total_allocated == 0);
}

TEST_CASE("FrameArena - allocate after release works again", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    arena.allocate(256, 16);
    arena.release();

    REQUIRE(raw.allocations.empty());

    void *p = arena.allocate(128, 32);
    REQUIRE(p != nullptr);
    REQUIRE(is_aligned(p, 32));
    REQUIRE_FALSE(raw.allocations.empty());
}

TEST_CASE("FrameArena - many small allocations", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    std::vector<void *> ptrs;
    for (int i = 0; i < 1000; ++i) {
        void *p = arena.allocate(16 + (i % 64), 8);
        REQUIRE(p != nullptr);
        ptrs.push_back(p);
    }

    // 所有指针都应该互不相同
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        for (std::size_t j = i + 1; j < ptrs.size(); ++j) {
            REQUIRE(ptrs[i] != ptrs[j]);
        }
    }

    arena.reset();

    // reset 后再分配同样数量，底层分配次数不应明显增加
    const auto allocs_before = raw.allocations.size();
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(arena.allocate(16 + (i % 64), 8) != nullptr);
    }
    // 允许少量增长（实现细节），但不应每次都新分配
    REQUIRE(raw.allocations.size() <= allocs_before + 4);
}

TEST_CASE("FrameArena - allocation failure propagates", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    // 先成功分配一次，让 arena 有状态
    REQUIRE(arena.allocate(64, 8) != nullptr);

    // 模拟下一次底层分配失败
    raw.fail_next = true;

    // 强制需要新块（用一个很大的 size）
    void *p = arena.allocate(1024 * 1024, 64); // 1MB
    REQUIRE(p == nullptr);

    // 失败后 arena 状态应保持可用
    raw.fail_next = false;
    void *p2 = arena.allocate(32, 8);
    REQUIRE(p2 != nullptr);
}

TEST_CASE("FrameArena - alignment edge cases", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    SECTION("alignment smaller than max_align_t is raised") {
        void *p = arena.allocate(16, 1); // 请求 1 字节对齐
        REQUIRE(p != nullptr);
        REQUIRE(is_aligned(p, alignof(std::max_align_t)));
    }

    SECTION("power-of-two alignments") {
        for (std::size_t align : {8, 16, 32, 64, 128, 256}) {
            void *p = arena.allocate(64, align);
            REQUIRE(p != nullptr);
            REQUIRE(is_aligned(p, align));
        }
    }
}

TEST_CASE("FrameArena - data can be written and read back", "[FrameArena]") {
    TestAllocator raw;
    FrameArena arena(AllocatorView{raw});

    constexpr std::size_t N = 256;
    auto *buf = static_cast<std::uint8_t *>(arena.allocate(N, 16));
    REQUIRE(buf != nullptr);

    for (std::size_t i = 0; i < N; ++i)
        buf[i] = static_cast<std::uint8_t>(i & 0xFF);

    for (std::size_t i = 0; i < N; ++i)
        REQUIRE(buf[i] == static_cast<std::uint8_t>(i & 0xFF));
}