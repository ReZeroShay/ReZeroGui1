#pragma once
#include <span>
#include <vector>
#include <array>
#include <cstring>
#include <string_view>
#include <ReZeroGui/Allocator.h>
#include <ReZeroGui/Style.h>
#include <ReZeroGui/Layout.h>
#include <ReZeroGui/Draw.h>
#include <ReZeroGui/Input.h>
#include <ReZeroGui/Font.h>

namespace ReZeroGui {

    namespace Misc {
        constexpr std::uint32_t hashCombine(std::uint32_t lhs, std::uint32_t rhs) noexcept {
            constexpr std::uint32_t Prime = 16777619u;
            std::uint32_t result = lhs == 0 ? 2166136261u : lhs;
            for (int byteIndex = 0; byteIndex < 4; byteIndex += 1) {
                result ^= (rhs >> (byteIndex * 8)) & 0xFFu;
                result *= Prime;
            }
            return result == 0 ? 1u : result;
        }
        // FNV-1a
        constexpr std::uint32_t hashBytes(const char *data, std::size_t size,
                                          std::uint32_t seed = 2166136261u) noexcept {
            constexpr std::uint32_t Prime = 16777619u;
            std::uint32_t result = seed;
            for (std::size_t index = 0; index < size; ++index) {
                result ^= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[index]));
                result *= Prime;
            }
            return result == 0 ? 1u : result;
        }

        constexpr std::uint32_t hashString(std::string_view text, std::uint32_t seed = 2166136261u) noexcept {
            return hashBytes(text.data(), text.size(), seed);
        }
    }; // namespace Misc

    // Non-owning RGBA8 image view.
    struct Image {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::span<const std::uint8_t> rgba; // must be width*height*4
    };

    enum class RendererErrorCode {
        CreatePipelineFailed,
    };

    struct RendererError {
        RendererErrorCode code;
        int32_t nativeCode;
    };

    template <typename T>
    concept RendererTextureTraits = requires(T &r, const Image &image, const TextureId id) {
        { r.createTexture(image) } noexcept -> std::same_as<TextureId>;
        { r.destroyTexture(id) } noexcept;
    };

    template <typename T>
    concept RendererFrameTraits = requires(T &r) {
        { r.beginFrame() } noexcept;
        { r.endFrame() } noexcept;
    };

    template <typename T>
    concept RendererLifecycleTraits = requires(T &r, const void *data) {
        { r.initialize() } noexcept;
        { r.shutdown() } noexcept;
    };
    template <typename T>
    concept RendererRenderTraits = requires(T &r, const void *data) {
        { r.render(data) } noexcept;
    };

    template <typename T>
    concept RendererTraits =
        RendererLifecycleTraits<T> && RendererFrameTraits<T> && RendererTextureTraits<T> && RendererRenderTraits<T>;

    class Renderer {
      private:
        struct Impl {
            virtual ~Impl() = default;

            virtual auto initialize() noexcept -> void = 0;
            virtual auto shutdown() noexcept -> void = 0;
            virtual auto render(void *data) noexcept -> void = 0;
            virtual auto createTexture(const Image &image) noexcept -> TextureId = 0;
            virtual auto destroyTexture(TextureId id) noexcept -> void = 0;
        };

        template <RendererTraits R> struct RendererImplFor final : Impl {
            R impl;

            void initialize() noexcept override { impl.initialize(); }

            void shutdown() noexcept override { impl.shutdown(); }

            void render(void *data) noexcept override { impl.render(data); }
        };

      public:
        template <RendererTraits T> explicit Renderer(RendererImplFor<T> &impl) noexcept : impl(&impl) {}

        auto initialize() noexcept { impl->initialize(); }

        auto shutdown() noexcept { impl->shutdown(); }

        auto render(void *data) noexcept { impl->render(data); }

        auto createTexture(const Image &image) noexcept -> TextureId { return impl->createTexture(image); };
        auto destroyTexture(TextureId id) noexcept { impl->destroyTexture(id); };

      private:
        Impl *impl;
    };

    struct RendererParams {
        void *windowsHandle{nullptr};
        void *deviceHandle{nullptr};
        void *swapChainHandle{nullptr};
    };
    enum class ArrowDirection : std::uint8_t {
        Right,
        Down,
    };
    using Id = std::uint32_t;
    constexpr Id InvalidId = std::numeric_limits<Id>::max();

    /// How the position set by WindowScope::position() is applied.
    ///
    /// Widget code runs every frame. Setting the position every frame
    /// would stop the user from moving the window.
    enum class PositionMode : std::uint8_t {
        /// Set the position when the window is first used.
        /// After that, keep the user's position.
        FirstUseEver,

        /// Set the position once, then stop applying it.
        /// Use this to move a window to a new position.
        Once,

        /// Set the position on every frame.
        /// Use this for windows that must stay in one place.
        Always,
    };
    static constexpr std::size_t NoWindow = static_cast<std::size_t>(-1);    struct WindowParams {
        char *title = nullptr;
        Vector2 position{0.0f, 0.0f};
        /// Negative means "not specified": the axis then keeps its current size
        /// (windowMinimumSize first, shrink wrapped to the contents afterwards).
        Vector2 size{-1.0f, -1.0f};
        bool hasPosition = false;
        PositionMode positionMode = PositionMode::FirstUseEver;
        bool hasSize = false;
        Vector2 padding{-1.0f, -1.0f};
        /// When true both dimensions shrink wrap the window contents.
        bool autoResize = true;
        bool hasWidth = false;
        bool hasHeight = false;
        bool hasTitleBar = true;
        bool movable = true;
        bool drawBackground = true;
        bool hasCollapsed = false;
        bool collapsed = false;
        const bool *openState = nullptr;
    };

    struct WindowData {
        Id id = 0;
        Rect rect;
        Vector2 padding;
        Vector2 fixedSize{-1.0f, -1.0f};
        bool hasTitleBar = true;
        bool movable = true;
        bool drawBackground = true;
        bool collapsed = false;
        bool skipItems = false;
        /// Set once an explicit position has been applied; used by PositionMode.
        bool positionApplied = false;
        std::size_t clipDepth = 0;
    };

    namespace detail {
        /// Length of the NUL terminated string inside a fixed size buffer. The
        /// buffer's own size is the hard limit, so an unterminated buffer cannot
        /// run away.
        inline auto textLength(std::span<const char> buffer) noexcept -> std::size_t {
            std::size_t length = 0;
            while (length < buffer.size() && buffer[length] != '\0') {
                length += 1;
            }
            return length;
        }

        /// Makes room for `count` bytes at `offset` and copies `text` in. The tail
        /// is dropped when the buffer runs out, and the terminating NUL is kept.
        /// Returns true when anything was written.
        inline auto insertText(std::span<char> buffer, std::size_t offset, const char *text,
                               std::size_t count) noexcept -> bool {
            if (buffer.size() < 2 || count == 0) {
                return false;
            }
            const std::size_t capacity = buffer.size() - 1;
            const std::size_t length = textLength(buffer);
            if (offset > length) {
                offset = length;
            }
            if (length + count > capacity) {
                count = capacity - length;
            }
            if (count == 0) {
                return false;
            }

            std::memmove(buffer.data() + offset + count, buffer.data() + offset, length - offset);
            std::memcpy(buffer.data() + offset, text, count);
            buffer[length + count] = '\0';
            return true;
        }

        /// Removes [from, to) from a NUL terminated buffer. Returns true when
        /// anything was removed.
        inline auto eraseText(std::span<char> buffer, std::size_t from, std::size_t to) noexcept -> bool {
            const std::size_t length = textLength(buffer);
            if (from >= to || from >= length) {
                return false;
            }
            if (to > length) {
                to = length;
            }
            std::memmove(buffer.data() + from, buffer.data() + to, length - to);
            buffer[length - (to - from)] = '\0';
            return true;
        }

        /// Animated values that belong to a widget id rather than to the caller.
        ///
        /// Every slot is stamped with the frame it was last asked for and swept in
        /// endFrame(), so a widget that stops being drawn cannot leave a stale
        /// animation behind, and a widget that comes back is born at its target
        /// instead of replaying the animation. The table is a fixed array because
        /// animation state has to outlive the frame arena, and because a stable
        /// address is what lets value() hand out a reference.
        class AnimationStore {
          public:
            static constexpr std::size_t Capacity = 128;

            /// Moves the value of `id` one frame toward `target` and returns the new
            /// value. A new id starts settled at `target`, so a widget that appears
            /// for the first time does not play a catch up animation, and a widget
            /// that was gone for a frame starts over rather than resuming.
            ///
            /// Nothing is handed out by reference: a slot is recycled the moment its
            /// widget stops being declared, so a stored reference would silently
            /// alias another widget's animation.
            auto advance(Id id, float target, float tau, float dt, std::uint32_t frame) noexcept -> float {
                std::size_t freeIndex = Capacity;
                for (std::size_t index = 0; index < Capacity; index += 1) {
                    AnimationSlot &slot = slots[index];
                    if (slot.id == id) {
                        // Two widgets claiming one id in the same frame would animate
                        // the same value twice, so ids have to be unique per frame.
                        REZERO_ASSERT_MSG(slot.lastFrame != frame, "two widgets share one id this frame");
                        slot.lastFrame = frame;
                        slot.value = Z::damp(slot.value, target, tau, dt);
                        return slot.value;
                    }
                    if (freeIndex == Capacity && slot.id == InvalidId) {
                        freeIndex = index;
                    }
                }
                if (freeIndex < Capacity) {
                    AnimationSlot &slot = slots[freeIndex];
                    slot.id = id;
                    slot.lastFrame = frame;
                    slot.value = target;
                    return slot.value;
                }
                // Full: keep the widget animating without remembering it instead of
                // growing the table. It shows up as an animation that snaps.
                REZERO_ASSERT_MSG(false, "animation store is full");
                return target;
            }

            /// Forgets every slot that was not asked for during `frame`.
            void sweep(std::uint32_t frame) noexcept {
                for (AnimationSlot &slot : slots) {
                    if (slot.id != InvalidId && slot.lastFrame != frame) {
                        slot.id = InvalidId;
                    }
                }
            }

          private:
            struct AnimationSlot {
                Id id = InvalidId;
                std::uint32_t lastFrame = 0;
                float value = 0.0f;
            };

            std::array<AnimationSlot, Capacity> slots{};
        };
    } // namespace detail

    struct Context {
        Context(AllocatorView allocator) noexcept
            : backgroundList(allocator), foregroundList(allocator), textureCommandQueue(allocator), atlas(allocator) {}

        Context(const Context &) = delete;
        Context &operator=(const Context &) = delete;

        /// Sets the window position based on PositionMode.
        /// Prevents repeated position changes.
        // FirstUseEver applies the position only once.
        // Once can apply it again later.
        void applyWindowPosition(WindowData &window, const WindowParams &params) noexcept {
            if (!params.hasPosition) {
                return;
            }
            if (params.positionMode == PositionMode::Always) {
                window.rect.x = params.position.x;
                window.rect.y = params.position.y;
                return;
            }
            if (window.positionApplied) {
                return;
            }
            window.rect.x = params.position.x;
            window.rect.y = params.position.y;
            window.positionApplied = params.positionMode == PositionMode::FirstUseEver;
        }

        std::size_t findOrCreateWindow(Id id) noexcept {
            for (std::size_t index = 0; index < windows.size(); ++index) {
                if (windows[index].id == id) {
                    return index;
                }
            }
            WindowData window;
            window.id = id;
            window.rect = Rect(Vector2(32.0f, 32.0f), style.windowMinimumSize);
            windows.push_back(window);
            return windows.size() - 1;
        }

        bool beginWindow(const WindowParams &params) noexcept {
            if (params.openState != nullptr && !*params.openState) {
                return false;
            }

            const std::size_t index = findOrCreateWindow(Misc::hashString(params.title));
            WindowData &window = windows[index];

            applyWindowPosition(window, params);
            window.fixedSize = params.size;
            if (params.hasWidth) {
                window.rect.width = params.size.x;
            }
            if (params.hasHeight) {
                window.rect.height = params.size.y;
            }
            window.padding = params.padding.x < 0.0f ? style.windowPadding : params.padding;

            window.hasTitleBar = params.hasTitleBar;
            window.movable = params.movable;
            window.drawBackground = params.drawBackground;
            if (params.hasCollapsed) {
                window.collapsed = params.collapsed;
            }
            window.skipItems = window.collapsed;

            const float titleBarHeight = window.hasTitleBar ? style.fontSize + style.framePadding.y * 2.0f : 0.0f;

            // Keep at least a sliver of the window reachable on screen.
            window.rect.x =
                Z::clampFloat(window.rect.x, 32.0f - window.rect.width, std::max(0.0f, displaySize.x - 32.0f));
            window.rect.y = Z::clampFloat(window.rect.y, 0.0f, std::max(0.0f, displaySize.y - 32.0f));

            const Rect titleBarRect(window.rect.x, window.rect.y, window.rect.width, titleBarHeight);
            const float triangleSize = style.fontSize * 0.6f;

            if (window.hasTitleBar && window.movable) {
                const Id moveId = Misc::hashCombine(window.id, Id(0x9E3779B9u));
                if (isMouseHoveringRect(titleBarRect, moveId) && input.isMouseClicked(Input::MouseButton::Left)) {
                    activeId = moveId;
                    windowDragOffset = input.currentMousePosition() - window.rect.position();
                }
                if (activeId == moveId) {
                    if (input.isMouseDown(Input::MouseButton::Left)) {
                        window.rect.x = input.currentMousePosition().x - windowDragOffset.x;
                        window.rect.y = input.currentMousePosition().y - windowDragOffset.y;
                        // Recompute after the move so widgets land where they are drawn.
                        window.rect.x = Z::clampFloat(window.rect.x, 32.0f - window.rect.width,
                                                      std::max(0.0f, displaySize.x - 32.0f));
                        window.rect.y = Z::clampFloat(window.rect.y, 0.0f, std::max(0.0f, displaySize.y - 32.0f));
                    } else {
                        activeId = InvalidId;
                    }
                }
            }

            const float bodyHeight = window.collapsed ? titleBarHeight : window.rect.height;
            const Rect bodyRect(window.rect.x, window.rect.y, window.rect.width, bodyHeight);

            foregroundList.pushClipRect(bodyRect);
            window.clipDepth = foregroundList.clipDepth();

            if (window.drawBackground) {
                const bool active = hoveredWindowId == window.id;
                foregroundList.addRectFilled(bodyRect.position(), bodyRect.maxPoint(),
                                             style.color(ColorIndex::WindowBackground), style.windowRounding);
                if (window.hasTitleBar) {
                    Pixel titleColor = style.color(ColorIndex::TitleBackground);
                    if (window.collapsed) {
                        titleColor = style.color(ColorIndex::TitleBackgroundCollapsed);
                    } else if (active) {
                        titleColor = style.color(ColorIndex::TitleBackgroundActive);
                    }
                    foregroundList.addRectFilled(titleBarRect.position(), titleBarRect.maxPoint(), titleColor,
                                                 style.windowRounding, DrawCorner::Top);

                    const Id collapseId = Misc::hashCombine(window.id, Id(0x85EBCA77u));
                    const Rect triangleRect(titleBarRect.x + style.framePadding.x,
                                            titleBarRect.y + titleBarHeight * 0.5f - triangleSize * 0.5f, triangleSize,
                                            triangleSize);
                    if (buttonBehavior(triangleRect, collapseId, nullptr, nullptr)) {
                        window.collapsed = !window.collapsed;
                        window.skipItems = window.collapsed;
                    }
                    renderArrow(triangleRect.center(), window.collapsed ? ArrowDirection::Right : ArrowDirection::Down,
                                style.color(ColorIndex::Text), triangleSize * 0.4f);

                    const Z::StringView title(params.title);
                    const Vector2 titleSize = calculateTextSize(title);
                    const float available =
                        titleBarRect.width - triangleSize - style.framePadding.x * 2.0f - style.itemInnerSpacing.x;
                    const float titleX = titleBarRect.x + style.framePadding.x + triangleSize +
                                         style.itemInnerSpacing.x +
                                         std::max(0.0f, (available - titleSize.x) * style.windowTitleAlignX);
                    const float titleY = titleBarRect.y + style.framePadding.y;
                    renderText(Vector2(titleX, titleY), title, style.color(ColorIndex::Text), defaultFont(), 0.0f);
                }
                if (style.windowBorderSize > 0.0f) {
                    foregroundList.addRect(bodyRect.position(), bodyRect.maxPoint(),
                                           style.color(ColorIndex::WindowBorder), style.windowRounding, DrawCorner::All,
                                           style.windowBorderSize);
                }
            }

            const Vector2 contentMin(window.rect.x + window.padding.x,
                                     window.rect.y + titleBarHeight + window.padding.y);
            Vector2 contentMax(std::max(contentMin.x, window.rect.x + window.rect.width - window.padding.x),
                               std::max(contentMin.y, window.rect.y + window.rect.height - window.padding.y));
            if (window.collapsed) {
                contentMax = contentMin;
                window.skipItems = true;
            }

            pushRegion(contentMin, contentMax);
            windowStack.push_back(currentWindowIndex);
            currentWindowIndex = index;
            // Widgets hash their labels against the current id seed, so scoping the
            // window id here is what keeps two windows from sharing a "OK" button.
            idStack.push_back(window.id);
            return true;
        }

        void endWindow() noexcept {
            if (currentWindowIndex == NoWindow || windowStack.empty()) {
                return;
            }

            WindowData &window = windows[currentWindowIndex];
            const float titleBarHeight = window.hasTitleBar ? style.fontSize + style.framePadding.y * 2.0f : 0.0f;

            const Vector2 contentSize = hasLayout() ? currentRegion().contentSize() : Vector2();
            popRegion();

            if (window.collapsed) {
                window.rect.height = titleBarHeight;
            } else {
                if (window.fixedSize.x < 0.0f) {
                    window.rect.width = std::max(style.windowMinimumSize.x, contentSize.x + window.padding.x * 2.0f);
                }
                if (window.fixedSize.y < 0.0f) {
                    window.rect.height =
                        std::max(style.windowMinimumSize.y, contentSize.y + window.padding.y * 2.0f + titleBarHeight);
                }
            }

            // clipDepth was captured *after* this window pushed its own clip
            // rectangle, so exactly one pop is due. Containers are expected to keep
            // the clip stack balanced; an unbalanced stack is a bug and is asserted
            // in debug builds instead of being silently papered over.
            REZERO_ASSERT_MSG(foregroundList.clipDepth() == window.clipDepth,
                              "unbalanced clip stack: a container leaked clip rectangles");
            foregroundList.popClipRect();

            currentWindowIndex = windowStack.back();
            windowStack.pop_back();
            if (!idStack.empty()) {
                idStack.pop_back();
            }
        }

        auto renderArrow(Vector2 center, ArrowDirection direction, Pixel color, float size) noexcept -> void {
            if (direction == ArrowDirection::Down) {
                foregroundList.addTriangleFilled(Vector2(center.x - size, center.y - size * 0.5f),
                                                 Vector2(center.x + size, center.y - size * 0.5f),
                                                 Vector2(center.x, center.y + size * 0.5f), color);
            } else {
                foregroundList.addTriangleFilled(Vector2(center.x - size * 0.5f, center.y - size),
                                                 Vector2(center.x - size * 0.5f, center.y + size),
                                                 Vector2(center.x + size * 0.5f, center.y), color);
            }
        }
        void beginFrame(float deltaTime = 0.0f) noexcept {
            frameIndex += 1;
            frameDelta = Z::clampFloat(deltaTime, 0.0f, MaxFrameDelta);

            backgroundList.onFrameBegin();
            foregroundList.onFrameBegin();
            backgroundList.setDisplayRect(Rect(Vector2(), displaySize));
            foregroundList.setDisplayRect(Rect(Vector2(), displaySize));

            input.onFrameBegin(deltaTime);
            // Rasterizes the glyph atlas and publishes it as a texture the first
            // time a frame starts; afterwards this is two flag checks.
            ensureFontAtlas();

            layoutDepth = 0;
            layoutOverflow = 0;

            // Resolve which window sits under the cursor using last frame's layout;
            // windows declared later in a frame are considered on top.
            hoveredWindowId = InvalidId;
            for (const WindowData &window : windows) {
                if (window.rect.contains(input.currentMousePosition())) {
                    hoveredWindowId = window.id;
                }
            }

            hoveredId = InvalidId;
            keyboardCapture = false;
        }

        void endFrame() noexcept {
            // Keyboard focus only survives while its field keeps being declared, so a
            // field that disappears cannot silently own the keyboard when it returns.
            if (focusedTextId != InvalidId && focusedTextFrame != frameIndex) {
                focusedTextId = InvalidId;
            }
            // Animated widget values follow the same rule: no declaration this frame,
            // no slot.
            animations.sweep(frameIndex);

            backgroundList.onFrameEnd();
            foregroundList.onFrameEnd();

            drawData.background = &backgroundList;
            drawData.foreground = &foregroundList;
            drawData.resourceCommands = textureCommands();
            drawData.displaySize = displaySize;
            drawData.valid = true;

            input.onFrameEnd();
        }

        auto drawItems() noexcept -> DrawData & { return drawData; }

        /// The draw lists behind drawItems(). Exposed so hosts can emit their own
        /// geometry - a custom widget, or a quad sampling a texture handed out by
        /// createTexture() - in the same batch as the built in widgets.
        auto background() noexcept -> DrawList & { return backgroundList; }

        /// The style in use. The non-const overload is what hosts configure before
        /// (or between) frames: fonts, sizes, spacing and colors all live here.
        ///
        /// Values are read while a frame is built, so a change applies to the next
        /// frame. The one exception is Style::fontSize, which is baked into the
        /// glyph atlas when it is first rasterized.
        Style &currentStyle() noexcept { return style; }
        const Style &currentStyle() const noexcept { return style; }

        auto currentId(std::string_view seed) const noexcept -> Id { return Misc::hashString(seed, idSeed()); }

        /// Core of both calcTextSize overloads. Templated on the character type so
        /// callers can spell their string as char or char8_t.
        template <typename Char>
        auto calculateTextSize(Z::BasicStringView<Char> text, FontId font = InvalidFont,
                               float wrapWidth = 0.0f) const noexcept -> Vector2 {
            const FontId effectiveFont = resolveFont(font);
            if (effectiveFont == InvalidFont) {
                return Vector2();
            }

            const Z::u8StringView bytes = asUtf8(text);
            const float lineHeight = atlas.lineHeight(effectiveFont);
            const float letterSpacing = style.letterSpacing;
            float width = 0.0f;
            float maximumWidth = 0.0f;
            float lineCount = 1.0f;

            std::size_t offset = 0;
            while (offset < bytes.size()) {
                auto codepoint = unicode::decodeUtf8(bytes, offset);
                if (!codepoint) {
                    break;
                }
                if (*codepoint == '\n') {
                    maximumWidth = Z::max(maximumWidth, width);
                    width = 0.0f;
                    lineCount += 1.0f;
                    continue;
                }
                const float advance = atlas.glyphAdvance(effectiveFont, *codepoint);
                if (wrapWidth > 0.0f && width > 0.0f && width + advance > wrapWidth) {
                    maximumWidth = std::max(maximumWidth, width);
                    width = 0.0f;
                    lineCount += 1.0f;
                }
                // Tracking is added after every glyph, exactly like renderText
                // advances the pen, so measuring and drawing agree.
                width += advance + letterSpacing;
            }
            return Vector2(Z::max(maximumWidth, width), lineCount * lineHeight);
        }

        /// Draws `text` with its top-left corner at `position`, into the
        /// foreground layer. Lines are broken on '\n', and on spaces once
        /// `wrapWidth` is exceeded (zero disables wrapping).
        template <typename Char>
        void renderText(Vector2 position, Z::BasicStringView<Char> text, Pixel color, FontId font,
                        float wrapWidth) noexcept {
            const FontId effectiveFont = resolveFont(font);
            if (effectiveFont == InvalidFont || fontTexture == InvalidTexture) {
                return;
            }

            const Z::u8StringView bytes = asUtf8(text);
            const float lineHeight = atlas.lineHeight(effectiveFont);
            const float letterSpacing = style.letterSpacing;
            const float startX = position.x;
            float penX = position.x;
            // The pen sits on the baseline, which is one ascent below the top of
            // the line box.
            float baselineY = position.y + atlas.ascent(effectiveFont);

            std::size_t offset = 0;
            while (offset < bytes.size()) {
                auto codepoint = unicode::decodeUtf8(bytes, offset);
                if (!codepoint) {
                    break;
                }
                if (*codepoint == '\n') {
                    penX = startX;
                    baselineY += lineHeight;
                    continue;
                }

                float nextX = penX;
                GlyphQuad quad{};
                if (!atlas.getGlyphQuad(effectiveFont, *codepoint, &nextX, &baselineY, quad)) {
                    continue;
                }

                // Only wrap once something has been drawn on this line, otherwise
                // a single over-long glyph would wrap forever.
                if (wrapWidth > 0.0f && penX > startX && nextX - startX > wrapWidth) {
                    penX = startX;
                    baselineY += lineHeight;
                    nextX = penX;
                    if (!atlas.getGlyphQuad(effectiveFont, *codepoint, &nextX, &baselineY, quad)) {
                        continue;
                    }
                }

                // Blank glyphs report success with empty UVs so that the pen still
                // advances; they must not produce geometry.
                if (quad.uv0.x != quad.uv1.x && quad.uv0.y != quad.uv1.y) {
                    foregroundList.addTexturedQuad(quad.position0, quad.position1, quad.uv0, quad.uv1, color,
                                                   fontTexture);
                }
                // Tracking rides on top of the glyph's own advance, matching what
                // measureText adds so the two never drift apart.
                penX = nextX + letterSpacing;
            }
        }
        /// Deepest region nesting a frame may open. A window body is one level; the
        /// regions a host pushes around windows add more. The stack is a fixed array,
        /// so a frame never allocates and a Region reference stays valid across a
        /// push - currentRegion() hands one out.
        static constexpr std::size_t MaxLayoutDepth = 16;

        /// True while a window body, panel, frame or scroll content is active.
        bool hasLayout() const noexcept { return layoutDepth != 0; }

        /// Pushes a fresh region covering [minPoint, maxPoint]. Nesting deeper than
        /// MaxLayoutDepth is a bug: the push is dropped (asserted in debug builds)
        /// and its matching popRegion() is swallowed, so the stack stays balanced.
        void pushRegion(Vector2 minPoint, Vector2 maxPoint) noexcept {
            if (layoutDepth >= MaxLayoutDepth) {
                REZERO_ASSERT_MSG(false, "layout region nesting exceeds MaxLayoutDepth");
                layoutOverflow += 1;
                return;
            }
            layoutStack[layoutDepth].begin(minPoint, maxPoint);
            layoutDepth += 1;
        }

        void popRegion() noexcept {
            if (layoutOverflow > 0) {
                layoutOverflow -= 1;
                return;
            }
            if (layoutDepth > 0) {
                layoutDepth -= 1;
            }
        }

        /// Reserves \p size for an item and returns its screen space rectangle.
        /// \p textBaseline is reserved for future cross axis text alignment.
        Rect placeItem(Vector2 size, float textBaseline = -1.0f) noexcept {
            (void)textBaseline;
            if (!hasLayout()) {
                return Rect();
            }
            return currentRegion().place(size, style.itemSpacing);
        }

        auto isItemVisible(const Rect &rect) const noexcept -> bool {
            if (!hasLayout()) {
                return false;
            }
            if (currentWindowIndex != NoWindow && windows[currentWindowIndex].skipItems) {
                return false;
            }
            return foregroundList.currentClipRect().overlaps(rect);
        }

        /// Everything a widget emits ends up here.
        DrawList &drawList() noexcept { return foregroundList; }


        /// Checks if the mouse is inside the given rectangle and can interact with the item
        bool isMouseHoveringRect(const Rect &rect, Id id = 0) const noexcept {
            if (!rect.contains(input.currentMousePosition())) {
                return false;
            }
            // skipItems only exists for windows (collapsed state); panels and frames
            // keep receiving input.
            if (currentWindowIndex != NoWindow && windows[currentWindowIndex].skipItems) {
                return false;
            }
            if (!foregroundList.currentClipRect().contains(input.currentMousePosition())) {
                return false;
            }
            if (activeId != InvalidId && activeId != id) {
                return false;
            }
            return true;
        }

        /// Handles the common click behavior for buttons and other clickable items.
        bool buttonBehavior(const Rect &rect, Id id, bool *outHovered, bool *outHeld, bool enabled = true) noexcept {
            const bool hovered = enabled && isMouseHoveringRect(rect, id);
            if (hovered) {
                hoveredId = id;
            }

            bool held = false;
            bool pressed = false;
            if (hovered && input.isMouseClicked(Input::MouseButton::Left)) {
                activeId = id;
            }
            if (activeId == id) {
                if (input.isMouseDown(Input::MouseButton::Left)) {
                    held = true;
                } else {
                    pressed = rect.contains(input.currentMousePosition());
                    activeId = InvalidId;
                }
            }

            if (outHovered != nullptr) {
                *outHovered = hovered;
            }
            if (outHeld != nullptr) {
                *outHeld = held;
            }
            return pressed;
        }

        /// Handles horizontal dragging for sliders and updates the value while held.
        bool dragBehavior(const Rect &rect, Id id, float *value, float minimum, float maximum, float grabWidth,
                          bool enabled = true, bool *outHovered = nullptr) noexcept {
            const bool hovered = enabled && isMouseHoveringRect(rect, id);
            if (hovered) {
                hoveredId = id;
            }

            if (outHovered != nullptr) {
                *outHovered = hovered;
            }

            if (hovered && input.isMouseClicked(Input::MouseButton::Left)) {
                activeId = id;
            }
            if (activeId != id) {
                return false;
            }
            if (!input.isMouseDown(Input::MouseButton::Left)) {
                activeId = InvalidId;
                return false;
            }

            const float trackWidth = std::max(1.0f, rect.width - grabWidth);
            const float normalized =
                Z::clampFloat((input.currentMousePosition().x - rect.x - grabWidth * 0.5f) / trackWidth, 0.0f, 1.0f);
            *value = minimum + (maximum - minimum) * normalized;
            return true;
        }

        /// True while \p id owns the mouse: it is being held, or its window is being
        /// dragged. Widgets ask this about their own id rather than the context
        /// tracking a "last declared item". Keyboard focus is separate, see
        /// isTextFocused().
        bool isItemActive(Id id) const noexcept { return id != InvalidId && activeId == id; }

        /// True while \p id owns the keyboard, i.e. it is the text field being
        /// edited. Unlike the mouse owner this survives frames until the edit ends,
        /// which is exactly why it cannot share activeId.
        bool isTextFocused(Id id) const noexcept { return id != InvalidId && focusedTextId == id; }

        /// Ends the current line so the next item starts below this one.
        void newLine() noexcept {
            if (hasLayout()) {
                currentRegion().wrap(style.itemSpacing);
            }
        }

        /// Width left for the next item on the current line. Zero when no container
        /// is open, which makes a widget fall back to its own default.
        auto contentRegionWidth() const noexcept -> float {
            return hasLayout() ? currentRegion().availableWidth() : 0.0f;
        }

        /// Rounded frame with an optional border: the shell a button, slider or
        /// text field draws itself into.
        void renderFrame(const Rect &rect, Pixel fill, Pixel border, float rounding, bool hasBorder = true) noexcept {
            foregroundList.addRectFilled(rect.position(), rect.maxPoint(), fill, rounding);
            if (hasBorder && style.frameBorderSize > 0.0f) {
                foregroundList.addRect(rect.position(), rect.maxPoint(), border, rounding, DrawCorner::All,
                                       style.frameBorderSize);
            }
        }

        /// Tick drawn inside a checkbox / radio box.
        void renderCheckMark(const Rect &rect, Pixel color) noexcept {
            const float thickness = std::max(1.0f, rect.width * 0.16f);
            const Vector2 start(rect.x + rect.width * 0.20f, rect.y + rect.height * 0.52f);
            const Vector2 middle(rect.x + rect.width * 0.42f, rect.y + rect.height * 0.76f);
            const Vector2 end(rect.x + rect.width * 0.80f, rect.y + rect.height * 0.24f);
            foregroundList.addLine(start, middle, color, thickness);
            foregroundList.addLine(middle, end, color, thickness);
        }

        /// Caret of the text field that owns the keyboard, as a byte offset into
        /// that field's buffer. Only one single line field can be edited at a time,
        /// so the caret lives in a single slot instead of a table keyed by widget
        /// id: a click moves it to the closest code point boundary, and it is
        /// meaningless while no field has focus.
        auto caretPosition() const noexcept -> std::size_t { return caret; }

        /// Advances the animated value of `id` one frame toward `target` and returns
        /// the new value. `tau` is the time constant in seconds; zero snaps. Widgets
        /// call this once per frame - the frame's delta time is applied here, so they
        /// cannot forget it - and the value is created at `target` the first time, so
        /// a widget that appears for the first time is settled rather than playing a
        /// catch up animation. A slot nobody asked for during a frame is dropped, so
        /// a widget that comes back starts settled instead of resuming from the
        /// middle.
        ///
        /// The value is returned by value on purpose: the slot behind it is recycled
        /// as soon as the widget stops being declared, so handing out a reference
        /// would let a stored one silently alias another widget's animation.
        auto animate(Id id, float target, float tau) noexcept -> float {
            return animations.advance(id, target, tau, frameDelta, frameIndex);
        }

        /// Seconds since the previous frame, clamped so one long frame cannot
        /// teleport an animation. Zero while the host does not pass it to
        /// beginFrame().
        auto deltaTime() const noexcept -> float { return frameDelta; }

        /// Keyboard editing for a single line text field.
        ///
        /// `buffer` has to stay NUL terminated; editing never writes past
        /// `buffer.size() - 1`. The caret is context state - see caretPosition() -
        /// rather than an in/out parameter, because only one field owns the
        /// keyboard at a time. That owner is `focusedTextId`, deliberately not
        /// `activeId`: an edit lasts until Enter, Escape or a click elsewhere, while
        /// `activeId` is the mouse owner and only lives as long as a press.
        ///
        /// Movement and deletion step over whole code points, so a caret can never
        /// end up inside a multi byte character.
        void textInputBehavior(const Rect &rect, Id id, std::span<char> buffer, bool *outChanged = nullptr) noexcept {
            bool changed = false;
            if (buffer.size() < 2) {
                if (outChanged != nullptr) {
                    *outChanged = false;
                }
                return;
            }

            const bool hovered = isMouseHoveringRect(rect, id);
            if (hovered) {
                hoveredId = id;
            }
            const bool clicked = input.isMouseClicked(Input::MouseButton::Left);
            if (hovered && clicked) {
                // Clicking focuses the field and places the caret at the closest
                // code point boundary.
                focusedTextId = id;
                caret = cursorOffsetFromX(buffer, rect.x + style.framePadding.x, input.currentMousePosition().x);
            } else if (clicked && focusedTextId == id) {
                // A click anywhere else ends the edit. This is about the caret and
                // the keyboard only: the field never owns the mouse, so the rest of
                // the UI stays reachable either way.
                focusedTextId = InvalidId;
            }

            if (focusedTextId == id) {
                focusedTextFrame = frameIndex;
                // The caret belongs to this field, so this is the only place it may
                // be clamped: another field's buffer must not move it.
                const std::size_t length = detail::textLength(buffer);
                if (caret > length) {
                    caret = length;
                }

                keyboardCapture = true;
                const Z::u8StringView bytes(reinterpret_cast<const char8_t *>(buffer.data()), length);

                for (const char32_t codepoint : input.currentCharacters()) {
                    if (codepoint < 0x20 || codepoint == 0x7F) {
                        continue;
                    }
                    char encoded[4];
                    const std::size_t count = unicode::encodeUtf8(static_cast<std::uint32_t>(codepoint), encoded);
                    if (detail::insertText(buffer, caret, encoded, count)) {
                        caret += count;
                        changed = true;
                    }
                }

                if (input.isKeyPressed(Input::KeyCode::Backspace) && caret > 0) {
                    const std::size_t previous = unicode::previousCodepoint(bytes, caret);
                    if (detail::eraseText(buffer, previous, caret)) {
                        caret = previous;
                        changed = true;
                    }
                }
                if (input.isKeyPressed(Input::KeyCode::Delete) && caret < length) {
                    const std::size_t next = unicode::nextCodepoint(bytes, caret);
                    if (detail::eraseText(buffer, caret, next)) {
                        changed = true;
                    }
                }
                if (input.isKeyPressed(Input::KeyCode::LeftArrow)) {
                    caret = unicode::previousCodepoint(bytes, caret);
                }
                if (input.isKeyPressed(Input::KeyCode::RightArrow)) {
                    caret = unicode::nextCodepoint(bytes, caret);
                }
                if (input.isKeyPressed(Input::KeyCode::Home)) {
                    caret = 0;
                }
                if (input.isKeyPressed(Input::KeyCode::End)) {
                    caret = length;
                }
                // Single line field: both keys end the edit.
                if (input.isKeyPressed(Input::KeyCode::Enter) || input.isKeyPressed(Input::KeyCode::Escape)) {
                    focusedTextId = InvalidId;
                }
            }

            if (outChanged != nullptr) {
                *outChanged = changed;
            }
        }

        Context &withDisplaySize(Vector2 size) noexcept {
            displaySize = size;
            return *this;
        }

        auto io() noexcept -> Input & { return input; }

        bool wantsKeyboardCapture() const noexcept { return keyboardCapture; }
        bool wantsMouseCapture() const noexcept { return hoveredWindowId != InvalidId || activeId != InvalidId; }

        /// Applies options that were chained onto a WindowScope after the window was
        /// already begun. Layout, clipping and hit testing follow immediately; the
        /// window chrome (background, title bar, border) was already emitted with the
        /// old rectangle and is only redrawn with the new one on the next frame.
        void applyWindowParams(const WindowParams &params) noexcept {
            if (currentWindowIndex == NoWindow) {
                return;
            }
            WindowData &window = windows[currentWindowIndex];

            window.padding = params.padding.x < 0.0f ? style.windowPadding : params.padding;
            window.fixedSize = params.size;
            if (params.hasCollapsed) {
                window.collapsed = params.collapsed;
            }

            applyWindowPosition(window, params);
            if (params.hasWidth) {
                window.rect.width = params.size.x;
            }
            if (params.hasHeight) {
                window.rect.height = params.size.y;
            }

            const float titleBarHeight = window.hasTitleBar ? style.fontSize + style.framePadding.y * 2.0f : 0.0f;

            const Vector2 contentMin(window.rect.x + window.padding.x,
                                     window.rect.y + titleBarHeight + window.padding.y);
            Vector2 contentMax(std::max(contentMin.x, window.rect.x + window.rect.width - window.padding.x),
                               std::max(contentMin.y, window.rect.y + window.rect.height - window.padding.y));
            if (window.collapsed) {
                contentMax = contentMin;
            }
            window.skipItems = window.collapsed;

            // beginWindow() pushed the clip rectangle from the geometry as it was
            // before these options arrived. Without rebuilding it, widgets that were
            // just re-laid-out against the new rectangle would still be clipped (and
            // hit tested) against the old one.
            const float bodyHeight = window.collapsed ? titleBarHeight : window.rect.height;
            const Rect bodyRect(window.rect.x, window.rect.y, window.rect.width, bodyHeight);
            if (foregroundList.clipDepth() == window.clipDepth) {
                foregroundList.popClipRect();
                foregroundList.pushClipRect(bodyRect);
                window.clipDepth = foregroundList.clipDepth();
            }

            // Chained options arrive before the body runs; restart the window's
            // region from the recomputed content rectangle.
            if (hasLayout()) {
                currentRegion().begin(contentMin, contentMax);
            }
        }
        /// Hands out a texture handle and asks the backend to create it.
        ///
        /// Nothing is created here: the request is queued and replayed by the
        /// renderer, which is the only layer allowed to touch the device. The
        /// handle is unique for the lifetime of the context and never reused, so a
        /// stale draw command can never end up sampling an unrelated texture.
        ///
        /// `rgba` is borrowed, so it has to stay valid until the next frame has
        /// been rendered.
        auto createTexture(std::uint32_t width, std::uint32_t height, const std::uint8_t *rgba) noexcept -> TextureId {
            if (width == 0 || height == 0) {
                return InvalidTexture;
            }

            ResourceCmd command;
            command.type = ResourceCmdType::CreateTexture;
            command.handle = nextTextureHandle++;
            command.desc.width = width;
            command.desc.height = height;
            command.pixels = rgba;
            if (!textureCommandQueue.push_back(command)) {
                return InvalidTexture;
            }
            return command.handle;
        }

        /// Queues the release of a texture previously handed out by createTexture.
        /// Destroying an unknown or already destroyed handle is a no-op.
        void destroyTexture(TextureId handle) noexcept {
            if (handle == InvalidTexture) {
                return;
            }
            ResourceCmd command;
            command.type = ResourceCmdType::DestroyTexture;
            command.handle = handle;
            textureCommandQueue.push_back(command);
        }

        /// Texture requests the backend has to replay before drawing.
        auto textureCommands() const noexcept -> std::span<const ResourceCmd> {
            return {textureCommandQueue.data(), textureCommandQueue.size()};
        }

        /// The texture holding the glyph atlas, or InvalidTexture while the atlas
        /// is unavailable.
        auto atlasTexture() const noexcept -> TextureId { return fontTexture; }

        auto fonts() const noexcept -> const FontAtlas & { return atlas; }

        auto defaultFont() const noexcept -> FontId { return atlas.defaultFont(); }

      private:
        /// Resolves the "InvalidFont means the default" convention and folds in
        /// the "atlas is not usable yet" case.
        auto resolveFont(FontId font) const noexcept -> FontId {
            if (!atlas.isBuilt()) {
                return InvalidFont;
            }
            const FontId effective = font == InvalidFont ? atlas.defaultFont() : font;
            return effective != InvalidFont && effective < atlas.fontCount() ? effective : InvalidFont;
        }

        /// char and char8_t are both byte sized, so UTF-8 text can be handed to
        /// the decoder without a copy however the caller spelled it.
        template <typename Char> static auto asUtf8(Z::BasicStringView<Char> text) noexcept -> Z::u8StringView {
            return Z::u8StringView(reinterpret_cast<const char8_t *>(text.data()), text.size());
        }

        /// Byte offset in `buffer` whose rendered prefix is closest to the click at
        /// `mouseX`, i.e. where a click should drop the caret.
        auto cursorOffsetFromX(std::span<const char> buffer, float textX, float mouseX) const noexcept -> std::size_t {
            const float target = mouseX > textX ? mouseX - textX : 0.0f;
            const std::size_t length = detail::textLength(buffer);
            const Z::u8StringView bytes(reinterpret_cast<const char8_t *>(buffer.data()), length);

            std::size_t best = 0;
            float bestDistance = target;
            std::size_t offset = 0;
            while (offset < length) {
                const std::size_t next = unicode::nextCodepoint(bytes, offset);
                const float width = calculateTextSize(Z::StringView(buffer.data(), next)).x;
                const float distance = width > target ? width - target : target - width;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = next;
                }
                offset = next;
            }
            return best;
        }

        /// Builds the glyph atlas once and uploads it as a texture.
        ///
        /// The atlas is rasterized at the style's font size, so the value of
        /// Style::fontSize on the first frame decides the glyph resolution;
        /// changing it later only moves layout around.
        void ensureFontAtlas() noexcept {
            if (!atlas.isBuilt()) {
                atlas.loadDefaultFont(nullptr, style.fontSize);
                atlas.build();
            }
            if (fontTexture != InvalidTexture || !atlas.isBuilt()) {
                return;
            }

            const ReZeroTrueType::Image *image = atlas.atlasImage(atlas.defaultFont());
            if (image == nullptr) {
                return;
            }
            fontTexture =
                createTexture(image->width, image->height, reinterpret_cast<const std::uint8_t *>(image->data()));
        }

      public:
        auto idSeed() const noexcept -> Id { return idStack.empty() ? 2166136261u : idStack.back(); }

      private:
        DrawList backgroundList;
        DrawList foregroundList;
        std::vector<Id> idStack;
        Style style;
        Id activeId = InvalidId;
        /// Keyboard owner: the text field being edited; see isTextFocused().
        Id focusedTextId = InvalidId;
        /// Frame the focused field was last declared in, so endFrame() can drop the
        /// focus of a field that is gone.
        std::uint32_t focusedTextFrame = 0;
        Id hoveredId = InvalidId;
        Id hoveredWindowId = InvalidId;

        Vector2 windowDragOffset;

        Vector2 displaySize;

        std::vector<std::size_t> windowStack;
        std::size_t currentWindowIndex = NoWindow;
        std::vector<WindowData> windows;

        /// Monotonic texture handle source. Handles are never reused, which is
        /// what lets the backend treat a create as idempotent without any
        /// generation counter.
        TextureId nextTextureHandle = 1;
        Z::ArrayList<ResourceCmd> textureCommandQueue;

        /// A frame longer than this counts as a stall, so animations step at most
        /// this far instead of jumping.
        static constexpr float MaxFrameDelta = 0.1f;

        std::uint32_t frameIndex = 0;
        /// Seconds since the previous frame; see deltaTime().
        float frameDelta = 0.0f;
        /// Animated values owned by widget ids; see animate().
        detail::AnimationStore animations;
        Input input;

        bool keyboardCapture = false;

        /// Caret of the field that owns the keyboard; see caretPosition().
        std::size_t caret = 0;

        /// Layout cursor of the innermost open region. Private on purpose: the slot
        /// behind it is recycled by popRegion(), so handing the reference out would
        /// let a stored one silently alias another scope's region. Widgets go through
        /// placeItem(), newLine() and contentRegionWidth() instead.
        Region &currentRegion() noexcept { return layoutStack[layoutDepth - 1]; }
        const Region &currentRegion() const noexcept { return layoutStack[layoutDepth - 1]; }

        /// Fixed size, so a frame never allocates and a Region reference stays valid
        /// across pushRegion().
        std::array<Region, MaxLayoutDepth> layoutStack{};
        std::size_t layoutDepth = 0;
        /// Pushes dropped because the stack was full; their pops are swallowed so a
        /// scope that nested too deep cannot unbalance the stack.
        std::size_t layoutOverflow = 0;

        DrawData drawData;
        FontAtlas atlas;
        TextureId fontTexture = InvalidTexture;
    };

} // namespace ReZeroGui