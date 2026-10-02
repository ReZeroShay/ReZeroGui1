#pragma once
#include <ReZeroGui/ReZeroGui.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

namespace ReZeroGui {

    //===----------------------------------------------------------------------===//
    // Labels
    //===----------------------------------------------------------------------===//

    /// Widget labels follow the "Label##id" convention: the text before "##" is
    /// what gets drawn, and everything after it identifies the widget. That keeps
    /// ids stable when the visible text changes (counters, localisation) and lets
    /// two widgets share a visible label.
    inline auto displayPartOf(std::string_view label) noexcept -> std::string_view {
        const std::size_t marker = label.find("##");
        return marker == std::string_view::npos ? label : label.substr(0, marker);
    }

    inline auto idPartOf(std::string_view label) noexcept -> std::string_view {
        const std::size_t marker = label.find("##");
        if (marker == std::string_view::npos) {
            return label;
        }
        const std::string_view id = label.substr(marker + 2);
        // A bare "Label##" falls back to the visible part rather than an empty id.
        return id.empty() ? label.substr(0, marker) : id;
    }

    class TextBuilder;
    class ButtonBuilder;
    class CheckboxBuilder;
    class RadioButtonBuilder;
    class SwitchToggleBuilder;
    class FloatSliderBuilder;
    class IntSliderBuilder;
    class TextInputBuilder;
    class ProgressBarBuilder;
    class SeparatorBuilder;
    class SeparatorTextBuilder;
    struct WindowScope;

    //===----------------------------------------------------------------------===//
    // Ui
    //===----------------------------------------------------------------------===//

    /// The widget facing facade.
    ///
    /// Context owns the state and the device facing behaviour; Ui is the thin
    /// layer the widgets are written against. It adds the two conveniences a
    /// widget needs but the core has no reason to know about: labels are narrow
    /// (`char`) strings, and widgets are created through the factories below so
    /// that a builder dies (and therefore draws) at the end of its statement.
    struct Ui {
        explicit Ui(Context &ctx) noexcept : ctx(ctx) {}

        Ui(const Ui &) noexcept = default;
        Ui &operator=(const Ui &) noexcept = default;

        Context &context() noexcept { return ctx; }
        const Context &context() const noexcept { return ctx; }

        // -- style -------------------------------------------------------------

        Style &currentStyle() noexcept { return ctx.currentStyle(); }
        const Style &currentStyle() const noexcept { return ctx.currentStyle(); }

        auto defaultFont() const noexcept -> FontId { return ctx.defaultFont(); }

        // -- text --------------------------------------------------------------

        /// Measures a label. Labels are char strings, so this takes the narrow
        /// view and lets the context deal with code points.
        auto calcTextSize(std::string_view text, FontId font = InvalidFont, float wrapWidth = 0.0f) const noexcept
            -> Vector2 {
            return ctx.calculateTextSize(Z::StringView(text.data(), text.size()), font, wrapWidth);
        }

        void renderText(Vector2 position, std::string_view text, Pixel color, FontId font, float wrapWidth) noexcept {
            ctx.renderText(position, Z::StringView(text.data(), text.size()), color, font, wrapWidth);
        }

        /// Content of a fixed size char buffer as a view, stopping at the NUL the
        /// text widgets maintain.
        auto bufferText(std::span<const char> buffer) const noexcept -> std::string_view {
            return std::string_view(buffer.data(), detail::textLength(buffer));
        }

        // -- layout and items --------------------------------------------------

        auto currentId(std::string_view seed) const noexcept -> Id { return ctx.currentId(seed); }

        auto placeItem(Vector2 size, float textBaseline = -1.0f) noexcept -> Rect {
            return ctx.placeItem(size, textBaseline);
        }

        auto isItemVisible(const Rect &rect) const noexcept -> bool { return ctx.isItemVisible(rect); }

        auto contentRegionWidth() const noexcept -> float { return ctx.contentRegionWidth(); }

        /// Ends the current line: the next item starts below this one.
        void newLine() noexcept { ctx.newLine(); }

        auto isItemActive(Id id) const noexcept -> bool { return ctx.isItemActive(id); }

        auto isTextFocused(Id id) const noexcept -> bool { return ctx.isTextFocused(id); }

        auto animate(Id id, float target, float tau) noexcept -> float { return ctx.animate(id, target, tau); }

        // -- interaction -------------------------------------------------------

        bool buttonBehavior(const Rect &rect, Id id, bool *outHovered, bool *outHeld, bool enabled = true) noexcept {
            return ctx.buttonBehavior(rect, id, outHovered, outHeld, enabled);
        }

        bool dragBehavior(const Rect &rect, Id id, float *value, float minimum, float maximum, float grabWidth,
                          bool enabled = true, bool *outHovered = nullptr) noexcept {
            return ctx.dragBehavior(rect, id, value, minimum, maximum, grabWidth, enabled, outHovered);
        }

        auto isMouseHoveringRect(const Rect &rect, Id id = 0) const noexcept -> bool {
            return ctx.isMouseHoveringRect(rect, id);
        }

        auto input() noexcept -> Input & { return ctx.io(); }

        auto caretPosition() const noexcept -> std::size_t { return ctx.caretPosition(); }

        void textInputBehavior(const Rect &rect, Id id, std::span<char> buffer, bool *outChanged) noexcept {
            ctx.textInputBehavior(rect, id, buffer, outChanged);
        }

        // -- drawing -----------------------------------------------------------

        DrawList &drawList() noexcept { return ctx.drawList(); }

        void renderFrame(const Rect &rect, Pixel fill, Pixel border, float rounding, bool hasBorder = true) noexcept {
            ctx.renderFrame(rect, fill, border, rounding, hasBorder);
        }

        void renderCheckMark(const Rect &rect, Pixel color) noexcept { ctx.renderCheckMark(rect, color); }

        // -- widgets -----------------------------------------------------------
        //
        // Each factory returns a builder by value. The builder draws in its
        // destructor, so `ui.button("X")` is drawn and hit tested at the end of
        // the statement it appears in, and `if (ui.button("X").clicked())` works
        // because clicked() emits early.
        //
        // The definitions live after the builders below.

        auto text(std::string_view content) noexcept -> TextBuilder;
        auto button(std::string_view label) noexcept -> ButtonBuilder;
        auto checkbox(std::string_view label) noexcept -> CheckboxBuilder;
        auto radioButton(std::string_view label) noexcept -> RadioButtonBuilder;
        auto switchToggle(std::string_view label) noexcept -> SwitchToggleBuilder;
        auto sliderFloat(std::string_view label) noexcept -> FloatSliderBuilder;
        auto sliderInt(std::string_view label) noexcept -> IntSliderBuilder;
        auto inputText(std::string_view label) noexcept -> TextInputBuilder;
        auto progressBar(std::string_view label = std::string_view()) noexcept -> ProgressBarBuilder;
        auto separator() noexcept -> SeparatorBuilder;
        auto separatorText(std::string_view label) noexcept -> SeparatorTextBuilder;

        Context &ctx;
    };

    //===----------------------------------------------------------------------===//
    // Text
    //===----------------------------------------------------------------------===//

