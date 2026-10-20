#include "prx/libSceFreeType/include/GuestObjects.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "tests/FontFixture.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace FreeType;

extern "C" {
int APS5_VABI FT_Set_Char_Size_nid_postfix(GuestFaceRec*, std::int64_t, std::int64_t, std::uint32_t, std::uint32_t);
int APS5_VABI FT_Get_Kerning_nid_postfix(GuestFaceRec*, std::uint32_t, std::uint32_t, std::uint32_t, GuestVector*);
int APS5_VABI FT_Load_Glyph_nid_postfix(GuestFaceRec*, std::uint32_t, std::int32_t);
}

static void Check(bool value, int line) {
    if (!value) {
        std::fprintf(stderr, "FreeType sizing check failed at line %d\n", line);
        std::abort();
    }
}
#define Require(value) Check((value), __LINE__)

template<typename TCall>
static bool ThrowsWith(TCall call, const char* prefix) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).rfind(prefix, 0) == 0;
    }
    return false;
}

struct FacePair {
    GuestLibrary library{};
    GuestFace guest{};
    FT_Library oracleLibrary = nullptr;
    FT_Face oracle = nullptr;

    FacePair(const std::vector<unsigned char>& bytes, FT_Long index = 0) {
        Require(FT_Init_FreeType(&library.host) == 0);
        Require(FT_New_Memory_Face(library.host, bytes.data(), static_cast<FT_Long>(bytes.size()), index, &guest.host) == 0);
        guest.library = &library;
        SyncFace(guest);
        Require(FT_Init_FreeType(&oracleLibrary) == 0);
        Require(FT_New_Memory_Face(oracleLibrary, bytes.data(), static_cast<FT_Long>(bytes.size()), index, &oracle) == 0);
    }

    ~FacePair() {
        Require(FT_Done_Face(guest.host) == 0);
        Require(FT_Done_FreeType(library.host) == 0);
        Require(FT_Done_Face(oracle) == 0);
        Require(FT_Done_FreeType(oracleLibrary) == 0);
    }
};

static void SizeMatches(const GuestSizeMetrics& guest, const FT_Size_Metrics& native) {
    Require(guest.x_ppem == native.x_ppem && guest.y_ppem == native.y_ppem);
    Require(guest.x_scale == native.x_scale && guest.y_scale == native.y_scale);
    Require(guest.ascender == native.ascender && guest.descender == native.descender);
    Require(guest.height == native.height && guest.max_advance == native.max_advance);
}

static void CheckKerning(FacePair& pair, bool hasKerning) {
    constexpr std::uint64_t sentinel = 0xA1B2C3D4E5F60718ull;
    struct Output {
        std::uint64_t before = sentinel;
        GuestVector value{987654321, -123456789};
        std::uint64_t after = sentinel;
    };
    for (const auto mode : {std::uint32_t{FT_KERNING_DEFAULT}, std::uint32_t{FT_KERNING_UNFITTED},
            std::uint32_t{FT_KERNING_UNSCALED}, std::numeric_limits<std::uint32_t>::max()}) {
        for (const auto glyphs : {std::array<std::uint32_t, 2>{1, 2}, {2, 1}, {0, 2}, {1, 0},
                {std::numeric_limits<std::uint32_t>::max(), 2}}) {
            Output out;
            FT_Vector expected{123, 456};
            const FT_Error error = FT_Get_Kerning(pair.oracle, glyphs[0], glyphs[1], mode, &expected);
            Require(FT_Get_Kerning_nid_postfix(&pair.guest.rec, glyphs[0], glyphs[1], mode, &out.value) == error);
            Require(out.value.x == expected.x && out.value.y == expected.y);
            Require(out.before == sentinel && out.after == sentinel);
            if (glyphs[0] == 1 && glyphs[1] == 2 && hasKerning) Require(out.value.x < 0);
            if (!hasKerning) Require(out.value.x == 0 && out.value.y == 0);
        }
    }
    Output invalid;
    Require(FT_Get_Kerning_nid_postfix(nullptr, 1, 2, 0, &invalid.value) == FT_Err_Invalid_Face_Handle);
    Require(invalid.value.x == 987654321 && invalid.value.y == -123456789 && invalid.before == sentinel && invalid.after == sentinel);
    Require(FT_Get_Kerning_nid_postfix(nullptr, 1, 2, 0, nullptr) == FT_Err_Invalid_Face_Handle);
    Require(FT_Get_Kerning_nid_postfix(&pair.guest.rec, 1, 2, 0, nullptr) == FT_Err_Invalid_Argument);
}

