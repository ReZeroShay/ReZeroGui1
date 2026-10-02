#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cmath>
#include <concepts>
#include <iterator>
#include <memory>
#include <ReZeroGui/Allocator.h>

#if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4141)
#endif

#if defined(_MSC_VER)
#    define REZERO_COMPILER_MSVC 1
#    define REZERO_BREAK() __debugbreak()
#    define REZERO_INLINE __forceinline
#    define REZERO_NOINLINE __declspec(noinline)
#elif defined(__clang__)
#    define REZERO_COMPILER_CLANG 1
#    define REZERO_BREAK() __builtin_debugtrap()
#    define REZERO_INLINE inline __attribute__((always_inline))
#    define REZERO_NOINLINE __attribute__((noinline))
#elif defined(__GNUC__)
#    define REZERO_COMPILER_GCC 1
#    define REZERO_BREAK() __builtin_trap()
#    define REZERO_INLINE inline __attribute__((always_inline))
#    define REZERO_NOINLINE __attribute__((noinline))
#else
#    error "ReZeroGui: unsupported compiler"
#endif

#if !defined(REZERO_INLINE)
#    define REZERO_INLINE inline
#endif
#if !defined(REZERO_NOINLINE)
#    define REZERO_NOINLINE
#endif
#if !defined(REZERO_BREAK)
#    define REZERO_BREAK() ((void)0)
#endif

#define REZERO_NODISCARD [[nodiscard]]
#define REZERO_LIKELY(x) (!!(x))
#define REZERO_UNLIKELY(x) (!!(x))

#if !defined(REZERO_ENABLE_ASSERT)
#    if defined(NDEBUG)
#        define REZERO_ENABLE_ASSERT 0
#    else
#        define REZERO_ENABLE_ASSERT 1
#    endif
#endif

#if REZERO_ENABLE_ASSERT
#    define REZERO_ASSERT(expr) assert(expr)
#    define REZERO_ASSERT_MSG(expr, msg) assert((expr) && (msg))
#else
#    define REZERO_ASSERT(expr) ((void)0)
#    define REZERO_ASSERT_MSG(expr, msg) ((void)0)
#endif

#define Z_TRY_EXPR(expr)                                                                                               \
    do {                                                                                                               \
        auto &&result = (expr);                                                                                        \
        if (!result)                                                                                                   \
            return z::Error{result.error()};                                                                           \
    } while (false)

#define Z_TRY(result)                                                                                                  \
    do {                                                                                                               \
        if (!result)                                                                                                   \
            return z::Error{result.error()};                                                                           \
    } while (false)

namespace ReZeroGui {

    namespace Z {
        template <typename T> constexpr const T &max(const T &a, const T &b) { return (a < b) ? b : a; }
        template <typename T> constexpr const T &min(const T &a, const T &b) { return (b < a) ? b : a; }
        constexpr float clampFloat(float value, float minimum, float maximum) noexcept {
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }

        /// Frame rate independent approach toward `target`: `current` closes
        /// 1 - exp(-dt / tau) of the remaining distance per call, and snaps once it is
        /// closer than a tenth of a pixel so an animation can actually settle instead
        /// of redrawing sub pixel noise forever. `tau` is the time constant in
        /// seconds: about 63% of the way after one tau.
        inline float damp(float current, float target, float tau, float dt) noexcept {
            if (tau <= 0.0f || dt <= 0.0f) {
                return target;
            }
            const float next = target + (current - target) * std::exp(-dt / tau);
            return std::abs(target - next) < 0.0001f ? target : next;
        }

        template <class T>
            requires std::is_trivially_copyable_v<T>
        class ArrayList {
          public:
            using value_type = T;
            using size_type = std::size_t;
            using iterator = T *;
            using const_iterator = const T *;

            static constexpr size_type npos = static_cast<size_type>(-1);

          public:
            explicit ArrayList(AllocatorView allocator) noexcept : allocator(allocator) {}

