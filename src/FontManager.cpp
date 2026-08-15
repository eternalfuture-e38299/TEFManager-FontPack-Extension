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

#include "FontManager.hpp"

#include "Log.hpp"
#include "tefkernel-cpp-wrapper/patchlib/struct/string.hpp"
#include "tefkernel-cpp-wrapper/patchlib/struct/array.hpp"
#include "tefkernel-cpp-wrapper/tefkernel/terraria/texture2d.h"

static patch_handle_t CreateTexture2D(const TextureData &texture) {
    // 根据通道数确定纹理格式
    texture_format_t format;
    switch (texture.channels) {
        case 1:
            format = TEXTURE_FORMAT_ALPHA8;
            break;
        case 3:
        case 4:
            format = TEXTURE_FORMAT_RGBA32;
            break;
        default:
            LOGE("Unsupported texture channels: %d", texture.channels);
            return nullptr;
    }

    // 创建纹理
    // 注意：terraria_texture2d_create 需要 RGBA 格式，如果原数据是 RGB，需要转换
    patch_handle_t texHandle = terraria_texture2d_create(
        texture.width,
        texture.height,
        format,
        const_cast<void *>(static_cast<const void *>(texture.data.data())),
        texture.data.size()
    );

    if (!texHandle) {
        LOGE("Failed to create texture: %dx%d, %d channels",
             texture.width, texture.height, texture.channels);
    } else {
        LOGD("Created texture: %dx%d, %d channels",
             texture.width, texture.height, texture.channels);
    }

    return texHandle;
}

static patch_handle_t CreateTexture2DArray(const std::vector<TextureData> &textures) {
    // 使用 Array<patch_handle_t> 创建数组
    TEFKernel::PatchLib::Struct::Array<patch_handle_t> array(textures.size(), terraria_texture2d_get_class());

    for (size_t i = 0; i < textures.size(); i++) {
        if (patch_handle_t tex = CreateTexture2D(textures[i])) {
            array.Set(i, tex);
        } else {
            LOGE("Failed to create texture at index %zu", i);
            array.Set(i, nullptr);
        }
    }

    return array.GetHandle();
}

namespace {
    // 创建 Glyph 数组
    struct GlyphStruct {
        uint32_t Character;              // offset 0x00
        int32_t BoundsInTexture_X;       // offset 0x04
        int32_t BoundsInTexture_Y;       // offset 0x08
        int32_t BoundsInTexture_Width;   // offset 0x0C
        int32_t BoundsInTexture_Height;  // offset 0x10
        int32_t Cropping_X;              // offset 0x14
        int32_t Cropping_Y;              // offset 0x18
        int32_t Cropping_Width;          // offset 0x1C
        int32_t Cropping_Height;         // offset 0x20
        float LeftSideBearing;           // offset 0x24
        float RightSideBearing;          // offset 0x28
        float Width;                     // offset 0x2C
        float WidthIncludingBearings;    // offset 0x30
        uint8_t TexureIndex;             // offset 0x34
    };
}

// 从 BMFont 字符构建游戏 Glyph 数组
static patch_handle_t CreateGlyphArray(const std::vector<BMFontChar> &chars,
                                        const std::vector<TextureData> &textures) {
    if (chars.empty()) {
        LOGE("No characters to create glyph array");
        return nullptr;
    }

    LOGD("Creating Glyph array with %zu characters", chars.size());

    // 创建 IL2CPP 数组（值类型数组）
    const auto array = patchlib_array_create(chars.size(), FontManager::Glyph.GetHandle());
    if (!array) {
        LOGE("Failed to create Glyph array");
        return nullptr;
    }

    // 填充数据
    size_t validCount = 0;
    size_t skippedCount = 0;
    for (size_t i = 0; i < chars.size(); i++) {
        const auto& bmChar = chars[i];

        // ===== 检查字符是否超出 UTF-16 范围 =====
        // UTF-16 能表示的最大码点是 0x10FFFF
        // 但 BMP (基本多文种平面) 范围是 0x0000 - 0xFFFF
        // Terraria 的 char 类型是 16 位，只能表示 BMP 字符
        if (bmChar.id > 0xFFFF) {
            LOGW("Character 0x%X (index %zu) exceeds UTF-16 BMP range (0xFFFF), skipping",
                 bmChar.id, i);
            skippedCount++;
            continue;  // 跳过这个字符
        }

        if (bmChar.page >= static_cast<int>(textures.size())) {
            LOGW("Invalid page index %d for char %d", bmChar.page, bmChar.id);
            continue;
        }

        // 直接填充结构体数据
        GlyphStruct glyph{};
        memset(&glyph, 0, sizeof(GlyphStruct));

        // 基础值：所有字符统一处理，不做任何额外补偿
        const auto xadvance = static_cast<float>(bmChar.xadvance);
        const auto xoffset = static_cast<float>(bmChar.xoffset);
        const auto width = static_cast<float>(bmChar.width);

        // 填充字段
        glyph.Character = static_cast<uint32_t>(bmChar.id);
        glyph.BoundsInTexture_X = bmChar.x;
        glyph.BoundsInTexture_Y = bmChar.y;
        glyph.BoundsInTexture_Width = bmChar.width;
        glyph.BoundsInTexture_Height = bmChar.height;
        // 关键：Cropping.X 必须为 0，水平偏移只由 LeftSideBearing 承担。
        // 游戏渲染按 LeftSideBearing + Cropping.X 定位，两者都填 xoffset 会导致字形右移。
        glyph.Cropping_X = 0;
        glyph.Cropping_Y = bmChar.yoffset;
        glyph.Cropping_Width = bmChar.width;
        glyph.Cropping_Height = bmChar.height;
        glyph.LeftSideBearing = xoffset;
        glyph.RightSideBearing = xadvance - static_cast<float>(bmChar.width) - xoffset;
        glyph.Width = width;
        glyph.WidthIncludingBearings = static_cast<float>(xadvance);
        glyph.TexureIndex = static_cast<uint8_t>(bmChar.page);

        patchlib_array_set(array, i, &glyph);
        validCount++;
    }

    if (skippedCount > 0) {
        LOGW("Skipped %zu characters that exceed UTF-16 BMP range", skippedCount);
    }

    LOGI("Successfully created Glyph array: %zu/%zu characters",
         validCount, chars.size());

    return array;
}

