#pragma once

#include <cstdint>
#include <cstddef>
#include <ReZeroGui/ReZeroLib.h>
#include <array>

namespace ReZeroGui {
    enum class ColorIndex : std::uint8_t {
        Text,
        TextDisabled,
        WindowBackground,
        WindowBorder,
        TitleBackground,
        TitleBackgroundActive,
        TitleBackgroundCollapsed,
        Border,
        FrameBackground,
        FrameBackgroundHovered,
        FrameBackgroundActive,
        Button,
        ButtonHovered,
        ButtonActive,
        CheckMark,
        SliderGrab,
        SliderGrabActive,
        Separator,
        ProgressBar,
        Count,
    };

    constexpr std::size_t ColorIndexCount = static_cast<std::size_t>(ColorIndex::Count);

    struct Style {
        // Window
        Vector2 windowPadding{10.0f, 10.0f};
        Vector2 windowMinimumSize{96.0f, 64.0f};
        float windowRounding = 6.0f;
        float windowBorderSize = 1.0f;
        float windowTitleAlignX = 0.0f;

        // Frame (buttons, sliders, inputs)
        Vector2 framePadding{8.0f, 5.0f};
        float frameRounding = 4.0f;
        float frameBorderSize = 1.0f;

        // Layout
        Vector2 itemSpacing{8.0f, 6.0f};
        Vector2 itemInnerSpacing{6.0f, 4.0f};
        float grabMinimumSize = 12.0f;

        // Text
        float fontSize = 16.0f;
        /// Extra advance added after every glyph, in pixels. Zero is faithful to the
        /// font's own metrics; the bundled Proggy faces draw their ink right up to
        /// the edge of the glyph cell, so a value of 1..2 is what makes them
        /// breathe.
        float letterSpacing = 0.0f;

        std::array<Pixel, ColorIndexCount> colors{};

        constexpr Style() noexcept { resetToDefault(); }

        constexpr void resetToDefault() noexcept {
            colors[static_cast<std::size_t>(ColorIndex::Text)] = Pixel::fromRgba(0xFFFFFFFF);
            colors[static_cast<std::size_t>(ColorIndex::TextDisabled)] = Pixel::fromRgba(0x9A9A9AFF);
            colors[static_cast<std::size_t>(ColorIndex::WindowBackground)] = Pixel::fromRgba(0x14161AEE);
            colors[static_cast<std::size_t>(ColorIndex::WindowBorder)] = Pixel::fromRgba(0x2C313AFF);
            colors[static_cast<std::size_t>(ColorIndex::TitleBackground)] = Pixel::fromRgba(0x1C2029FF);
            colors[static_cast<std::size_t>(ColorIndex::TitleBackgroundActive)] = Pixel::fromRgba(0x2A4C78FF);
            colors[static_cast<std::size_t>(ColorIndex::TitleBackgroundCollapsed)] = Pixel::fromRgba(0x181B21FF);
            colors[static_cast<std::size_t>(ColorIndex::Border)] = Pixel::fromRgba(0x3A414DFF);

            colors[static_cast<std::size_t>(ColorIndex::FrameBackground)] = Pixel::fromRgba(0x22262EFF);
            colors[static_cast<std::size_t>(ColorIndex::FrameBackgroundHovered)] = Pixel::fromRgba(0x2B303AFF);
            colors[static_cast<std::size_t>(ColorIndex::FrameBackgroundActive)] = Pixel::fromRgba(0x36404FFF);
            colors[static_cast<std::size_t>(ColorIndex::Button)] = Pixel::fromRgba(0x2F6FB2FF);
            colors[static_cast<std::size_t>(ColorIndex::ButtonHovered)] = Pixel::fromRgba(0x3A83CFFF);
            colors[static_cast<std::size_t>(ColorIndex::ButtonActive)] = Pixel::fromRgba(0x245FA0FF);
            colors[static_cast<std::size_t>(ColorIndex::CheckMark)] = Pixel::fromRgba(0x6FC3FFFF);
            colors[static_cast<std::size_t>(ColorIndex::SliderGrab)] = Pixel::fromRgba(0x8A93A6FF);
            colors[static_cast<std::size_t>(ColorIndex::SliderGrabActive)] = Pixel::fromRgba(0xC7D1E5FF);
            colors[static_cast<std::size_t>(ColorIndex::Separator)] = Pixel::fromRgba(0x3A414DFF);


            colors[static_cast<std::size_t>(ColorIndex::ProgressBar)] = Pixel::fromRgba(0x2F6FB2FF);
        }

        constexpr Pixel color(ColorIndex index) const noexcept { return colors[static_cast<std::size_t>(index)]; }

        constexpr auto scaled(float scale) const noexcept -> Style {

            Style result = *this;
            result.windowPadding = windowPadding * scale;
            result.windowMinimumSize = windowMinimumSize * scale;
            result.windowRounding = windowRounding * scale;
            result.windowBorderSize = windowBorderSize * scale;
            result.framePadding = framePadding * scale;
            result.frameRounding = frameRounding * scale;
            result.frameBorderSize = frameBorderSize * scale;
            result.itemSpacing = itemSpacing * scale;
            result.itemInnerSpacing = itemInnerSpacing * scale;
            result.grabMinimumSize = grabMinimumSize * scale;
            result.fontSize = fontSize * scale;
            result.letterSpacing = letterSpacing * scale;
            return result;
        }
    };
} // namespace ReZeroGui