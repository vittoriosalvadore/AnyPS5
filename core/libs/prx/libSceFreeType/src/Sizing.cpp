#include "prx/libSceFreeType/include/GuestObjects.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

using namespace FreeType;

extern "C" {

int APS5_VABI FT_Set_Char_Size_nid_postfix(GuestFaceRec* face, std::int64_t charWidth, std::int64_t charHeight, std::uint32_t horizontalDpi, std::uint32_t verticalDpi) {
    GuestFace* owner = FaceOf(face);
    const FT_Face host = owner ? owner->host : nullptr;
    if (!host || !host->size) {
        const FT_Error error = FT_Set_Char_Size(host, 0, 0, horizontalDpi, verticalDpi);
        if (host) SyncSize(*owner);
        return error;
    }
    if (charWidth == 0) charWidth = charHeight;
    else if (charHeight == 0) charHeight = charWidth;
    charWidth = std::max<std::int64_t>(charWidth, 64);
    charHeight = std::max<std::int64_t>(charHeight, 64);
    if (horizontalDpi == 0) horizontalDpi = verticalDpi;
    else if (verticalDpi == 0) verticalDpi = horizontalDpi;
    if (horizontalDpi == 0) horizontalDpi = verticalDpi = 72;
    const auto maxSize = static_cast<std::uint64_t>(std::numeric_limits<FT_F26Dot6>::max());
    const auto maxProduct = static_cast<std::uint64_t>(std::numeric_limits<FT_Pos>::max() - 36);
    if (static_cast<std::uint64_t>(charWidth) > maxSize || static_cast<std::uint64_t>(charHeight) > maxSize ||
        static_cast<std::uint64_t>(charWidth) > maxProduct / horizontalDpi || static_cast<std::uint64_t>(charHeight) > maxProduct / verticalDpi) {
        throw std::runtime_error("FT_Set_Char_Size: requested point size and DPI exceed the host FreeType range");
    }
    const FT_Error error = FT_Set_Char_Size(host, static_cast<FT_F26Dot6>(charWidth), static_cast<FT_F26Dot6>(charHeight), horizontalDpi, verticalDpi);
    SyncSize(*owner);
    return error;
}

int APS5_VABI FT_Get_Kerning_nid_postfix(GuestFaceRec* face, std::uint32_t leftGlyph, std::uint32_t rightGlyph, std::uint32_t mode, GuestVector* kerning) {
    const GuestFace* owner = FaceOf(face);
    const FT_Face host = owner ? owner->host : nullptr;
    if (!host || !kerning) return FT_Get_Kerning(host, leftGlyph, rightGlyph, mode, nullptr);
    if (!host->size && mode != FT_KERNING_UNSCALED) {
        throw std::runtime_error("FT_Get_Kerning: scaled kerning without an active font size is not implemented");
    }
    FT_Vector delta{};
    const FT_Error error = FT_Get_Kerning(host, leftGlyph, rightGlyph, mode, &delta);
    *kerning = {delta.x, delta.y};
    return error;
}

}
