/*******************************************************************************
 * fontpack_extension - FontManager
 * Copyright (C) 2026 eternalfuture-e38299
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Author: eternalfuture-e38299
 * GitHub: https://github.com/eternalfuture-e38299
 * Created: 2026/8/4
 *******************************************************************************/

#pragma once

#include "FontPack.hpp"
#include "tefkernel-cpp-wrapper/patchlib/method.hpp"
#include "tefkernel-cpp-wrapper/patchlib/field.hpp"

namespace FontManager {
    inline std::unique_ptr<FontPack> font_pack = nullptr;

    // SpriteFont(Texture2D[] textures, Glyph[] glyphs, int lineSpacing, float spacing, char? defaultCharacter)
    inline TEFKernel::PatchLib::Method SpriteFont_ctor{};

    // Rectangle(int x, int y, int width, int height)
    inline TEFKernel::PatchLib::Method Rectangle_ctor{};

    patch_handle_t CreateSpriteFontFromBMFont(const std::string& fontName);

    inline TEFKernel::PatchLib::Type Glyph ;// struct Glyph

    bool LoadSpriteFont_Hook(patch_handle_t instance, void **args,
                                  const patch_method_signature_t *sig_info, void *result);
}