            ~ArrayList() noexcept { clearAndRelease(); }

            ArrayList(const ArrayList &) = delete;
            auto operator=(const ArrayList &) -> ArrayList & = delete;

            ArrayList(ArrayList &&other) noexcept
                : allocator(other.allocator), items(other.items), length(other.length), capacity(other.capacity) {
                other.items = nullptr;
                other.length = 0;
                other.capacity = 0;
            }

            auto operator=(ArrayList &&Other) noexcept -> ArrayList & {
                if (this == &Other) {
                    return *this;
                }

                clearAndRelease();

                allocator = Other.allocator;
                items = Other.items;
                length = Other.length;
                capacity = Other.capacity;

                Other.items = nullptr;
                Other.length = 0;
                Other.capacity = 0;

                return *this;
            }

          public:
            [[nodiscard]] auto data() noexcept -> T * { return items; }

            [[nodiscard]] auto data() const noexcept -> const T * { return items; }

            [[nodiscard]] auto size() const noexcept -> size_type { return length; }

            [[nodiscard]] auto empty() const noexcept -> bool { return length == 0; }

          public:
            auto operator[](size_type index) noexcept -> T & {
                assert(index < length);
                return items[index];
            }

            auto operator[](size_type index) const noexcept -> const T & {
                assert(index < length);
                return items[index];
            }

            auto back() noexcept -> T & {
                assert(length > 0);
                return items[length - 1];
            }

            auto back() const noexcept -> const T & {
                assert(length > 0);
                return items[length - 1];
            }

          public:
            auto push_back(const T &value) noexcept -> bool {
                if (length == capacity && !growForOne()) {
                    return false;
                }

                items[length] = value;
                length += 1;

                return true;
            }

            auto push_back(T &&value) noexcept -> bool {
                if (length == capacity && !growForOne()) {
                    return false;
                }

                items[length] = value;
                length += 1;

                return true;
            }

            auto pop_back() noexcept -> void {
                assert(length > 0);
                length -= 1;
            }

            auto clear() noexcept -> void { length = 0; }

            auto begin() noexcept -> iterator { return items; }

            auto begin() const noexcept -> const_iterator { return items; }

            auto end() noexcept -> iterator { return items + length; }

            auto end() const noexcept -> const_iterator { return items + length; }

          private:
            auto growForOne() noexcept -> bool {
                if (capacity == 0) {
                    return grow(1);
                }

                return grow(capacity * 2);
            }

            auto grow(size_type newCapacity) noexcept -> bool {
                assert(newCapacity > capacity);

                const size_type bytes = newCapacity * sizeof(T);

                void *newMemory = allocator.allocate(bytes, alignof(T));

                if (newMemory == nullptr) {
                    return false;
                }

                auto *newItems = static_cast<T *>(newMemory);

                if (items != nullptr && length != 0) {
                    std::memcpy(newItems, items, length * sizeof(T));
                }

                allocator.deallocate(items);

                items = newItems;
                capacity = newCapacity;

                return true;
            }

            auto clearAndRelease() noexcept -> void {
                if (items != nullptr) {
                    allocator.deallocate(items);
                }

                items = nullptr;
                length = 0;
                capacity = 0;
            }

          private:
            AllocatorView allocator;
            T *items = nullptr;
            size_type length = 0;
            size_type capacity = 0;
        };