    class TextBuilder {
      public:
        TextBuilder(Ui &ui, std::string_view content) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(content))), content_(displayPartOf(content)),
              color_(ui.currentStyle().color(ColorIndex::Text)), font_(ui.defaultFont()) {}

        ~TextBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        TextBuilder(const TextBuilder &) = delete;
        TextBuilder &operator=(const TextBuilder &) = delete;

        TextBuilder &color(Pixel value) noexcept {
            color_ = value;
            return *this;
        }

        TextBuilder &font(FontId value) noexcept {
            font_ = value;
            return *this;
        }

        TextBuilder &wrapWidth(float value) noexcept {
            wrapWidth_ = value;
            return *this;
        }

        TextBuilder &width(float value) noexcept {
            width_ = value;
            return *this;
        }

        TextBuilder &alpha(float value) noexcept {
            color_ = color_.withAlpha(value);
            return *this;
        }

        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Vector2 measured = ui_.calcTextSize(content_, font_, wrapWidth_);
            Vector2 size = measured;
            float width = wrapWidth_ > 0.0f ? wrapWidth_ : size.x;
            if (width_ > 0.0f) {
                width = width_;
            }
            size.x = width;

            const Rect rect = ui_.placeItem(size, size.y);
            if (ui_.isItemVisible(rect)) {
                ui_.renderText(rect.position(), content_, color_, font_, wrapWidth_);
            }
            rect_ = rect;
        }

      private:
        Ui &ui_;
        Id id_;
        std::string_view content_;
        Pixel color_;
        FontId font_;
        float wrapWidth_ = 0.0f;
        float width_ = 0.0f;
        bool emitted_ = false;
        Rect rect_;
    };

    //===----------------------------------------------------------------------===//
    // Button
    //===----------------------------------------------------------------------===//

    class ButtonBuilder {
      public:
        ButtonBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

        ~ButtonBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        ButtonBuilder(const ButtonBuilder &) = delete;
        ButtonBuilder &operator=(const ButtonBuilder &) = delete;

        ButtonBuilder &size(Vector2 value) noexcept {
            explicitSize_ = value;
            hasExplicitSize_ = true;
            return *this;
        }

        ButtonBuilder &size(float width, float height) noexcept { return size(Vector2(width, height)); }

        ButtonBuilder &width(float value) noexcept {
            explicitSize_.x = value;
            hasExplicitSize_ = true;
            return *this;
        }

        ButtonBuilder &height(float value) noexcept {
            explicitSize_.y = value;
            hasExplicitSize_ = true;
            return *this;
        }

        /// Stretches the button over the whole available width, with the label laid out
        /// inside it by align() - centred unless told otherwise. This is the usual shape
        /// for a stacked OK / Cancel pair. An explicit width() or size() still wins, and
        /// a button drawn where no layout region is open keeps its natural size.
        ButtonBuilder &fillWidth(bool enabled = true) noexcept {
            fillWidth_ = enabled;
            return *this;
        }

        /// Where the label sits inside the button's frame padding: {0, 0} against the
        /// top left inset, {0.5, 0.5} centred, {1, 1} against the bottom right inset.
        ButtonBuilder &align(Vector2 value) noexcept {
            align_ = value;
            return *this;
        }

        ButtonBuilder &disabled(bool value = true) noexcept {
            disabled_ = value;
            return *this;
        }

        ButtonBuilder &background(Pixel value) noexcept {
            background_ = value;
            hasBackground_ = true;
            return *this;
        }

        ButtonBuilder &textColor(Pixel value) noexcept {
            textColor_ = value;
            hasTextColor_ = true;
            return *this;
        }

        ButtonBuilder &rounding(float value) noexcept {
            rounding_ = value;
            return *this;
        }

        /// Registers a callback invoked on click. The callable only has to stay alive
        /// until the end of the full expression.
        template <class F>
            requires std::invocable<F &> || std::invocable<F &, Ui &>
        ButtonBuilder &onClick(F &&callback) noexcept {
            using Fn = std::remove_reference_t<F>;
            callbackTarget_ = static_cast<void *>(std::addressof(callback));
            if constexpr (std::invocable<Fn &, Ui &>) {
                callbackInvoke_ = [](void *target, Ui &ui) { (*static_cast<Fn *>(target))(ui); };
            } else {
                callbackInvoke_ = [](void *target, Ui &) { (*static_cast<Fn *>(target))(); };
            }
            return *this;
        }

        bool clicked() noexcept {
            if (!emitted_) {
                emit();
            }
            return clicked_;
        }

        bool hovered() noexcept {
            if (!emitted_) {
                emit();
            }
            return hovered_;
        }

        bool pressed() noexcept {
            if (!emitted_) {
                emit();
            }
            return held_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const Vector2 labelSize = ui_.calcTextSize(label_);
            Vector2 size = labelSize + style.framePadding * 2.0f;
            if (fillWidth_) {
                const float available = ui_.contentRegionWidth();
                if (available > 0.0f) {
                    size.x = available;
                }
            }
            if (hasExplicitSize_) {
                if (explicitSize_.x > 0.0f) {
                    size.x = explicitSize_.x;
                }
                if (explicitSize_.y > 0.0f) {
                    size.y = explicitSize_.y;
                }
            }

            const Rect rect = rect_ = ui_.placeItem(size);

            bool hovered = false;
            bool held = false;
            const bool pressed = ui_.buttonBehavior(rect, id_, &hovered, &held, !disabled_);

            if (ui_.isItemVisible(rect)) {
                Pixel fill = hasBackground_ ? background_ : style.color(ColorIndex::Button);
                if (disabled_) {
                    fill = fill * 0.55f;
                } else if (held) {
                    fill = style.color(ColorIndex::ButtonActive);
                } else if (hovered) {
                    fill = style.color(ColorIndex::ButtonHovered);
                }
                ui_.renderFrame(rect, fill, style.color(ColorIndex::Border),
                                rounding_ < 0.0f ? style.frameRounding : rounding_, style.frameBorderSize > 0.0f);

                if (!label_.empty()) {
                    const Pixel text = hasTextColor_ ? textColor_
                                                     : (disabled_ ? style.color(ColorIndex::TextDisabled)
                                                                  : style.color(ColorIndex::Text));
                    // Alignment happens inside the frame padding, so a left aligned label
                    // keeps its inset instead of touching the border. Centring is
                    // unaffected: padding + (size - 2 * padding - label) / 2 is the same
                    // as (size - label) / 2.
                    const Vector2 labelArea = rect.size() - style.framePadding * 2.0f;
                    const Vector2 textPosition =
                        rect.position() + style.framePadding + (labelArea - labelSize) * Vector2(align_.x, align_.y);
                    ui_.renderText(textPosition, label_, text, ui_.defaultFont(), 0.0f);
                }
            }

            hovered_ = hovered;
            held_ = held;
            clicked_ = pressed && !disabled_;

            if (clicked_ && callbackInvoke_ != nullptr) {
                callbackInvoke_(callbackTarget_, ui_);
            }
        }

        /// Screen space rectangle the button was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

        /// The widget id, as the context hashes it from the label.
        Id id() const noexcept { return id_; }

      private:
        Ui &ui_;
        Id id_;
        Rect rect_;
        std::string_view label_;
        Vector2 explicitSize_;
        Vector2 align_{0.5f, 0.5f};
        bool fillWidth_ = false;
        Pixel background_;
        Pixel textColor_;
        float rounding_ = -1.0f;
        bool hasExplicitSize_ = false;
        bool hasBackground_ = false;
        bool hasTextColor_ = false;
        bool disabled_ = false;
        bool emitted_ = false;
        bool hovered_ = false;
        bool held_ = false;
        bool clicked_ = false;
        void *callbackTarget_ = nullptr;
        void (*callbackInvoke_)(void *, Ui &) = nullptr;
    };

    namespace detail {
        /// Which edge of the row the indicator owns.
        enum class ToggleSide : std::uint8_t {
            /// Indicator first, label straight after it: the checkbox and radio look.
            IndicatorFirst,
            /// Label at the left edge, indicator pushed to the right edge.
            LabelFirst,
        };

        /// What the checkbox, the radio button and the switch share: one line holding
        /// an indicator plus an optional label, with the click already resolved and
        /// the placement worked out. The widgets keep their own state and draw their
        /// own indicator, so this is plain shared code rather than a base class.
        struct ToggleLine {
            Rect rect;
            Vector2 labelSize;
            Vector2 indicatorSize;
            /// Top left corner the label is drawn at.
            Vector2 labelPosition;
            /// Top left corner the indicator is drawn at.
            Vector2 indicatorPosition;
            bool hovered = false;
            bool held = false;
            bool clicked = false;
        };

        /// Sizes and places the line, runs the click behaviour and reports the hit
        /// state. `spacing` is the gap between the label and the indicator (negative
        /// keeps the style default) and `maxWidth` caps a stretched row, which is
        /// also as far as the indicator can be pushed away from the label. The whole
        /// row stays clickable either way.
        inline auto placeToggleLine(Ui &ui, Id id, std::string_view label, Vector2 indicatorSize, bool disabled,
                                    float spacing = -1.0f, float maxWidth = 0.0f,
                                    ToggleSide side = ToggleSide::IndicatorFirst) noexcept -> ToggleLine {
            const Style &style = ui.currentStyle();
            const float gap = spacing >= 0.0f ? spacing : style.itemInnerSpacing.x;
            const bool hasLabel = !label.empty();

            ToggleLine line;
            line.indicatorSize = indicatorSize;
            line.labelSize = hasLabel ? ui.calcTextSize(label) : Vector2();

            const float contentWidth = indicatorSize.x + (hasLabel ? gap + line.labelSize.x : 0.0f);
            float rowWidth = contentWidth;
            if (side == ToggleSide::LabelFirst) {
                const float available = ui.contentRegionWidth();
                rowWidth = available > 0.0f ? available : contentWidth;
                if (maxWidth > 0.0f && rowWidth > maxWidth) {
                    rowWidth = maxWidth;
                }
                // Stretching only ever adds room: the contents stay reachable.
                if (rowWidth < contentWidth) {
                    rowWidth = contentWidth;
                }
            }

            const float contentHeight = std::max(indicatorSize.y, hasLabel ? line.labelSize.y : 0.0f);
            line.rect = ui.placeItem(Vector2(rowWidth, contentHeight + style.framePadding.y));

            const float contentY = line.rect.y + style.framePadding.y * 0.5f;
            const bool labelFirst = side == ToggleSide::LabelFirst;
            line.labelPosition = Vector2(labelFirst ? line.rect.x : line.rect.x + indicatorSize.x + gap,
                                         contentY + (contentHeight - line.labelSize.y) * 0.5f);
            line.indicatorPosition = Vector2(labelFirst ? line.rect.maxX() - indicatorSize.x : line.rect.x,
                                             contentY + (contentHeight - indicatorSize.y) * 0.5f);

            line.clicked = ui.buttonBehavior(line.rect, id, &line.hovered, &line.held, !disabled);
            return line;
        }

        /// Draws the label where the line placed it.
        inline void drawToggleLabel(Ui &ui, const ToggleLine &line, std::string_view label, bool disabled) noexcept {
            if (label.empty()) {
                return;
            }
            const Style &style = ui.currentStyle();
            ui.renderText(line.labelPosition, label,
                          disabled ? style.color(ColorIndex::TextDisabled) : style.color(ColorIndex::Text),
                          ui.defaultFont(), 0.0f);
        }
    } // namespace detail

    //===----------------------------------------------------------------------===//
    // Checkbox
    //===----------------------------------------------------------------------===//

    class CheckboxBuilder {
      public:
        CheckboxBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

        ~CheckboxBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        CheckboxBuilder(const CheckboxBuilder &) = delete;
        CheckboxBuilder &operator=(const CheckboxBuilder &) = delete;

        /// Two way binding: the flag is toggled on click.
        CheckboxBuilder &checked(bool *value) noexcept {
            value_ = value;
            return *this;
        }

        /// Read only binding (the value is copied into the builder).
        CheckboxBuilder &checked(bool value) noexcept {
            readOnlyValue_ = value;
            value_ = &readOnlyValue_;
            return *this;
        }

        CheckboxBuilder &disabled(bool value = true) noexcept {
            disabled_ = value;
            return *this;
        }

        bool changed() noexcept {
            if (!emitted_) {
                emit();
            }
            return changed_;
        }

        bool value() noexcept {
            if (!emitted_) {
                emit();
            }
            return value_ != nullptr && *value_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const float boxSize = style.fontSize * 0.95f;
            const detail::ToggleLine line = detail::placeToggleLine(ui_, id_, label_, Vector2(boxSize, boxSize), disabled_);
            rect_ = line.rect;

            if (line.clicked && value_ != nullptr && !disabled_) {
                *value_ = !*value_;
                changed_ = true;
            }

            if (ui_.isItemVisible(line.rect)) {
                const Rect box(line.indicatorPosition.x, line.indicatorPosition.y, boxSize, boxSize);
                const Pixel fill = line.held ? style.color(ColorIndex::FrameBackgroundActive)
                                             : (line.hovered ? style.color(ColorIndex::FrameBackgroundHovered)
                                                             : style.color(ColorIndex::FrameBackground));
                ui_.renderFrame(box, fill, style.color(ColorIndex::Border), style.frameRounding,
                                style.frameBorderSize > 0.0f);
                if (value_ != nullptr && *value_) {
                    ui_.renderCheckMark(box, disabled_ ? style.color(ColorIndex::TextDisabled)
                                                       : style.color(ColorIndex::CheckMark));
                }
                detail::drawToggleLabel(ui_, line, label_, disabled_);
            }
        }

        /// Screen space rectangle the checkbox was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

      private:
        Ui &ui_;
        Id id_;
        Rect rect_;
        std::string_view label_;
        bool *value_ = nullptr;
        bool readOnlyValue_ = false;
        bool disabled_ = false;
        bool emitted_ = false;
        bool changed_ = false;
    };

    //===----------------------------------------------------------------------===//
    // Radio button
    //===----------------------------------------------------------------------===//

    class RadioButtonBuilder {
      public:
        RadioButtonBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

        ~RadioButtonBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        RadioButtonBuilder(const RadioButtonBuilder &) = delete;
        RadioButtonBuilder &operator=(const RadioButtonBuilder &) = delete;

        /// Two way binding to the variable holding the selected value.
        RadioButtonBuilder &selected(int *current) noexcept {
            current_ = current;
            return *this;
        }

        /// Value this button writes into the bound variable when clicked.
        RadioButtonBuilder &optionValue(int value) noexcept {
            optionValue_ = value;
            return *this;
        }

        RadioButtonBuilder &disabled(bool value = true) noexcept {
            disabled_ = value;
            return *this;
        }

        bool changed() noexcept {
            if (!emitted_) {
                emit();
            }
            return changed_;
        }

        bool selected() noexcept {
            if (!emitted_) {
                emit();
            }
            return current_ != nullptr && *current_ == optionValue_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const float circleSize = style.fontSize * 0.9f;
            const detail::ToggleLine line = detail::placeToggleLine(ui_, id_, label_, Vector2(circleSize, circleSize), disabled_);
            rect_ = line.rect;

            if (line.clicked && current_ != nullptr && !disabled_ && *current_ != optionValue_) {
                *current_ = optionValue_;
                changed_ = true;
            }

            if (ui_.isItemVisible(line.rect)) {
                const Vector2 center(line.indicatorPosition.x + circleSize * 0.5f,
                                     line.indicatorPosition.y + circleSize * 0.5f);
                ui_.drawList().addCircleFilled(center, circleSize * 0.5f,
                                               line.held ? style.color(ColorIndex::FrameBackgroundActive)
                                                         : (line.hovered
                                                                ? style.color(ColorIndex::FrameBackgroundHovered)
                                                                : style.color(ColorIndex::FrameBackground)));
                ui_.drawList().addCircle(center, circleSize * 0.5f, style.color(ColorIndex::Border), 0, 1.0f);
                if (current_ != nullptr && *current_ == optionValue_) {
                    ui_.drawList().addCircleFilled(center, circleSize * 0.28f, style.color(ColorIndex::CheckMark));
                }
                detail::drawToggleLabel(ui_, line, label_, disabled_);
            }
        }

        /// Screen space rectangle the option was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

      private:
        Ui &ui_;
        Id id_;
        Rect rect_;
        std::string_view label_;
        int *current_ = nullptr;
        int optionValue_ = 0;
        bool disabled_ = false;
        bool emitted_ = false;
        bool changed_ = false;
    };

    //===----------------------------------------------------------------------===//
    // Switch toggle
    //===----------------------------------------------------------------------===//

    /// A checkbox drawn as a sliding switch. The knob position is a context owned
    /// animated value (see Context::animate()), so the widget itself carries no
    /// cross frame state.
    class SwitchToggleBuilder {
      public:
        SwitchToggleBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

        ~SwitchToggleBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        SwitchToggleBuilder(const SwitchToggleBuilder &) = delete;
        SwitchToggleBuilder &operator=(const SwitchToggleBuilder &) = delete;

        /// Two way binding: the flag is toggled on click.
        SwitchToggleBuilder &checked(bool *value) noexcept {
            value_ = value;
            return *this;
        }

        /// Read only binding (the value is copied into the builder).
        SwitchToggleBuilder &checked(bool value) noexcept {
            readOnlyValue_ = value;
            value_ = &readOnlyValue_;
            return *this;
        }

        SwitchToggleBuilder &disabled(bool value = true) noexcept {
            disabled_ = value;
            return *this;
        }

        /// Time constant of the slide, in seconds. Smaller is snappier, zero snaps
        /// outright.
        SwitchToggleBuilder &smoothing(float seconds) noexcept {
            smoothing_ = seconds;
            return *this;
        }

        /// Gap between the label and the switch, in pixels. Negative keeps the style
        /// default (Style::itemInnerSpacing.x).
        SwitchToggleBuilder &spacing(float pixels) noexcept {
            spacing_ = pixels;
            return *this;
        }

        /// Caps how wide the row may get, which is also the furthest the switch can be
        /// pushed away from the label. Zero uses the whole available width, and it
        /// only matters with labelFirst(), where the row stretches.
        SwitchToggleBuilder &maxWidth(float pixels) noexcept {
            maxWidth_ = pixels;
            return *this;
        }

        /// Puts the label at the left edge and the switch at the right edge of the
        /// row, stretching to the available width up to maxWidth(). The whole row is
        /// clickable, not just the switch.
        SwitchToggleBuilder &labelFirst(bool enabled = true) noexcept {
            labelFirst_ = enabled;
            return *this;
        }

        bool changed() noexcept {
            if (!emitted_) {
                emit();
            }
            return changed_;
        }

        bool value() noexcept {
            if (!emitted_) {
                emit();
            }
            return value_ != nullptr && *value_;
        }

        /// Knob progress: zero while off, one while on, in between mid slide.
        float slide() noexcept {
            if (!emitted_) {
                emit();
            }
            return slide_;
        }

        /// Screen space rectangle the switch was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const float trackWidth = style.fontSize * 1.8f;
            const float trackHeight = style.fontSize * 0.95f;

            const detail::ToggleLine line =
                detail::placeToggleLine(ui_, id_, label_, Vector2(trackWidth, trackHeight), disabled_, spacing_,
                                        maxWidth_,
                                        labelFirst_ ? detail::ToggleSide::LabelFirst
                                                    : detail::ToggleSide::IndicatorFirst);
            rect_ = line.rect;

            if (line.clicked && value_ != nullptr && !disabled_) {
                *value_ = !*value_;
                changed_ = true;
            }

            // The animation advances whether or not the switch ends up being drawn:
            // an item that is clipped away must still be settled when it comes back.
            const float target = (value_ != nullptr && *value_) ? 1.0f : 0.0f;
            slide_ = ui_.animate(id_, target, smoothing_);

            if (!ui_.isItemVisible(line.rect)) {
                return;
            }

            const Rect track(line.indicatorPosition.x, line.indicatorPosition.y, trackWidth, trackHeight);
            const float radius = trackHeight * 0.5f;
            const Pixel off = line.hovered ? style.color(ColorIndex::FrameBackgroundHovered)
                                           : style.color(ColorIndex::FrameBackground);
            const Pixel on = line.held ? style.color(ColorIndex::ButtonActive) : style.color(ColorIndex::Button);
            Pixel fill = off.mix(on, slide_);
            if (disabled_) {
                fill = fill * 0.55f;
            }

            ui_.drawList().addRectFilled(track.position(), track.maxPoint(), fill, radius, DrawCorner::All);
            if (style.frameBorderSize > 0.0f) {
                ui_.drawList().addRect(track.position(), track.maxPoint(), style.color(ColorIndex::Border), radius,
                                       DrawCorner::All, style.frameBorderSize);
            }

            const float knobRadius = std::max(1.0f, radius - style.frameBorderSize - 1.0f);
            const float knobX = track.x + radius + (trackWidth - trackHeight) * slide_;
            const Pixel knob = style.color(ColorIndex::Text);
            ui_.drawList().addCircleFilled(Vector2(knobX, track.center().y), knobRadius,
                                           disabled_ ? knob * 0.55f : knob);
            detail::drawToggleLabel(ui_, line, label_, disabled_);
        }

      private:
        Ui &ui_;
        Id id_;
        Rect rect_;
        std::string_view label_;
        bool *value_ = nullptr;
        bool readOnlyValue_ = false;
        float smoothing_ = 0.08f;
        float spacing_ = -1.0f;
        float maxWidth_ = 0.0f;
        float slide_ = 0.0f;
        bool labelFirst_ = false;
        bool disabled_ = false;
        bool emitted_ = false;
        bool changed_ = false;
    };
    //===----------------------------------------------------------------------===//
    // Sliders
    //===----------------------------------------------------------------------===//

    /// Shape of a slider's grab handle.
    enum class SliderGrabShape : std::uint8_t {
        /// Round handle; the default.
        Circle,
        /// Rectangle spanning the track's height.
        Rectangle,
    };

    namespace detail {
        /// Shared implementation of the float and the int slider. The two widgets
        /// differ in exactly three places, all of them below: how the dragged float
        /// turns back into T, what snprintf takes for T, and the default format.
        template <typename T> class SliderCore {
          public:
            SliderCore(Ui &ui, std::string_view label) noexcept
                : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

            ~SliderCore() noexcept {
                if (!emitted_) {
                    emit();
                }
            }

            SliderCore(const SliderCore &) = delete;
            SliderCore &operator=(const SliderCore &) = delete;

            void bind(T *target) noexcept { value_ = target; }

            void range(T minimum, T maximum) noexcept {
                minimum_ = minimum;
                maximum_ = maximum;
            }

            void format(const char *format) noexcept { format_ = format; }
            void width(float value) noexcept { explicitWidth_ = value; }
            void thin(float thickness = 4.0f) noexcept { thinTrack_ = thickness > 0.0f ? thickness : 4.0f; }
            void grabShape(SliderGrabShape value) noexcept { grabShape_ = value; }
            void valueOnLabel(bool enabled = true) noexcept { valueOnLabel_ = enabled; }
            void disabled(bool value = true) noexcept { disabled_ = value; }

            bool changed() noexcept {
                if (!emitted_) {
                    emit();
                }
                return changed_;
            }

            T value() noexcept {
                if (!emitted_) {
                    emit();
                }
                return value_ != nullptr ? *value_ : T{};
            }

            Rect rect() noexcept {
                if (!emitted_) {
                    emit();
                }
                return rect_;
            }

            void emit() noexcept {
                if (emitted_) {
                    return;
                }
                emitted_ = true;

                const Style &style = ui_.currentStyle();
                const Vector2 labelSize = label_.empty() ? Vector2() : ui_.calcTextSize(label_);
                Rect labelRow;
                if (!label_.empty()) {
                    // valueOnLabel() stretches the row over the whole line so the value
                    // can sit against its right edge; otherwise it only takes the label.
                    labelRow = ui_.placeItem(
                        valueOnLabel_ ? Vector2(ui_.contentRegionWidth(), labelSize.y) : labelSize, labelSize.y);
                    ui_.newLine();
                }

                const float rowHeight = style.fontSize + style.framePadding.y * 2.0f;
                const float width = explicitWidth_ > 0.0f ? explicitWidth_ : ui_.contentRegionWidth();
                rect_ = ui_.placeItem(Vector2(width, rowHeight));

                // The bar a thin slider draws, centred in the row it occupies. The row
                // keeps its usual height, so the value text and a comfortable grab still
                // fit and the drag target stays easy to hit.
                const float trackHeight = thinTrack_ > 0.0f ? thinTrack_ : rowHeight;
                const Rect track(rect_.x, rect_.y + (rowHeight - trackHeight) * 0.5f, rect_.width, trackHeight);

                float current = value_ != nullptr ? toFloat(*value_) : toFloat(minimum_);
                const float grabWidth = std::max(style.grabMinimumSize, rowHeight * 0.55f);

                bool hovered = false;
                const bool dragging = ui_.dragBehavior(rect_, id_, &current, toFloat(minimum_), toFloat(maximum_),
                                                       grabWidth, !disabled_ && value_ != nullptr, &hovered);

                T bound = fromFloat(current);
                if (maximum_ <= minimum_) {
                    bound = minimum_;
                } else if (bound < minimum_) {
                    bound = minimum_;
                } else if (bound > maximum_) {
                    bound = maximum_;
                }
                if (value_ != nullptr) {
                    *value_ = bound;
                }
                changed_ = dragging;

                char text[32];
                std::snprintf(text, sizeof(text), format_ != nullptr ? format_ : defaultFormat(), formatArg(bound));
                const std::string_view valueText(text);
                const Vector2 valueSize = ui_.calcTextSize(valueText);
                const bool valueOnRow = valueOnLabel_ && !label_.empty();

                // The label row is drawn once the value is known, so a drag updates the
                // number in the same frame that moves the grab.
                if (!label_.empty() && ui_.isItemVisible(labelRow)) {
                    ui_.renderText(labelRow.position(), label_, style.color(ColorIndex::Text), ui_.defaultFont(), 0.0f);
                    if (valueOnRow) {
                        ui_.renderText(Vector2(labelRow.maxX() - valueSize.x,
                                               labelRow.y + (labelRow.height - valueSize.y) * 0.5f),
                                       valueText, style.color(ColorIndex::Text), ui_.defaultFont(), 0.0f);
                    }
                }

                if (ui_.isItemVisible(rect_)) {
                    ui_.renderFrame(track, style.color(ColorIndex::FrameBackground), style.color(ColorIndex::Border),
                                    style.frameRounding, style.frameBorderSize > 0.0f);

                    const float span = toFloat(maximum_) - toFloat(minimum_);
                    const float normalized =
                        span > 0.0f ? Z::clampFloat((toFloat(bound) - toFloat(minimum_)) / span, 0.0f, 1.0f) : 0.0f;
                    const float fillWidth = normalized * track.width;
                    if (fillWidth > 0.5f) {
                        ui_.drawList().addRectFilled(track.position(), Vector2(track.x + fillWidth, track.maxY()),
                                                     style.color(ColorIndex::ProgressBar), style.frameRounding);
                    }

                    const float grabX = track.x + normalized * (track.width - grabWidth) + grabWidth * 0.5f;
                    const Pixel grabColor = (dragging || hovered) ? style.color(ColorIndex::SliderGrabActive)
                                                                  : style.color(ColorIndex::SliderGrab);
                    if (grabShape_ == SliderGrabShape::Rectangle) {
                        const float inset = style.frameBorderSize;
                        const Rect grab(grabX - grabWidth * 0.5f, track.y + inset, grabWidth,
                                        std::max(1.0f, track.height - inset * 2.0f));
                        ui_.drawList().addRectFilled(grab.position(), grab.maxPoint(), grabColor, style.frameRounding);
                    } else {
                        ui_.drawList().addCircleFilled(Vector2(grabX, track.center().y), grabWidth * 0.5f, grabColor);
                    }

                    if (!valueOnRow) {
                        ui_.renderText(rect_.center() - valueSize * 0.5f, valueText, style.color(ColorIndex::Text),
                                       ui_.defaultFont(), 0.0f);
                    }
                }
            }

          private:
            static float toFloat(T value) noexcept { return static_cast<float>(value); }

            static T fromFloat(float value) noexcept {
                if constexpr (std::is_same_v<T, int>) {
                    return static_cast<T>(std::round(value));
                } else {
                    return static_cast<T>(value);
                }
            }

            /// What snprintf expects for T: %d takes an int, %f a double.
            static auto formatArg(T value) noexcept {
                if constexpr (std::is_same_v<T, int>) {
                    return value;
                } else {
                    return static_cast<double>(value);
                }
            }

            static constexpr T defaultMaximum() noexcept {
                if constexpr (std::is_same_v<T, int>) {
                    return T{100};
                } else {
                    return T{1};
                }
            }

            static constexpr const char *defaultFormat() noexcept { return std::is_same_v<T, int> ? "%d" : "%.3f"; }

            Ui &ui_;
            Id id_;
            Rect rect_;
            std::string_view label_;
            T *value_ = nullptr;
            T minimum_ = T{};
            T maximum_ = defaultMaximum();
            float explicitWidth_ = 0.0f;
            float thinTrack_ = 0.0f;
            SliderGrabShape grabShape_ = SliderGrabShape::Circle;
            bool valueOnLabel_ = false;
            const char *format_ = nullptr;
            bool disabled_ = false;
            bool emitted_ = false;
            bool changed_ = false;
        };
    } // namespace detail

    class FloatSliderBuilder {
      public:
        FloatSliderBuilder(Ui &ui, std::string_view label) noexcept : core_(ui, label) {}

        FloatSliderBuilder &value(float *target) noexcept {
            core_.bind(target);
            return *this;
        }

        FloatSliderBuilder &range(float minimum, float maximum) noexcept {
            core_.range(minimum, maximum);
            return *this;
        }

        FloatSliderBuilder &format(const char *format) noexcept {
            core_.format(format);
            return *this;
        }

        FloatSliderBuilder &width(float value) noexcept {
            core_.width(value);
            return *this;
        }

        /// Thin track style: the bar is 	hickness pixels tall (4 by default) while the
        /// row keeps its usual height, so the value text and the grab still fit and the
        /// drag target stays easy to hit. Pairs well with valueOnLabel(), which moves
        /// the number off the thin bar onto the label's line.
        FloatSliderBuilder &thin(float thickness = 4.0f) noexcept {
            core_.thin(thickness);
            return *this;
        }

        /// Round grab (the default) or a rectangle spanning the track's height.
        FloatSliderBuilder &grabShape(SliderGrabShape value) noexcept {
            core_.grabShape(value);
            return *this;
        }

        /// Draws the value against the right edge of the label's line instead of
        /// centred inside the track. Needs a label to share the line with.
        FloatSliderBuilder &valueOnLabel(bool enabled = true) noexcept {
            core_.valueOnLabel(enabled);
            return *this;
        }

        FloatSliderBuilder &disabled(bool value = true) noexcept {
            core_.disabled(value);
            return *this;
        }

        void emit() noexcept { core_.emit(); }
        bool changed() noexcept { return core_.changed(); }
        float value() noexcept { return core_.value(); }
        Rect rect() noexcept { return core_.rect(); }

      private:
        detail::SliderCore<float> core_;
    };

    class IntSliderBuilder {
      public:
        IntSliderBuilder(Ui &ui, std::string_view label) noexcept : core_(ui, label) {}

        IntSliderBuilder &value(int *target) noexcept {
            core_.bind(target);
            return *this;
        }

        IntSliderBuilder &range(int minimum, int maximum) noexcept {
            core_.range(minimum, maximum);
            return *this;
        }

        IntSliderBuilder &format(const char *format) noexcept {
            core_.format(format);
            return *this;
        }

        IntSliderBuilder &width(float value) noexcept {
            core_.width(value);
            return *this;
        }

        /// Thin track style: the bar is 	hickness pixels tall (4 by default) while the
        /// row keeps its usual height, so the value text and the grab still fit and the
        /// drag target stays easy to hit. Pairs well with valueOnLabel(), which moves
        /// the number off the thin bar onto the label's line.
        IntSliderBuilder &thin(float thickness = 4.0f) noexcept {
            core_.thin(thickness);
            return *this;
        }

        /// Round grab (the default) or a rectangle spanning the track's height.
        IntSliderBuilder &grabShape(SliderGrabShape value) noexcept {
            core_.grabShape(value);
            return *this;
        }

        /// Draws the value against the right edge of the label's line instead of
        /// centred inside the track. Needs a label to share the line with.
        IntSliderBuilder &valueOnLabel(bool enabled = true) noexcept {
            core_.valueOnLabel(enabled);
            return *this;
        }

        IntSliderBuilder &disabled(bool value = true) noexcept {
            core_.disabled(value);
            return *this;
        }

        void emit() noexcept { core_.emit(); }
        bool changed() noexcept { return core_.changed(); }
        int value() noexcept { return core_.value(); }
        Rect rect() noexcept { return core_.rect(); }

      private:
        detail::SliderCore<int> core_;
    };

    //===----------------------------------------------------------------------===//
    // Text input
    //===----------------------------------------------------------------------===//

    class TextInputBuilder {
      public:
        TextInputBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

        ~TextInputBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        TextInputBuilder(const TextInputBuilder &) = delete;
        TextInputBuilder &operator=(const TextInputBuilder &) = delete;

        /// The caller's storage. It has to stay NUL terminated: the widget never
        /// writes past the last byte of the span.
        TextInputBuilder &buffer(std::span<char> buffer) noexcept {
            buffer_ = buffer;
            return *this;
        }

        template <std::size_t N> TextInputBuilder &buffer(char (&array)[N]) noexcept {
            buffer_ = std::span<char>(array, N);
            return *this;
        }

        TextInputBuilder &hint(std::string_view text) noexcept {
            hint_ = text;
            return *this;
        }

        TextInputBuilder &width(float value) noexcept {
            explicitWidth_ = value;
            return *this;
        }

        TextInputBuilder &disabled(bool value = true) noexcept {
            disabled_ = value;
            return *this;
        }

        bool changed() noexcept {
            if (!emitted_) {
                emit();
            }
            return changed_;
        }

        std::string_view text() noexcept {
            if (!emitted_) {
                emit();
            }
            return ui_.bufferText(buffer_);
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            if (!label_.empty()) {
                const Vector2 labelSize = ui_.calcTextSize(label_);
                const Rect labelRect = ui_.placeItem(labelSize, labelSize.y);
                if (ui_.isItemVisible(labelRect)) {
                    ui_.renderText(labelRect.position(), label_, style.color(ColorIndex::Text), ui_.defaultFont(),
                                   0.0f);
                }
                ui_.newLine();
            }

            const float height = style.fontSize + style.framePadding.y * 2.0f;
            const float width =
                explicitWidth_ > 0.0f ? explicitWidth_ : ui_.contentRegionWidth();
            const Rect rect = rect_ = ui_.placeItem(Vector2(width, height));

            bool changed = false;
            if (!disabled_ && !buffer_.empty()) {
                ui_.textInputBehavior(rect, id_, buffer_, &changed);
            }
            changed_ = changed;

            const bool focused = focused_ = ui_.isTextFocused(id_);
            if (ui_.isItemVisible(rect)) {
                const Pixel fill = focused ? style.color(ColorIndex::FrameBackgroundActive)
                                           : style.color(ColorIndex::FrameBackground);
                ui_.renderFrame(rect, fill, style.color(ColorIndex::Border), style.frameRounding,
                                style.frameBorderSize > 0.0f);

                const std::string_view content = ui_.bufferText(buffer_);
                const Vector2 textPosition =
                    rect.position() + Vector2(style.framePadding.x, (rect.height - style.fontSize) * 0.5f);
                if (content.empty()) {
                    if (!hint_.empty()) {
                        ui_.renderText(textPosition, hint_, style.color(ColorIndex::TextDisabled), ui_.defaultFont(),
                                       0.0f);
                    }
                } else {
                    ui_.renderText(textPosition, content, style.color(ColorIndex::Text), ui_.defaultFont(), 0.0f);
                }

                if (focused) {
                    const std::size_t caret = ui_.caretPosition();
                    const std::size_t caretOffset = caret < content.size() ? caret : content.size();
                    const std::string_view caretText(content.data(), caretOffset);
                    const float caretX = textPosition.x + ui_.calcTextSize(caretText).x + 1.0f;
                    ui_.drawList().addLine(Vector2(caretX, rect.y + style.framePadding.y * 0.5f),
                                           Vector2(caretX, rect.maxY() - style.framePadding.y * 0.5f),
                                           style.color(ColorIndex::Text), 1.0f);
                }
            }
        }

        /// Screen space rectangle the field was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

        /// The widget id, as the context hashes it from the label.
        Id id() const noexcept { return id_; }

        /// True while the field owns the keyboard.
        bool focused() noexcept {
            if (!emitted_) {
                emit();
            }
            return focused_;
        }

      private:
        Ui &ui_;
        Id id_;
        Rect rect_;
        std::string_view label_;
        std::string_view hint_;
        std::span<char> buffer_;
        bool focused_ = false;
        float explicitWidth_ = 0.0f;
        bool disabled_ = false;
        bool emitted_ = false;
        bool changed_ = false;
    };

    //===----------------------------------------------------------------------===//
    // Progress bar
    //===----------------------------------------------------------------------===//

    class ProgressBarBuilder {
      public:
        ProgressBarBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), id_(ui.currentId(idPartOf(label))), label_(displayPartOf(label)) {}

        ~ProgressBarBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        ProgressBarBuilder(const ProgressBarBuilder &) = delete;
        ProgressBarBuilder &operator=(const ProgressBarBuilder &) = delete;

        ProgressBarBuilder &fraction(float value) noexcept {
            fraction_ = Z::clampFloat(value, 0.0f, 1.0f);
            return *this;
        }

        ProgressBarBuilder &size(Vector2 value) noexcept {
            explicitSize_ = value;
            return *this;
        }

        ProgressBarBuilder &size(float width, float height) noexcept { return size(Vector2(width, height)); }

        ProgressBarBuilder &overlay(std::string_view text) noexcept {
            overlay_ = text;
            return *this;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const float height = explicitSize_.y > 0.0f ? explicitSize_.y : style.fontSize + style.framePadding.y;
            const float width =
                explicitSize_.x > 0.0f ? explicitSize_.x : ui_.contentRegionWidth();
            const Rect rect = rect_ = ui_.placeItem(Vector2(width, height));

            if (ui_.isItemVisible(rect)) {
                ui_.renderFrame(rect, style.color(ColorIndex::FrameBackground), style.color(ColorIndex::Border),
                                style.frameRounding, style.frameBorderSize > 0.0f);
                const float fillWidth = fraction_ * rect.width;
                if (fillWidth > 0.5f) {
                    ui_.drawList().addRectFilled(rect.position(), Vector2(rect.x + fillWidth, rect.maxY()),
                                                 style.color(ColorIndex::ProgressBar), style.frameRounding);
                }
                if (!overlay_.empty()) {
                    const Vector2 textSize = ui_.calcTextSize(overlay_);
                    ui_.renderText(rect.center() - textSize * 0.5f, overlay_, style.color(ColorIndex::Text),
                                   ui_.defaultFont(), 0.0f);
                }
            }
        }

        /// Screen space rectangle the bar was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

      private:
        Ui &ui_;
        Id id_;
        Rect rect_;
        std::string_view label_;
        std::string_view overlay_;
        Vector2 explicitSize_;
        float fraction_ = 0.0f;
        bool emitted_ = false;
    };

    //===----------------------------------------------------------------------===//
    // Separator
    //===----------------------------------------------------------------------===//

    /// A horizontal rule across the content region. It takes a row of its own, so the
    /// usual item spacing above and below is what separates blocks of widgets.
    class SeparatorBuilder {
      public:
        explicit SeparatorBuilder(Ui &ui) noexcept : ui_(ui) {}

        ~SeparatorBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        SeparatorBuilder(const SeparatorBuilder &) = delete;
        SeparatorBuilder &operator=(const SeparatorBuilder &) = delete;

        /// Screen space rectangle the rule was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const float thickness = std::max(1.0f, style.frameBorderSize);
            rect_ = ui_.placeItem(Vector2(ui_.contentRegionWidth(), thickness));
            if (!ui_.isItemVisible(rect_)) {
                return;
            }

            const float centerY = rect_.y + rect_.height * 0.5f;
            ui_.drawList().addLine(Vector2(rect_.x, centerY), Vector2(rect_.maxX(), centerY),
                                   style.color(ColorIndex::Separator), thickness);
        }

      private:
        Ui &ui_;
        Rect rect_;
        bool emitted_ = false;
    };

    //===----------------------------------------------------------------------===//
    // Separator text
    //===----------------------------------------------------------------------===//

    /// A section header: the label sits along the row with a rule filling the space on
    /// either side of it, everything on the same vertical centre. The label is left of
    /// centre by default; textPosition() moves it.
    class SeparatorTextBuilder {
      public:
        SeparatorTextBuilder(Ui &ui, std::string_view label) noexcept
            : ui_(ui), label_(displayPartOf(label)) {}

        ~SeparatorTextBuilder() noexcept {
            if (!emitted_) {
                emit();
            }
        }

        SeparatorTextBuilder(const SeparatorTextBuilder &) = delete;
        SeparatorTextBuilder &operator=(const SeparatorTextBuilder &) = delete;

        /// Where the label sits along the row, as a fraction of the row width: zero
        /// pins it to the left edge, one to the right edge, and 0.4 is slightly left of
        /// centre. The rules take whatever room is left on either side, so the side
        /// without room is simply not drawn.
        SeparatorTextBuilder &textPosition(float fraction) noexcept {
            textPosition_ = fraction;
            return *this;
        }

        /// Screen space rectangle the header was placed into.
        Rect rect() noexcept {
            if (!emitted_) {
                emit();
            }
            return rect_;
        }

        void emit() noexcept {
            if (emitted_) {
                return;
            }
            emitted_ = true;

            const Style &style = ui_.currentStyle();
            const float thickness = std::max(1.0f, style.frameBorderSize);
            const Vector2 labelSize = ui_.calcTextSize(label_);
            rect_ = ui_.placeItem(
                Vector2(ui_.contentRegionWidth(), std::max(labelSize.y, thickness) + style.framePadding.y));
            if (!ui_.isItemVisible(rect_)) {
                return;
            }

            // Clamped so a label wider than the row still starts inside it.
            const float lowestX = rect_.x;
            const float highestX = std::max(rect_.x, rect_.maxX() - labelSize.x);
            const float desiredX = rect_.x + rect_.width * textPosition_ - labelSize.x * 0.5f;
            const float textX = std::min(std::max(desiredX, lowestX), highestX);

            const float centerY = rect_.y + rect_.height * 0.5f;
            const float gap = style.itemInnerSpacing.x;
            const float leadingEndX = textX - gap;
            const float trailingStartX = textX + labelSize.x + gap;
            if (leadingEndX > rect_.x) {
                ui_.drawList().addLine(Vector2(rect_.x, centerY), Vector2(leadingEndX, centerY),
                                       style.color(ColorIndex::Separator), thickness);
            }
            if (trailingStartX < rect_.maxX()) {
                ui_.drawList().addLine(Vector2(trailingStartX, centerY), Vector2(rect_.maxX(), centerY),
                                       style.color(ColorIndex::Separator), thickness);
            }
            ui_.renderText(Vector2(textX, rect_.y + (rect_.height - labelSize.y) * 0.5f), label_,
                           style.color(ColorIndex::Text), ui_.defaultFont(), 0.0f);
        }

      private:
        Ui &ui_;
        Rect rect_;
        std::string_view label_;
        float textPosition_ = 0.4f;
        bool emitted_ = false;
    };
    //===----------------------------------------------------------------------===//
    // Window
    //===----------------------------------------------------------------------===//

    struct WindowScope {
      public:
        explicit WindowScope(Context &ctx, char *title) noexcept : ctx(ctx) {
            params.title = title;
            visible = ctx.beginWindow(params);
            if (!visible) {
                closed = true;
            }
        }

        ~WindowScope() noexcept { close(); }

        WindowScope(const WindowScope &) = delete;
        WindowScope &operator=(const WindowScope &) = delete;

        /// Places the window. By default (PositionMode::FirstUseEver) the position
        /// only takes effect the first time the window is declared, so chaining it
        /// every frame does not cancel dragging. Use pin() for HUD-style windows
        /// that must stay glued to a spot.
        WindowScope &position(Vector2 value) noexcept {
            params.position = value;
            params.hasPosition = true;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &position(float x, float y) noexcept { return position(Vector2(x, y)); }

        /// Selects how an explicitly requested position is applied.
        WindowScope &position(Vector2 value, PositionMode mode) noexcept {
            params.position = value;
            params.hasPosition = true;
            params.positionMode = mode;
            ctx.applyWindowParams(params);
            return *this;
        }

        /// Pins the window to its requested position on every frame.
        WindowScope &pin() noexcept {
            params.positionMode = PositionMode::Always;
            if (params.hasPosition) {
                ctx.applyWindowParams(params);
            }
            return *this;
        }

        WindowScope &size(Vector2 value) noexcept {
            params.size = value;
            params.hasWidth = true;
            params.hasHeight = true;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &size(float width, float height) noexcept { return size(Vector2(width, height)); }

        /// Fixes the width; the height keeps shrink wrapping the contents.
        WindowScope &width(float value) noexcept {
            params.size.x = value;
            params.hasWidth = true;
            ctx.applyWindowParams(params);
            return *this;
        }

        /// Fixes the height; the width keeps shrink wrapping the contents.
        WindowScope &height(float value) noexcept {
            params.size.y = value;
            params.hasHeight = true;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &padding(Vector2 value) noexcept {
            params.padding = value;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &titleBar(bool enabled = true) noexcept {
            params.hasTitleBar = enabled;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &movable(bool enabled = true) noexcept {
            params.movable = enabled;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &background(bool enabled = true) noexcept {
            params.drawBackground = enabled;
            ctx.applyWindowParams(params);
            return *this;
        }

        WindowScope &collapsed(bool value) noexcept {
            params.hasCollapsed = true;
            params.collapsed = value;
            ctx.applyWindowParams(params);
            return *this;
        }

        /// Binds the window's visibility to a caller owned flag.
        WindowScope &open(const bool *openState) noexcept {
            params.openState = openState;
            if (openState != nullptr && !*openState) {
                close();
            }
            return *this;
        }

        /// Runs \p closure with a Ui while the window scope is active, so the body
        /// can declare widgets without repeating the context.
        template <class Body>
            requires std::invocable<Body &, Ui &>
        WindowScope &body(Body &&closure) {
            if (isVisible()) {
                Ui ui(ctx);
                closure(ui);
            }
            return *this;
        }

        bool isVisible() const noexcept { return visible && !closed; }

        void close() noexcept {
            if (closed) {
                return;
            }
            if (visible) {
                ctx.endWindow();
                visible = false;
            }
            closed = true;
        }

      private:
        Context &ctx;
        WindowParams params;
        bool visible = false;
        bool closed = false;
    };

    //===----------------------------------------------------------------------===//
    // Ui widget factories
    //===----------------------------------------------------------------------===//

    inline auto Ui::text(std::string_view content) noexcept -> TextBuilder {
        return TextBuilder(*this, content);
    }

    inline auto Ui::button(std::string_view label) noexcept -> ButtonBuilder {
        return ButtonBuilder(*this, label);
    }

    inline auto Ui::checkbox(std::string_view label) noexcept -> CheckboxBuilder {
        return CheckboxBuilder(*this, label);
    }

    inline auto Ui::radioButton(std::string_view label) noexcept -> RadioButtonBuilder {
        return RadioButtonBuilder(*this, label);
    }

    inline auto Ui::sliderFloat(std::string_view label) noexcept -> FloatSliderBuilder {
        return FloatSliderBuilder(*this, label);
    }

    inline auto Ui::sliderInt(std::string_view label) noexcept -> IntSliderBuilder {
        return IntSliderBuilder(*this, label);
    }

    inline auto Ui::inputText(std::string_view label) noexcept -> TextInputBuilder {
        return TextInputBuilder(*this, label);
    }

    inline auto Ui::progressBar(std::string_view label) noexcept -> ProgressBarBuilder {
        return ProgressBarBuilder(*this, label);
    }

    inline auto Ui::separator() noexcept -> SeparatorBuilder {
        return SeparatorBuilder(*this);
    }

    inline auto Ui::separatorText(std::string_view label) noexcept -> SeparatorTextBuilder {
        return SeparatorTextBuilder(*this, label);
    }

    inline auto Ui::switchToggle(std::string_view label) noexcept -> SwitchToggleBuilder {
        return SwitchToggleBuilder(*this, label);
    }

} // namespace ReZeroGui
