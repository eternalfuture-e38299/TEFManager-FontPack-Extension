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
#include "tefkernel-cpp-wrapper/tefkernel/patchlib/type.h"
#include "tefkernel-cpp-wrapper/tefkernel/patchlib/method.h"
#include "tefkernel-cpp-wrapper/tefkernel/patchlib/field.h"

#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// 字体图集贴图：按游戏原生 DynamicSpriteFont 的方式创建
//
// 原版字体走 UnityEngine.ImageConversion.LoadImage()，它创建的是
//   mipChain = true, filterMode = Bilinear, wrapMode = Clamp
// 的 Texture2D。之前这里走 terraria_texture2d_create()（mipChain=false +
// Point），游戏 UI 缩放时既糊又有锯齿。这里直接用 patchlib 调 Unity API，
// 和原生字体保持一致；拿不到 API 时再退回内核的通用创建函数。
// ---------------------------------------------------------------------------
namespace {
    struct UnityTexApi {
        patch_handle_t tex_cls = PATCH_NULL;
        patch_handle_t xna_tex_cls = PATCH_NULL;
        patch_handle_t ctor = PATCH_NULL;       // Texture2D(int,int,TextureFormat,bool)
        patch_handle_t get_writable = PATCH_NULL; // GetWritableImageData(int)
        patch_handle_t apply = PATCH_NULL;      // Apply()
        patch_handle_t set_filter = PATCH_NULL; // Texture.set_filterMode(int)
        patch_handle_t set_wrap = PATCH_NULL;   // Texture.set_wrapMode(int)
        patch_handle_t xna_ctor = PATCH_NULL;   // XNA Texture2D(UnityEngine.Texture2D)
        bool ready = false;
        bool tried = false;
    };

    UnityTexApi &unity_tex_api() {
        static UnityTexApi api;
        if (api.tried) return api;
        api.tried = true;

        api.tex_cls = patchlib_type_get_type("UnityEngine", "Texture2D");
        patch_handle_t base_tex = patchlib_type_get_type("UnityEngine", "Texture");
        api.xna_tex_cls = patchlib_type_get_type("Microsoft.Xna.Framework.Graphics", "Texture2D");
        if (patchlib_is_valid(api.tex_cls)) {
            api.ctor = patchlib_type_get_method_by_param_count(api.tex_cls, ".ctor", 4);
            api.get_writable = patchlib_type_get_method_by_param_count(api.tex_cls, "GetWritableImageData", 1);
            api.apply = patchlib_type_get_method_by_param_count(api.tex_cls, "Apply", 0);
        }
        if (patchlib_is_valid(base_tex)) {
            api.set_filter = patchlib_type_get_method_by_param_count(base_tex, "set_filterMode", 1);
            api.set_wrap = patchlib_type_get_method_by_param_count(base_tex, "set_wrapMode", 1);
        }
        if (patchlib_is_valid(api.xna_tex_cls)) {
            api.xna_ctor = patchlib_type_get_method_by_param_count(api.xna_tex_cls, ".ctor", 1);
        }
        api.ready = patchlib_is_valid(api.tex_cls) && patchlib_is_valid(api.xna_tex_cls) &&
                    patchlib_is_valid(api.ctor) && patchlib_is_valid(api.get_writable) &&
                    patchlib_is_valid(api.apply) && patchlib_is_valid(api.set_filter) &&
                    patchlib_is_valid(api.set_wrap) && patchlib_is_valid(api.xna_ctor);
        if (!api.ready) {
            LOGW("font texture: Unity Texture2D API incomplete, fallback to terraria_texture2d_create");
        }
        return api;
    }

    // 统一转成 RGBA32（BMFont 的 PNG 本来就是 RGBA，但防御 3/1 通道）
    std::vector<unsigned char> to_rgba(const TextureData &t) {
        const size_t px = static_cast<size_t>(t.width) * static_cast<size_t>(t.height);
        if (t.channels == 4 && t.data.size() >= px * 4) return t.data;
        std::vector<unsigned char> out(px * 4, 255);
        for (size_t i = 0; i < px; ++i) {
            if (t.channels == 3 && t.data.size() >= px * 3) {
                out[i*4+0] = t.data[i*3+0];
                out[i*4+1] = t.data[i*3+1];
                out[i*4+2] = t.data[i*3+2];
                out[i*4+3] = 255;
            } else if (t.channels == 1 && t.data.size() >= px) {
                const unsigned char a = t.data[i];
                out[i*4+0] = a; out[i*4+1] = a; out[i*4+2] = a; out[i*4+3] = a;
            }
        }
        return out;
    }
}