static std::unordered_map<std::string, std::string> g_fontNameMap = {
    {"Content/Fonts/Death_Text", "death_text"},
    {"Content/Fonts/Mouse_Text", "mouse_text"},
    {"Content/Fonts/Combat_Text", "combat_text"},
    {"Content/Fonts/Combat_Crit", "combat_crit"},
    {"Content/Fonts/Item_Stack", "item_stack"}
};

patch_handle_t FontManager::CreateSpriteFontFromBMFont(const std::string &fontName) {
    if (!font_pack) {
        LOGE("FontPack not loaded");
        return nullptr;
    }

    // 获取字体数据
    const auto font = font_pack->getFont(fontName);
    if (!font) {
        LOGE("Font not found: %s", fontName.c_str());
        return nullptr;
    }

    // 等待纹理加载完成
    font_pack->waitForFont(fontName);

    // 获取所有纹理
    const auto textures = font_pack->getTextures(fontName);
    if (textures.empty()) {
        LOGE("No textures found for font: %s", fontName.c_str());
        return nullptr;
    }

    LOGI("Creating SpriteFont from BMFont: %s (%zu textures, %zu chars)",
         fontName.c_str(), textures.size(), font->chars.size());

    // 创建 Texture2D 数组
    patch_handle_t textureArray = CreateTexture2DArray(textures);
    if (!textureArray) {
        LOGE("Failed to create Texture2D array");
        return nullptr;
    }

    LOGI("Successfully created Texture2D array");

    // 创建 Glyph 数组
    patch_handle_t glyphArray = CreateGlyphArray(font->chars, textures);
    if (!glyphArray) {
        LOGE("Failed to create Glyph array");
        return nullptr;
    }

    LOGI("Successfully created Glyph array");

    constexpr uint16_t defaultChar = '*';
    // 调用 SpriteFont 构造函数
    // SpriteFont(Texture2D[] textures, Glyph[] glyphs, int lineSpacing, float spacing, char? defaultCharacter)

    const float spacing = static_cast<float>(font->info.spacing[0] + font->info.spacing[1]) / 2.0f;

    patch_handle_t spriteFont = SpriteFont_ctor.InvokeConstructor(
        textureArray,
        glyphArray,
        font->common.lineHeight,
        spacing, // spacing
        defaultChar // default character (null)
    );

    if (spriteFont) {
        LOGI("SpriteFont created successfully: %s", fontName.c_str());
    } else {
        LOGE("Failed to create SpriteFont: %s", fontName.c_str());
    }

    return spriteFont;
}

bool FontManager::LoadSpriteFont_Hook(patch_handle_t instance, void **args, const patch_method_signature_t *sig_info,
                                      void *result) {
    const TEFKernel::PatchLib::Struct::String assetName(*static_cast<patch_handle_t *>(args[0]), false);
    const auto asset = assetName.ToString();

    LOGI("Load font asset: %s", asset.c_str());

    if (const auto it = g_fontNameMap.find(asset); it != g_fontNameMap.end()) {
        const std::string &fontPackName = it->second;

        LOGI("Replacing font: %s -> %s", asset.c_str(), fontPackName.c_str());

        if (font_pack && font_pack->hasFont(fontPackName)) {
            // 从 FontPack 创建 SpriteFont
            patch_handle_t spriteFont = CreateSpriteFontFromBMFont(fontPackName);

            if (spriteFont != nullptr) {
                // 将结果写入 result
                if (result)
                    *static_cast<patch_handle_t *>(result) = spriteFont;

                LOGI("Successfully replaced font: %s", asset.c_str());
                return true; // 返回 true 表示我们已经处理了，不要执行原始函数
            }
            LOGE("Failed to create SpriteFont from FontPack: %s", fontPackName.c_str());
        } else {
            LOGW("FontPack font not available: %s", fontPackName.c_str());
        }
    }

    return false;
}
