#pragma once
#include "ReZeroGui/Allocator.h"
#include <ReZeroGui/ReZeroLib.h>
#include <ReZeroTrueType/ReZeroTrueType.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace ReZeroGui {
    using FontId = std::uint32_t;
    constexpr FontId InvalidFont = std::numeric_limits<FontId>::max();

    /// Size the bundled font is rasterized at when the caller asks for nothing
    /// else. Matches Style::fontSize so layout and glyph bitmaps agree.
    constexpr float DefaultFontSizePixels = 16.0f;

#if !defined(REZEROGUI_DEFAULT_FONT_PATH)
    /// Where loadDefaultFont() looks for the bundled face when the build system
    /// does not inject a path. A relative path is resolved against the process
    /// working directory, which for a shipped executable means next to the exe.
#    define REZEROGUI_DEFAULT_FONT_PATH "ProggyForever-Regular.ttf"
#endif

    /// Device space placement of one glyph.
    ///
    /// position0/position1 are the top-left and bottom-right corners of the quad
    /// in UI pixels; uv0/uv1 are the matching corners inside the font atlas. Both
    /// are spelled as (minimum, maximum) so they can be handed straight to
    /// DrawList::addTexturedQuad.
    struct GlyphQuad {
        Vector2 position0;
        Vector2 position1;
        Vector2 uv0;
        Vector2 uv1;
    };

    /// Owns the CPU side of the glyph atlases and answers every text query.
    ///
    /// The face bytes are only borrowed (see FontParams), so they have to stay
    /// alive until build() returns. The atlases themselves are owned here and are
    /// exposed through atlasImage() so the backend can upload them as a texture.
    struct FontAtlas {
        struct FontParams {
            const std::uint8_t *data = nullptr; ///< TTF/OTF bytes, non-owning.
            std::size_t size = 0;
            float sizePixels = DefaultFontSizePixels;
            std::uint32_t firstCodepoint = 0x20;
            std::uint32_t lastCodepoint = 0x7E;
        };

        struct Font {
            FontParams params;
            /// Distance from the baseline to the top of the line, in pixels.
            float ascent = 0.0f;
            /// Distance from the baseline to the bottom of the line. Negative for
            /// the usual TrueType vertical metrics.
            float descent = 0.0f;
            /// ascent - descent + lineGap, i.e. the baseline to baseline distance.
            float lineHeight = 0.0f;
            ReZeroTrueType::PackedAtlas atlas;

            explicit Font(ReZeroTrueType::AllocatorView source) noexcept : atlas(source) {}
        };

        /// The TrueType library has its own AllocatorView, so the UI allocator is
        /// wrapped once here. `source` has to outlive the atlas, which is why the
        /// view is built from the member below rather than from the constructor
        /// argument.
        explicit FontAtlas(AllocatorView source) noexcept : allocator(source), ttfAllocator(allocator) {}

        FontAtlas(const FontAtlas &) = delete;
        FontAtlas &operator=(const FontAtlas &) = delete;

        /// Registers a face. Nothing is rasterized until build(), so a batch of
        /// faces only pays for one atlas allocation round.
        auto addFont(const FontParams &params) noexcept -> FontId {
            if (params.data == nullptr || params.size == 0) {
                return InvalidFont;
            }
            if (params.lastCodepoint < params.firstCodepoint) {
                return InvalidFont;
            }
            fonts.emplace_back(ttfAllocator);
            fonts.back().params = params;
            return static_cast<FontId>(fonts.size() - 1);
        }

        /// Registers the bundled face, reading it from disk. `path` overrides the
        /// build time default. The file bytes are owned by the atlas from here on,
        /// so the caller does not have to keep them around.
        auto loadDefaultFont(const char *path = nullptr, float sizePixels = DefaultFontSizePixels) noexcept -> FontId {
            if (defaultFontId != InvalidFont) {
                return defaultFontId;
            }
            if (!readDefaultFontFile(path)) {
                return InvalidFont;
            }

            FontParams params;
            params.data = defaultFontData.data();
            params.size = defaultFontData.size();
            params.sizePixels = sizePixels;
            defaultFontId = addFont(params);
            return defaultFontId;
        }

        /// Rasterizes every registered face into its atlas. Repeat calls are a
        /// no-op, so it is safe to call from the frame loop.
        auto build() noexcept -> bool {
            if (built) {
                return true;
            }
            if (fonts.empty()) {
                return false;
            }
            for (Font &font : fonts) {
                if (!rasterize(font)) {
                    return false;
                }
            }
            if (defaultFontId == InvalidFont) {
                defaultFontId = 0;
            }
            built = true;
            return true;
        }

        auto isBuilt() const noexcept -> bool { return built; }
        auto defaultFont() const noexcept -> FontId { return defaultFontId; }
        auto fontCount() const noexcept -> std::size_t { return fonts.size(); }


        auto ascent(FontId id) const noexcept -> float {
            const Font *font = fontFor(id);
            return font != nullptr ? font->ascent : 0.0f;
        }

        auto descent(FontId id) const noexcept -> float {
            const Font *font = fontFor(id);
            return font != nullptr ? font->descent : 0.0f;
        }

        auto lineHeight(FontId id) const noexcept -> float {
            const Font *font = fontFor(id);
            REZERO_ASSERT(font != nullptr);
            return font != nullptr ? font->lineHeight : 0.0f;
        }

        /// Horizontal pen movement for one code point, in pixels. Zero for
        /// code points the atlas does not carry.
        auto glyphAdvance(FontId id, std::uint32_t codepoint) const noexcept -> float {
            const Font *font = fontFor(id);
            if (font == nullptr) {
                return 0.0f;
            }
            const ReZeroTrueType::GlyphMetric *glyph = font->atlas.find(codepoint);
            return glyph != nullptr ? glyph->advance : 0.0f;
        }

        /// Places one glyph and advances the pen.
        ///
        /// On entry `penX` is the pen position and `baselineY` the baseline the
        /// glyph sits on. On success `penX` is advanced by the glyph's advance
        /// width while `baselineY` is left alone.
        ///
        /// Blank glyphs (spaces, and code points the face has no outline for)
        /// report success with a quad whose UVs are empty so the pen still moves;
        /// callers are expected to test the UVs before emitting geometry, which is
        /// what Context::renderText does.
        auto getGlyphQuad(FontId id, std::uint32_t codepoint, float *penX, float *baselineY, GlyphQuad &quad) const
            noexcept -> bool {
            const Font *font = fontFor(id);
            if (font == nullptr || penX == nullptr || baselineY == nullptr) {
                return false;
            }

            const ReZeroTrueType::GlyphMetric *glyph = font->atlas.find(codepoint);
            if (glyph == nullptr) {
                return false;
            }

            const float pen = *penX;
            *penX = pen + glyph->advance;
            quad = GlyphQuad{};

            if (glyph->blank || glyph->w == 0 || glyph->h == 0) {
                return true;
            }

            // Snap the glyph origin to whole pixels. The atlas was rasterized at
            // exactly sizePixels, so a 1:1 texel-to-pixel quad resamples nothing
            // and every pixel centre lands on a texel centre. The pen keeps its
            // fractional part, so snapping the drawn quad does not make advances
            // drift.
            const float originX = std::round(pen);
            const float originY = std::round(*baselineY);
            quad.position0 = Vector2(originX + static_cast<float>(glyph->ox), originY + static_cast<float>(glyph->oy));
            quad.position1 = Vector2(quad.position0.x + static_cast<float>(glyph->w),
                                     quad.position0.y + static_cast<float>(glyph->h));
            quad.uv0 = Vector2(glyph->u0, glyph->v0);
            quad.uv1 = Vector2(glyph->u1, glyph->v1);
            return true;
        }

        /// The atlas image backing a face, or null when it is not built yet.
        /// Pixels are RGBA8 with white RGB and coverage in alpha.
        auto atlasImage(FontId id) const noexcept -> const ReZeroTrueType::Image * {
            const Font *font = fontFor(id);
            if (font == nullptr) {
                return nullptr;
            }
            const ReZeroTrueType::Image &grid = font->atlas.grid;
            if (grid.width == 0 || grid.height == 0 || grid.data() == nullptr) {
                return nullptr;
            }
            return &grid;
        }

      private:
        auto isValid(FontId id) const noexcept -> bool {
            return id != InvalidFont && static_cast<std::size_t>(id) < fonts.size();
        }

        auto fontFor(FontId id) const noexcept -> const Font * {
            return isValid(id) ? &fonts[static_cast<std::size_t>(id)] : nullptr;
        }

        /// 一个字体 -> 一张图集：解析、装箱、光栅化一次做完。
        auto rasterize(Font &font) noexcept -> bool {
            // The TrueType reader only reads the font bytes but still asks for a
            // mutable span, while FontParams deliberately keeps the caller's data
            // const so registration never implies write access.
            auto *bytes = const_cast<std::uint8_t *>(font.params.data);
            auto loaded = ReZeroTrueType::loadTTFFromMemory(ttfAllocator, {bytes, font.params.size});
            if (!loaded) {
                return false;
            }
            ReZeroTrueType::Font &face = *loaded;

            ReZeroTrueType::FontPackerParams packerParams;
            packerParams.pixelSize = font.params.sizePixels;
            // Codepoints are added explicitly below, so the requested range is
            // what ends up in the atlas instead of the packer's own default.
            packerParams.includeAscii = false;
            // 8x8 samples per pixel rather than the library default 4x4. A stem is one
            // or two pixels wide, so the sampling grid is what decides how evenly its
            // weight comes out at small sizes; the atlas is only built once anyway.
            packerParams.rasterize.subpixelBits = 3;

            ReZeroTrueType::FontPacker packer(ttfAllocator, face, packerParams);
            packer.includeRange(font.params.firstCodepoint, font.params.lastCodepoint);

            auto packed = packer.pack();
            if (!packed) {
                return false;
            }
            font.atlas = std::move(*packed);
            darkenStems(font.atlas);

            const float pixelSize = font.params.sizePixels;
            font.ascent = face.unitsToPixel(static_cast<float>(face.ascent), pixelSize);
            font.descent = face.unitsToPixel(static_cast<float>(face.descent), pixelSize);
            font.lineHeight = face.lineHeight(pixelSize);
            return true;
        }

        /// Small text loses apparent weight. Coverage is blended in the render target's
        /// 8 bit space, which is not linear, so a half covered pixel comes out lighter
        /// than half the ink - and a one pixel stem is almost nothing but half covered
        /// pixels. Nudging the mid tones up compensates; full coverage stays full, so
        /// large text is barely touched.
        static void darkenStems(ReZeroTrueType::PackedAtlas &atlas) noexcept {
            for (const ReZeroTrueType::GlyphMetric &glyph : atlas.glyphs) {
                if (glyph.blank || glyph.w == 0 || glyph.h == 0) {
                    continue;
                }
                for (std::uint32_t row = 0; row < glyph.h; ++row) {
                    for (std::uint32_t column = 0; column < glyph.w; ++column) {
                        ReZeroTrueType::Pixel &texel = atlas.grid[glyph.x + column, glyph.y + row];
                        const std::uint32_t alpha = texel.a;
                        texel.a = static_cast<std::uint8_t>(alpha + (alpha * (255u - alpha)) / 1275u);
                    }
                }
            }
        }

        auto readDefaultFontFile(const char *path) noexcept -> bool {
            static constexpr const char *FallbackPath = "ProggyForever-Regular.ttf";

            if (path != nullptr && readFile(path, defaultFontData)) {
                return true;
            }
            if (readFile(REZEROGUI_DEFAULT_FONT_PATH, defaultFontData)) {
                return true;
            }
            if (readFile(FallbackPath, defaultFontData)) {
                return true;
            }
            return false;
        }

        static auto readFile(const char *path, std::vector<std::uint8_t> &out) noexcept -> bool {
            if (path == nullptr || path[0] == '\0') {
                return false;
            }

            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream.is_open()) {
                return false;
            }

            const std::streamoff size = stream.tellg();
            if (size <= 0) {
                return false;
            }

            out.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(out.data()), size);
            if (!stream) {
                out.clear();
                return false;
            }
            return true;
        }

        AllocatorView allocator;
        ReZeroTrueType::AllocatorView ttfAllocator;
        std::vector<Font> fonts;
        /// Backing store for the bundled face; FontParams only borrows it.
        std::vector<std::uint8_t> defaultFontData;
        FontId defaultFontId = InvalidFont;
        bool built = false;
    };
} // namespace ReZeroGui