        template <class Char> struct BasicStringView {
          public:
            static_assert(std::is_trivial_v<Char>);

            using value_type = Char;
            using size_type = std::size_t;
            using difference_type = std::ptrdiff_t;

            using const_pointer = const Char *;
            using const_reference = const Char &;
            using const_iterator = const Char *;

            static constexpr size_type npos = static_cast<size_type>(-1);

          public:
            constexpr BasicStringView() noexcept = default;

            constexpr BasicStringView(const Char *chars, size_type length) noexcept : first(chars), length(length) {
                REZERO_ASSERT(chars != nullptr || length == 0);
            }

            constexpr BasicStringView(const Char *chars) noexcept
                : first(chars), length(chars ? cstrLength(chars) : 0) {}

            template <size_type N>
            constexpr BasicStringView(const Char (&str)[N]) noexcept : first(str), length(N - 1) {}

          public:
            [[nodiscard]] constexpr auto data() const noexcept -> const Char * { return first; }

            [[nodiscard]] constexpr auto size() const noexcept -> size_type { return length; }

            [[nodiscard]] constexpr auto empty() const noexcept -> bool { return length == 0; }

            [[nodiscard]] constexpr auto front() const noexcept -> const Char & {
                REZERO_ASSERT(length > 0);
                return first[0];
            }

            [[nodiscard]] constexpr auto back() const noexcept -> const Char & {
                REZERO_ASSERT(length > 0);
                return first[length - 1];
            }

            [[nodiscard]] constexpr auto operator[](size_type index) const noexcept -> const Char & {
                REZERO_ASSERT(index < length);
                return first[index];
            }

            [[nodiscard]] constexpr auto at(size_type index) const noexcept -> const Char & {
                REZERO_ASSERT(index < length);
                return first[index];
            }

          public:
            [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator { return first; }

            [[nodiscard]] constexpr auto end() const noexcept -> const_iterator { return first + length; }

          public:
            [[nodiscard]] constexpr auto substr(size_type position, size_type count = npos) const noexcept
                -> BasicStringView {
                REZERO_ASSERT(position <= length);

                const size_type remaining = length - position;

                if (count > remaining) {
                    count = remaining;
                }

                return BasicStringView(first + position, count);
            }

          public:
            [[nodiscard]] constexpr auto startsWith(BasicStringView value) const noexcept -> bool {
                if (value.length > length) {
                    return false;
                }

                return equals(first, value.first, value.length);
            }

            [[nodiscard]] constexpr auto endsWith(BasicStringView value) const noexcept -> bool {
                if (value.length > length) {
                    return false;
                }

                return equals(first + length - value.length, value.first, value.length);
            }

            [[nodiscard]] constexpr auto operator==(BasicStringView other) const noexcept -> bool {
                return length == other.length && equals(first, other.first, length);
            }

            [[nodiscard]] constexpr auto operator!=(BasicStringView other) const noexcept -> bool {
                return !(*this == other);
            }

          public:
            [[nodiscard]] constexpr auto find(Char value, size_type position = 0) const noexcept -> size_type {
                if (position >= length) {
                    return npos;
                }

                for (size_type i = position; i < length; ++i) {
                    if (first[i] == value) {
                        return i;
                    }
                }

                return npos;
            }

            [[nodiscard]] constexpr auto find(BasicStringView value, size_type position = 0) const noexcept
                -> size_type {
                if (position > length) {
                    return npos;
                }

                if (value.empty()) {
                    return position;
                }

                if (value.length > length - position) {
                    return npos;
                }

                const size_type last = length - value.length;

                for (size_type i = position; i <= last; ++i) {
                    if (equals(first + i, value.first, value.length)) {
                        return i;
                    }
                }

                return npos;
            }

            [[nodiscard]] constexpr auto findFirstOf(BasicStringView values, size_type position = 0) const noexcept
                -> size_type {
                if (position >= length) {
                    return npos;
                }

                for (size_type i = position; i < length; ++i) {
                    for (size_type j = 0; j < values.length; ++j) {
                        if (first[i] == values[j]) {
                            return i;
                        }
                    }
                }

                return npos;
            }

          private:
            [[nodiscard]] static constexpr auto cstrLength(const Char *str) noexcept -> size_type {
                size_type length = 0;

                while (str[length] != Char{}) {
                    ++length;
                }

                return length;
            }

            [[nodiscard]] static constexpr auto equals(const Char *lhs, const Char *rhs, size_type size) noexcept
                -> bool {
                for (size_type i = 0; i < size; ++i) {
                    if (lhs[i] != rhs[i]) {
                        return false;
                    }
                }

                return true;
            }

          private:
            const Char *first = nullptr;
            size_type length = 0;
        };
        using StringView = BasicStringView<char>;
        using u8StringView = BasicStringView<char8_t>;

        struct NoneType {
            explicit constexpr NoneType() noexcept = default;
        };

        inline constexpr NoneType None{};

        template <typename T> struct Option {
            union Storage {
                T value;

                constexpr Storage() noexcept {}
                ~Storage() noexcept {};
            };

            using ValueType = T;

            constexpr Option() noexcept : storage{}, hasValue(false) {}

            constexpr Option(NoneType) noexcept : storage{}, hasValue(false) {}

            template <typename... Args>
            constexpr explicit Option(std::in_place_t, Args &&...args) : storage{}, hasValue(true) {
                std::construct_at(std::addressof(storage.value), std::forward<Args>(args)...);
            }

            constexpr Option(const T &val) : storage{}, hasValue(true) {
                std::construct_at(std::addressof(storage.value), val);
            }

            constexpr Option(T &&val) noexcept(std::is_nothrow_move_constructible_v<T>) : storage{}, hasValue(true) {
                std::construct_at(std::addressof(storage.value), std::move(val));
            }
            constexpr ~Option() { reset(); }

            constexpr Option(const Option &other) : storage{}, hasValue(other.hasValue) {
                if (hasValue) {
                    std::construct_at(std::addressof(storage.value), other.storage.value);
                }
            }

            constexpr Option(Option &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
                : storage{}, hasValue(other.hasValue) {
                if (hasValue) {
                    std::construct_at(std::addressof(storage.value), std::move(other.storage.value));
                    other.reset();
                }
            }
            Option &operator=(NoneType) noexcept {
                reset();
                return *this;
            }

            constexpr Option &operator=(const Option &other) {
                if (this != std::addressof(other)) {
                    if (other.hasValue) {
                        if (hasValue) {
                            storage.value = other.storage.value; // 已持有值时直接赋值
                        } else {
                            std::construct_at(std::addressof(storage.value), other.storage.value);
                            hasValue = true;
                        }
                    } else {
                        reset();
                    }
                }
                return *this;
            }

            constexpr Option &operator=(Option &&other) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                                 std::is_nothrow_move_constructible_v<T>) {
                if (this != std::addressof(other)) {
                    if (other.hasValue) {
                        if (hasValue) {
                            storage.value = std::move(other.storage.value);
                        } else {
                            std::construct_at(std::addressof(storage.value), std::move(other.storage.value));
                            hasValue = true;
                        }
                        other.reset();
                    } else {
                        reset();
                    }
                }
                return *this;
            }

            constexpr void reset() noexcept {
                if (hasValue) {
                    std::destroy_at(std::addressof(storage.value)); // 替换显式析构调用 storage.value.~T()
                    hasValue = false;
                }
            }

            [[nodiscard]] constexpr T &&value() && noexcept {
                REZERO_ASSERT(hasValue);
                return std::move(storage.value);
            }

            [[nodiscard]] constexpr T &value() & noexcept {
                REZERO_ASSERT(hasValue);
                return storage.value;
            }

            [[nodiscard]] constexpr const T &value() const & noexcept {
                REZERO_ASSERT(hasValue);
                return storage.value;
            }

            [[nodiscard]] constexpr bool has_value() const noexcept { return hasValue; }
            [[nodiscard]] constexpr explicit operator bool() const noexcept { return hasValue; }

            [[nodiscard]] constexpr T &operator*() & noexcept {
                REZERO_ASSERT(hasValue);
                return storage.value;
            }
            [[nodiscard]] constexpr const T &operator*() const & noexcept {
                REZERO_ASSERT(hasValue);
                return storage.value;
            }
            [[nodiscard]] constexpr T &&operator*() && noexcept {
                REZERO_ASSERT(hasValue);
                return std::move(storage.value);
            }
            [[nodiscard]] constexpr T *operator->() noexcept {
                REZERO_ASSERT(hasValue);
                return std::addressof(storage.value);
            }
            [[nodiscard]] constexpr const T *operator->() const noexcept {
                REZERO_ASSERT(hasValue);
                return std::addressof(storage.value);
            }

            Storage storage;
            bool hasValue{false};
        };
    } // namespace Z