int main() {
    using FontFixture::BuildFont;
    using FontFixture::Words;
    for (const bool hasKerning : {true, false}) {
        std::map<std::string, std::vector<unsigned char>> tables{
            {"cmap", Words({0, 1, 3, 1, 0, 12, 4, 40, 0, 6, 4, 1, 2, 'A', 'V', 0xFFFF, 0, 'A', 'V', 0xFFFF, 1 - 'A', 2 - 'V', 1, 0, 0, 0})}};
        if (hasKerning) tables["kern"] = Words({0, 1, 0, 20, 1, 1, 6, 0, 0, 1, 2, -200});
        const auto bytes = BuildFont(3, tables);
        FacePair pair(bytes);
        struct Setting {
            FT_F26Dot6 width;
            FT_F26Dot6 height;
            std::uint32_t horizontalDpi;
            std::uint32_t verticalDpi;
        };
        for (const auto setting : {Setting{100 * 64, 100 * 64, 72, 72}, {12 * 64, 12 * 64, 72, 72},
                {0, 11 * 64 + 32, 96, 144}, {20 * 64 + 16, 0, 0, 110}, {0, 0, 0, 0},
                {-64, 16 * 64, 120, 0}, {16 * 64, -64, 0, 144}}) {
            const FT_Error expected = FT_Set_Char_Size(pair.oracle, setting.width, setting.height, setting.horizontalDpi, setting.verticalDpi);
            Require(FT_Set_Char_Size_nid_postfix(&pair.guest.rec, setting.width, setting.height, setting.horizontalDpi, setting.verticalDpi) == expected);
            Require(expected == 0);
            SizeMatches(pair.guest.size.metrics, pair.oracle->size->metrics);
            Require(FT_Load_Glyph_nid_postfix(&pair.guest.rec, 1, FT_LOAD_NO_HINTING) == FT_Load_Glyph(pair.oracle, 1, FT_LOAD_NO_HINTING));
            Require(pair.guest.slot.advance.x == pair.oracle->glyph->advance.x && pair.guest.slot.advance.y == pair.oracle->glyph->advance.y);
        }
        const FT_Error invalidSize = FT_Set_Char_Size(pair.oracle, 65536 * 64, 100 * 64, 72, 72);
        Require(invalidSize != 0);
        Require(FT_Set_Char_Size_nid_postfix(&pair.guest.rec, 65536 * 64, 100 * 64, 72, 72) == invalidSize);
        SizeMatches(pair.guest.size.metrics, pair.oracle->size->metrics);
        Require(FT_Set_Char_Size_nid_postfix(&pair.guest.rec, std::numeric_limits<std::int64_t>::min(), 0, 72, 72) == 0);
        Require(FT_Set_Char_Size(pair.oracle, 64, 64, 72, 72) == 0);
        SizeMatches(pair.guest.size.metrics, pair.oracle->size->metrics);
        Require(FT_Set_Char_Size_nid_postfix(&pair.guest.rec, 100 * 64, 100 * 64, 72, 72) == 0);
        Require(FT_Set_Char_Size(pair.oracle, 100 * 64, 100 * 64, 72, 72) == 0);
        CheckKerning(pair, hasKerning);
        const auto safeMetrics = pair.oracle->size->metrics;
        const auto maxPosition = static_cast<std::uint64_t>(std::numeric_limits<FT_Pos>::max());
        const auto productOverflow = static_cast<std::int64_t>(maxPosition / 72 + 1);
        for (const auto width : {std::numeric_limits<std::int64_t>::max(), productOverflow}) {
            Require(ThrowsWith([&] { FT_Set_Char_Size_nid_postfix(&pair.guest.rec, width, 64, 72, 72); }, "FT_Set_Char_Size:"));
            SizeMatches(pair.guest.size.metrics, safeMetrics);
        }
        const auto heightOverflow = static_cast<std::int64_t>(maxPosition / std::numeric_limits<std::uint32_t>::max() + 1);
        Require(ThrowsWith([&] { FT_Set_Char_Size_nid_postfix(&pair.guest.rec, 64, heightOverflow, 72, std::numeric_limits<std::uint32_t>::max()); }, "FT_Set_Char_Size:"));
        SizeMatches(pair.guest.size.metrics, safeMetrics);
        Require(FT_Set_Char_Size_nid_postfix(nullptr, std::numeric_limits<std::int64_t>::max(), 64, 72, 72) == FT_Err_Invalid_Face_Handle);
        Require(FT_Set_Char_Size_nid_postfix(&pair.guest.rec, 12 * 64, 14 * 64, 96, 144) == 0);
        Require(FT_Set_Char_Size(pair.oracle, 12 * 64, 14 * 64, 96, 144) == 0);
        SizeMatches(pair.guest.size.metrics, pair.oracle->size->metrics);
        CheckKerning(pair, hasKerning);

        FacePair probe(bytes, -1);
        Require(probe.guest.host->size == nullptr);
        Require(FT_Set_Char_Size_nid_postfix(&probe.guest.rec, std::numeric_limits<std::int64_t>::max(), 64, 72, 72) == FT_Err_Invalid_Size_Handle);
        GuestVector unchanged{123, 456};
        Require(ThrowsWith([&] { FT_Get_Kerning_nid_postfix(&probe.guest.rec, 1, 2, FT_KERNING_DEFAULT, &unchanged); }, "FT_Get_Kerning:"));
        Require(unchanged.x == 123 && unchanged.y == 456);
        FT_Vector unscaled{};
        Require(FT_Get_Kerning_nid_postfix(&probe.guest.rec, 1, 2, FT_KERNING_UNSCALED, &unchanged) == FT_Get_Kerning(probe.oracle, 1, 2, FT_KERNING_UNSCALED, &unscaled));
        Require(unchanged.x == unscaled.x && unchanged.y == unscaled.y);
    }
    std::puts("FreeType point sizing, DPI and kerning: ok");
}