static patch_handle_t CreateTexture2D(const TextureData &texture) {
    UnityTexApi &api = unity_tex_api();
    if (!api.ready) {
        // 兜底：内核旧接口（Point + 无 mipmap）
        patch_handle_t texHandle = terraria_texture2d_create(
            texture.width, texture.height, TEXTURE_FORMAT_RGBA32,
            const_cast<void *>(static_cast<const void *>(texture.data.data())),
            texture.data.size());
        if (!texHandle) {
            LOGE("Failed to create texture: %dx%d, %d channels",
                 texture.width, texture.height, texture.channels);
        }
        return texHandle;
    }

    const auto rgba = to_rgba(texture);

    // Texture2D(int width, int height, TextureFormat format, bool mipChain)
    patch_handle_t unity = patchlib_type_new_instance(api.tex_cls);
    if (!patchlib_is_valid(unity)) {
        LOGE("font texture: failed to allocate UnityEngine.Texture2D");
        return PATCH_NULL;
    }
    const int fmt = static_cast<int>(TEXTURE_FORMAT_RGBA32);
    const bool mip_chain = true;
    reinterpret_cast<void (*)(void *, int, int, int, bool)>(
        patchlib_method_get_pointer(api.ctor))(unity, texture.width, texture.height, fmt, mip_chain);

    // 和 ImageConversion.LoadImage 一致：Bilinear(1) + Clamp(1)
    reinterpret_cast<void (*)(void *, int)>(
        patchlib_method_get_pointer(api.set_filter))(unity, 1);
    reinterpret_cast<void (*)(void *, int)>(
        patchlib_method_get_pointer(api.set_wrap))(unity, 1);

    void *pixels = reinterpret_cast<void *(*)(void *, int)>(
        patchlib_method_get_pointer(api.get_writable))(unity, 0);
    if (!pixels) {
        LOGE("font texture: GetWritableImageData returned NULL");
        return PATCH_NULL;
    }
    memcpy(pixels, rgba.data(), static_cast<size_t>(texture.width) * texture.height * 4);

    // Apply() 默认 updateMipmaps=true，会自动生成 mip 链
    reinterpret_cast<void (*)(void *)>(
        patchlib_method_get_pointer(api.apply))(unity);

    // 包成 XNA Texture2D
    patch_handle_t xna = patchlib_type_new_instance(api.xna_tex_cls);
    if (!patchlib_is_valid(xna)) {
        LOGE("font texture: failed to allocate XNA Texture2D");
        return PATCH_NULL;
    }
    reinterpret_cast<void (*)(void *, void *)>(
        patchlib_method_get_pointer(api.xna_ctor))(xna, unity);

    LOGD("Created font texture: %dx%d (mipChain=true, bilinear)",
         texture.width, texture.height);
    return xna;
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
        uint32_t Character; // offset 0x00
        int32_t BoundsInTexture_X; // offset 0x04
        int32_t BoundsInTexture_Y; // offset 0x08
        int32_t BoundsInTexture_Width; // offset 0x0C
        int32_t BoundsInTexture_Height; // offset 0x10
        int32_t Cropping_X; // offset 0x14
        int32_t Cropping_Y; // offset 0x18
        int32_t Cropping_Width; // offset 0x1C
        int32_t Cropping_Height; // offset 0x20
        float LeftSideBearing; // offset 0x24
        float RightSideBearing; // offset 0x28
        float Width; // offset 0x2C
        float WidthIncludingBearings; // offset 0x30
        uint8_t TexureIndex; // offset 0x34
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
        const auto &bmChar = chars[i];

        // ===== 检查字符是否超出 UTF-16 范围 =====
        // UTF-16 能表示的最大码点是 0x10FFFF
        // 但 BMP (基本多文种平面) 范围是 0x0000 - 0xFFFF
        // Terraria 的 char 类型是 16 位，只能表示 BMP 字符
        if (bmChar.id >= 0xFFFF) {
            LOGW("Skipping char id=0x%X (out of index table range)", bmChar.id);
            skippedCount++;
            continue;
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

// 从字体字符中挑选一个合适的默认字符
// 优先级：'□' (U+25A1) > '*' > '?' > null
static uint32_t PickDefaultCharacter(const std::vector<BMFontChar> &chars) {
    if (chars.empty()) return 0;

    bool hasBox = false, hasStar = false, hasQuestion = false;

    for (const auto &c : chars) {
        if (const auto id = static_cast<uint32_t>(c.id); id == 0x25A1) hasBox = true;
        else if (id == '*') hasStar = true;
        else if (id == '?') hasQuestion = true;
        if (hasBox && hasStar && hasQuestion) break;
    }

    if (hasBox)      return (0x25A1u << 16) | 1u;
    if (hasStar)     return ('*' << 16) | 1u;
    if (hasQuestion) return ('?' << 16) | 1u;
    return 0;
}

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

    // 调用 SpriteFont 构造函数
    // SpriteFont(Texture2D[] textures, Glyph[] glyphs, int lineSpacing, float spacing, char? defaultCharacter)

    const float spacing = static_cast<float>(font->info.spacing[0] + font->info.spacing[1]) / 2.0f;

    patch_handle_t spriteFont = SpriteFont.NewInstance(); /*SpriteFont_ctor.InvokeConstructor(
        textureArray,
        glyphArray,
        font->common.lineHeight,
        spacing, // spacing
        defaultChar // default character (null)
    );*/

    auto ctor = reinterpret_cast<void(*)(void *, void *, void *, int, float, uint32_t)>(SpriteFont_ctor.GetPointer());


    const uint32_t  defaultCharNullable = PickDefaultCharacter(font->chars);
    ctor(spriteFont,
         textureArray,
         glyphArray,
         font->common.lineHeight,
         spacing, // spacing
         defaultCharNullable // default character (null)
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