    namespace unicode {

        /// 从 `offset` 处解出一个 UTF-8 码点，并把 `offset` 推到下一个字符。
        ///
        /// UTF-8 的编码方式（第一个字节的高位决定长度）：
        ///
        ///     0xxxxxxx                              1 字节，码点 7 位
        ///     110xxxxx 10xxxxxx                     2 字节，码点 11 位
        ///     1110xxxx 10xxxxxx 10xxxxxx            3 字节，码点 16 位
        ///     11110xxx 10xxxxxx 10xxxxxx 10xxxxxx   4 字节，码点 21 位
        ///
        /// 解不出（越界或续字节格式不对）就返回空的 Option，offset 不动。
        ///
        /// @brief Decode one UTF-8 code point and update the offset
        ///
        /// @param text UTF-8 string
        /// @param offset current offset
        /// @return z::Option<uint32_t>
        inline auto decodeUtf8(Z::u8StringView text, std::size_t &offset) -> Z::Option<uint32_t> {
            if (offset >= text.size())
                return Z::None;

            uint8_t b0 = static_cast<uint8_t>(text[offset]);
            if (b0 < 0x80) {
                offset += 1;
                return static_cast<uint32_t>(b0);
            }

            std::size_t length{0};
            uint32_t codepoint{0};
            if ((b0 & 0xE0) == 0xC0) {
                length = 2;
                codepoint = b0 & 0x1Fu;
            } else if ((b0 & 0xF0) == 0xE0) {
                length = 3;
                codepoint = b0 & 0x0Fu;
            } else if ((b0 & 0xF8) == 0xF0) {
                length = 4;
                codepoint = b0 & 0x07u;
            } else {
                // invalid lead byte
                return Z::None;
            }

            if (offset + length > text.size())
                return Z::None;

            for (std::size_t i = 1; i < length; ++i) {
                uint8_t b = static_cast<uint8_t>(text[offset + i]);
                if ((b & 0xC0) != 0x80)
                    // invalid continuation byte
                    return Z::None;
                codepoint = (codepoint << 6) | (b & 0x3Fu);
            }

            // Reject invalid code points(overlong encoding)
            // - 0x80: minimum value for a 2-byte UTF-8 sequence.
            // - 0x800: minimum value for a 3-byte UTF-8 sequence.
            // - 0x10000: minimum value for a 4-byte UTF-8 sequence.
            // - 0x10FFFF: maximum valid Unicode code point.
            // - 0xD800-0xDFFF: UTF-16 surrogate range.
            bool overlong = (length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
                            (length == 4 && codepoint < 0x10000);
            if (overlong || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
                // invalid code point
                return Z::None;

            offset += length;
            return codepoint;
        }

        /// Writes `codepoint` at `out`, which must have room for at most 4 bytes.
        /// Returns the number of bytes written. Surrogates and code points
        /// past U+10FFFF are written as U+FFFD so a caller can never produce
        /// invalid UTF-8.
        inline auto encodeUtf8(std::uint32_t codepoint, char *out) noexcept -> std::size_t {
            auto value = codepoint;
            if (value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
                value = 0xFFFDu;
            }

            if (value < 0x80u) {
                out[0] = static_cast<char>(value);
                return 1;
            }
            if (value < 0x800u) {
                out[0] = static_cast<char>(0xC0u | (value >> 6));
                out[1] = static_cast<char>(0x80u | (value & 0x3Fu));
                return 2;
            }
            if (value < 0x10000u) {
                out[0] = static_cast<char>(0xE0u | (value >> 12));
                out[1] = static_cast<char>(0x80u | ((value >> 6) & 0x3Fu));
                out[2] = static_cast<char>(0x80u | (value & 0x3Fu));
                return 3;
            }
            out[0] = static_cast<char>(0xF0u | (value >> 18));
            out[1] = static_cast<char>(0x80u | ((value >> 12) & 0x3Fu));
            out[2] = static_cast<char>(0x80u | ((value >> 6) & 0x3Fu));
            out[3] = static_cast<char>(0x80u | (value & 0x3Fu));
            return 4;
        }

        /// Offset of the code point that ends at `offset`, i.e. one caret step to
        /// the left. Continuation bytes are skipped, so a caret never lands in the
        /// middle of a code point.
        inline auto previousCodepoint(Z::u8StringView text, std::size_t offset) noexcept -> std::size_t {
            if (offset == 0) {
                return 0;
            }
            std::size_t index = offset > text.size() ? text.size() : offset;
            index -= 1;
            while (index > 0 && (static_cast<std::uint8_t>(text[index]) & 0xC0u) == 0x80u) {
                index -= 1;
            }
            return index;
        }

        /// Offset of the code point starting at `offset`, i.e. one caret step to
        /// the right.
        inline auto nextCodepoint(Z::u8StringView text, std::size_t offset) noexcept -> std::size_t {
            if (offset >= text.size()) {
                return text.size();
            }
            std::size_t index = offset + 1;
            while (index < text.size() && (static_cast<std::uint8_t>(text[index]) & 0xC0u) == 0x80u) {
                index += 1;
            }
            return index;
        }
    } // namespace unicode

    using Pixel32 = std::uint32_t;

    struct Pixel {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        constexpr Pixel() noexcept = default;
        constexpr Pixel(float red, float green, float blue, float alpha = 1.0f) noexcept
            : r(red), g(green), b(blue), a(alpha) {}
        constexpr Pixel(const Pixel &, float alpha) noexcept;

        /// Builds a color from a 0xRRGGBBAA literal, i.e. the most significant byte
        /// is the red channel and the least significant byte is alpha. Note that
        /// toColor32() uses the opposite (little endian memory) order on purpose: it
        /// has to produce R, G, B, A bytes for the GPU.
        static constexpr Pixel fromRgba(std::uint32_t value, float alphaOverride = -1.0f) noexcept {
            return Pixel(static_cast<float>((value >> 24) & 0xFF) / 255.0f,
                         static_cast<float>((value >> 16) & 0xFF) / 255.0f,
                         static_cast<float>((value >> 8) & 0xFF) / 255.0f,
                         alphaOverride >= 0.0f ? alphaOverride : static_cast<float>(value & 0xFF) / 255.0f);
        }

        constexpr Pixel withAlpha(float alpha) const noexcept { return Pixel(r, g, b, alpha); }

        /// Channel wise interpolation toward `other`: zero keeps this color, one
        /// gives `other`. Alpha is interpolated as well, so this - rather than the
        /// scaled add operators - is what a cross fade wants.
        constexpr Pixel mix(const Pixel &other, float t) const noexcept {
            return Pixel(r + (other.r - r) * t, g + (other.g - g) * t, b + (other.b - b) * t,
                         a + (other.a - a) * t);
        }

        constexpr Pixel operator*(float scalar) const noexcept { return Pixel(r * scalar, g * scalar, b * scalar, a); }

        constexpr Pixel operator+(const Pixel &other) const noexcept {
            return Pixel(r + other.r, g + other.g, b + other.b, a + other.a);
        }

        constexpr bool operator==(const Pixel &) const noexcept = default;

        constexpr Pixel32 toPixel32() const noexcept {
            const auto channel = [](float value) -> std::uint32_t {
                const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
            };
            return (channel(r) << 0) | (channel(g) << 8) | (channel(b) << 16) | (channel(a) << 24);
        }
    };

    struct Vector2 {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vector2() noexcept = default;
        constexpr Vector2(float xValue, float yValue) noexcept : x(xValue), y(yValue) {}
        constexpr explicit Vector2(float scalar) noexcept : x(scalar), y(scalar) {}

        constexpr Vector2 operator+(Vector2 other) const noexcept { return Vector2(x + other.x, y + other.y); }
        constexpr Vector2 operator-(Vector2 other) const noexcept { return Vector2(x - other.x, y - other.y); }
        constexpr Vector2 operator*(Vector2 other) const noexcept { return Vector2(x * other.x, y * other.y); }
        constexpr Vector2 operator/(Vector2 other) const noexcept { return Vector2(x / other.x, y / other.y); }
        constexpr Vector2 operator*(float scalar) const noexcept { return Vector2(x * scalar, y * scalar); }
        constexpr Vector2 operator/(float scalar) const noexcept { return Vector2(x / scalar, y / scalar); }
        constexpr Vector2 operator-() const noexcept { return Vector2(-x, -y); }

        constexpr Vector2 &operator+=(Vector2 other) noexcept {
            x += other.x;
            y += other.y;
            return *this;
        }
        constexpr Vector2 &operator-=(Vector2 other) noexcept {
            x -= other.x;
            y -= other.y;
            return *this;
        }
        constexpr Vector2 &operator*=(float scalar) noexcept {
            x *= scalar;
            y *= scalar;
            return *this;
        }
        constexpr Vector2 &operator/=(float scalar) noexcept {
            x /= scalar;
            y /= scalar;
            return *this;
        }

        constexpr bool operator==(const Vector2 &) const noexcept = default;
    };
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        constexpr Rect() noexcept = default;
        constexpr Rect(float xValue, float yValue, float widthValue, float heightValue) noexcept
            : x(xValue), y(yValue), width(widthValue), height(heightValue) {}
        constexpr Rect(Vector2 position, Vector2 size) noexcept
            : x(position.x), y(position.y), width(size.x), height(size.y) {}

        static constexpr Rect fromPoints(Vector2 minimum, Vector2 maximum) noexcept {
            return Rect(minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y);
        }

        constexpr Vector2 position() const noexcept { return Vector2(x, y); }
        constexpr Vector2 size() const noexcept { return Vector2(width, height); }
        constexpr Vector2 minPoint() const noexcept { return Vector2(x, y); }
        constexpr Vector2 maxPoint() const noexcept { return Vector2(x + width, y + height); }
        constexpr Vector2 center() const noexcept { return Vector2(x + width * 0.5f, y + height * 0.5f); }

        constexpr float minX() const noexcept { return x; }
        constexpr float minY() const noexcept { return y; }
        constexpr float maxX() const noexcept { return x + width; }
        constexpr float maxY() const noexcept { return y + height; }

        constexpr bool isEmpty() const noexcept { return width <= 0.0f || height <= 0.0f; }

        constexpr bool contains(Vector2 point) const noexcept {
            return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
        }

        constexpr bool contains(const Rect &other) const noexcept {
            return other.x >= x && other.y >= y && other.maxX() <= maxX() && other.maxY() <= maxY();
        }

        constexpr bool overlaps(const Rect &other) const noexcept {
            return other.x < maxX() && other.y < maxY() && other.maxX() > x && other.maxY() > y;
        }

        /// Intersection of this rectangle with \p other. May be empty.
        constexpr Rect intersected(const Rect &other) const noexcept {
            const float minimumX = x > other.x ? x : other.x;
            const float minimumY = y > other.y ? y : other.y;
            const float maximumX = maxX() < other.maxX() ? maxX() : other.maxX();
            const float maximumY = maxY() < other.maxY() ? maxY() : other.maxY();
            return fromPoints(Vector2(minimumX, minimumY), Vector2(maximumX > minimumX ? maximumX : minimumX,
                                                                   maximumY > minimumY ? maximumY : minimumY));
        }
    };
} // namespace ReZeroGui