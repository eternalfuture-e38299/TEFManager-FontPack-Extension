/*******************************************************************************
 * fontpack_extension - FontPack
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

#include <filesystem>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// 前向声明
struct BMFont;
struct BMFontChar;

// 纹理数据
struct TextureData {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<unsigned char> data;
};

// 字体数据
struct FontData {
    std::string name;
    std::shared_ptr<BMFont> font;
    std::unordered_map<int, TextureData> textures; // page id -> 纹理数据
    std::atomic<bool> loadingComplete{false};
    std::mutex mutex;
    std::condition_variable cv;
};

class FontPack {
public:
    FontPack() = default;
    explicit FontPack(std::filesystem::path path);
    ~FontPack();

    // 禁止拷贝
    FontPack(const FontPack&) = delete;
    FontPack& operator=(const FontPack&) = delete;

    // 检查某个字体是否存在
    bool hasFont(const std::string& fontName) const;

    // 获取字体配置
    std::shared_ptr<BMFont> getFont(const std::string& fontName) const;

    // 获取指定page的纹理数据（会阻塞直到解码完成）
    const TextureData* getTexture(const std::string& fontName, int pageId) const;

    // 获取所有纹理数据（按page id排序，会阻塞直到解码完成）
    std::vector<TextureData> getTextures(const std::string& fontName) const;

    // 获取字体中某个字符的信息
    const BMFontChar* getChar(const std::string& fontName, int charId) const;

    // 等待指定字体加载完成
    void waitForFont(const std::string& fontName) const;

    // 检查字体是否已加载完成
    bool isFontReady(const std::string& fontName) const;

private:
    // 初始化：解压并解析所有字体
    void initialize();

    // 解压zip文件
    bool extractZip(const std::filesystem::path& path);

    // 解码纹理的线程函数
    void decodeTexture(const std::string& fontName, int pageId, const std::vector<unsigned char> &pngData) const;

    // 获取字体数据
    std::shared_ptr<FontData> getFontData(const std::string& fontName) const;

    std::filesystem::path m_zipPath;
    std::unordered_map<std::string, std::shared_ptr<FontData>> m_fonts;
    mutable std::mutex m_fontsMutex;
    std::atomic<bool> m_initialized{false};
    std::vector<std::thread> m_decodeThreads;
};

// BMFont结构体定义
struct BMFontInfo {
    std::string face;
    int size = 0;
    int bold = 0;
    int italic = 0;
    std::string charset;
    int unicode = 0;
    int stretchH = 100;
    int smooth = 1;
    int aa = 4;
    int padding[4] = {0, 0, 0, 0};
    int spacing[2] = {1, 1};
    int outline = 0;
};

struct BMFontCommon {
    int lineHeight = 0;
    int base = 0;
    int scaleW = 0;
    int scaleH = 0;
    int pages = 0;
    int packed = 0;
    int alphaChnl = 0;
    int redChnl = 0;
    int greenChnl = 0;
    int blueChnl = 0;
};

struct BMFontPage {
    int id = 0;
    std::string file;
};

struct BMFontChar {
    int id = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int xoffset = 0;
    int yoffset = 0;
    int xadvance = 0;
    int page = 0;
    int chnl = 0;
};

struct BMFontKerning {
    int first = 0;
    int second = 0;
    int amount = 0;
};

struct BMFont {
    BMFontInfo info;
    BMFontCommon common;
    std::vector<BMFontPage> pages;
    std::vector<BMFontChar> chars;
    std::vector<BMFontKerning> kernings;
    std::unordered_map<int, BMFontChar> charMap;

    void buildCharMap() {
        charMap.clear();
        for (const auto& c : chars) {
            charMap[c.id] = c;
        }
    }

    const BMFontChar* getChar(const int id) const {
        const auto it = charMap.find(id);
        return (it != charMap.end()) ? &it->second : nullptr;
    }
};