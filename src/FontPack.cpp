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

#include "FontPack.hpp"
#include "Log.hpp"

#include "pugixml.hpp"
#include "lib/miniz.h"
#include "lib/stb_image.h"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <ranges>
#include <utility>


FontPack::FontPack(std::filesystem::path  path) : m_zipPath(std::move(path)) {
    LOGI("FontPack constructor started, path: %s", m_zipPath.string().c_str());
    initialize();
    LOGI("FontPack constructor completed");
}

FontPack::~FontPack() {
    LOGD("FontPack destructor started");
    for (auto& thread : m_decodeThreads) {
        if (thread.joinable()) {
            LOGD("Waiting for decode thread to finish");
            thread.join();
        }
    }
    LOGD("FontPack destructor completed");
}

void FontPack::initialize() {
    if (m_initialized) {
        LOGD("FontPack already initialized");
        return;
    }

    LOGD("Extracting zip file: %s", m_zipPath.string().c_str());
    if (!extractZip(m_zipPath)) {
        LOGE("Failed to extract zip file: %s", m_zipPath.string().c_str());
        throw std::runtime_error("Failed to extract zip file: " + m_zipPath.string());
    }

    m_initialized = true;
    LOGI("FontPack initialized successfully");
}

bool FontPack::extractZip(const std::filesystem::path& path) {
    LOGI("Starting to extract font package: %s", path.string().c_str());

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, path.string().c_str(), 0)) {
        LOGE("Failed to initialize zip file: %s", path.string().c_str());
        return false;
    }

    const auto fileCount = mz_zip_reader_get_num_files(&zip);
    LOGD("Zip file contains %d files", fileCount);

    std::unordered_map<std::string, std::vector<unsigned char>> files;

    for (int i = 0; i < fileCount; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            LOGW("Failed to get file status, index: %d", i);
            continue;
        }

        std::string filename = stat.m_filename;
        if (filename.back() == '/') {
            LOGD("Skipping directory: %s", filename.c_str());
            continue;
        }

        std::vector<unsigned char> content(stat.m_uncomp_size);
        if (!mz_zip_reader_extract_file_to_mem(&zip, stat.m_filename, content.data(), content.size(), 0)) {
            LOGW("Failed to extract file: %s", filename.c_str());
            continue;
        }

        files[filename] = std::move(content);
        LOGD("Extracted file: %s (%zu bytes)", filename.c_str(), content.size());
    }

    mz_zip_reader_end(&zip);
    LOGD("Extracted %zu files total", files.size());

    // Identify font directories
    std::unordered_map<std::string, std::vector<std::string>> fontFiles;

    for (const auto &filename: files | std::views::keys) {
        size_t pos = filename.find('.');
        if (pos == std::string::npos) continue;

        if (std::string ext = filename.substr(pos); ext != ".fnt") continue;

        size_t sepPos = filename.find('/');
        if (sepPos == std::string::npos) continue;

        if (std::string dirName = filename.substr(0, sepPos); dirName == "mouse_text" || dirName == "death_text" ||
                                                              dirName == "combat_crit" || dirName == "combat_text" ||
                                                              dirName == "item_stack") {
            fontFiles[dirName].push_back(filename);
            LOGD("Found font file: %s (directory: %s)", filename.c_str(), dirName.c_str());
        }
    }

    LOGI("Found %zu font directories", fontFiles.size());

    // Create font data objects
    for (auto& [dirName, fntFiles] : fontFiles) {
        LOGI("Loading font: %s", dirName.c_str());

        auto fontData = std::make_shared<FontData>();
        fontData->name = dirName;

        // ===== 先注册到 m_fonts，让解码线程能找到 =====
        {
            std::lock_guard lock(m_fontsMutex);
            m_fonts[dirName] = fontData;
        }
        LOGD("Font data registered: %s", dirName.c_str());

        for (const auto& fntFile : fntFiles) {
            if (auto it = files.find(fntFile); it != files.end()) {
                LOGD("Parsing font file: %s", fntFile.c_str());

                std::string xmlContent(it->second.begin(), it->second.end());
                fontData->font = std::make_shared<BMFont>();

                pugi::xml_document doc;
                if (pugi::xml_parse_result result = doc.load_string(xmlContent.c_str()); !result) {
                    LOGE("Failed to parse XML %s: %s", fntFile.c_str(), result.description());
                    continue;
                }

                pugi::xml_node fontNode = doc.child("font");
                if (!fontNode) {
                    LOGE("Font node not found: %s", fntFile.c_str());
                    continue;
                }

                // Parse info
                if (pugi::xml_node infoNode = fontNode.child("info")) {
                    fontData->font->info.face = infoNode.attribute("face").as_string();
                    fontData->font->info.size = infoNode.attribute("size").as_int();
                    fontData->font->info.bold = infoNode.attribute("bold").as_int();
                    fontData->font->info.italic = infoNode.attribute("italic").as_int();
                    fontData->font->info.charset = infoNode.attribute("charset").as_string();
                    fontData->font->info.unicode = infoNode.attribute("unicode").as_int();
                    fontData->font->info.stretchH = infoNode.attribute("stretchH").as_int(100);
                    fontData->font->info.smooth = infoNode.attribute("smooth").as_int(1);
                    fontData->font->info.aa = infoNode.attribute("aa").as_int(4);
                    fontData->font->info.outline = infoNode.attribute("outline").as_int();

                    LOGD("Font info - Name: %s, Size: %d, Bold: %d",
                         fontData->font->info.face.c_str(),
                         fontData->font->info.size,
                         fontData->font->info.bold);

                    // Parse padding
                    if (std::string paddingStr = infoNode.attribute("padding").as_string(); !paddingStr.empty()) {
                        std::vector<std::string> pads;
                        std::stringstream ss(paddingStr);
                        std::string item;
                        while (std::getline(ss, item, ',')) {
                            pads.push_back(item);
                        }
                        for (size_t i = 0; i < pads.size() && i < 4; i++) {
                            fontData->font->info.padding[i] = std::stoi(pads[i]);
                        }
                    }

                    // Parse spacing
                    if (std::string spacingStr = infoNode.attribute("spacing").as_string(); !spacingStr.empty()) {
                        std::vector<std::string> spacings;
                        std::stringstream ss(spacingStr);
                        std::string item;
                        while (std::getline(ss, item, ',')) {
                            spacings.push_back(item);
                        }
                        for (size_t i = 0; i < spacings.size() && i < 2; i++) {
                            fontData->font->info.spacing[i] = std::stoi(spacings[i]);
                        }
                    }
                }

                // Parse common
                if (pugi::xml_node commonNode = fontNode.child("common")) {
                    fontData->font->common.lineHeight = commonNode.attribute("lineHeight").as_int();
                    fontData->font->common.base = commonNode.attribute("base").as_int();
                    fontData->font->common.scaleW = commonNode.attribute("scaleW").as_int();
                    fontData->font->common.scaleH = commonNode.attribute("scaleH").as_int();
                    fontData->font->common.pages = commonNode.attribute("pages").as_int();
                    fontData->font->common.packed = commonNode.attribute("packed").as_int();
                    fontData->font->common.alphaChnl = commonNode.attribute("alphaChnl").as_int();
                    fontData->font->common.redChnl = commonNode.attribute("redChnl").as_int();
                    fontData->font->common.greenChnl = commonNode.attribute("greenChnl").as_int();
                    fontData->font->common.blueChnl = commonNode.attribute("blueChnl").as_int();

                    LOGD("Font common - LineHeight: %d, Base: %d, Size: %dx%d, Pages: %d",
                         fontData->font->common.lineHeight,
                         fontData->font->common.base,
                         fontData->font->common.scaleW,
                         fontData->font->common.scaleH,
                         fontData->font->common.pages);
                }

                // Parse pages
                if (pugi::xml_node pagesNode = fontNode.child("pages")) {
                    for (pugi::xml_node pageNode : pagesNode.children("page")) {
                        BMFontPage page;
                        page.id = pageNode.attribute("id").as_int();
                        page.file = pageNode.attribute("file").as_string();
                        fontData->font->pages.push_back(page);
                        LOGD("Page %d: %s", page.id, page.file.c_str());
                    }
                }

                // Parse chars
                if (pugi::xml_node charsNode = fontNode.child("chars")) {
                    int charCount = 0;
                    for (pugi::xml_node charNode : charsNode.children("char")) {
                        BMFontChar ch;
                        ch.id = charNode.attribute("id").as_int();
                        ch.x = charNode.attribute("x").as_int();
                        ch.y = charNode.attribute("y").as_int();
                        ch.width = charNode.attribute("width").as_int();
                        ch.height = charNode.attribute("height").as_int();
                        ch.xoffset = charNode.attribute("xoffset").as_int();
                        ch.yoffset = charNode.attribute("yoffset").as_int();
                        ch.xadvance = charNode.attribute("xadvance").as_int();
                        ch.page = charNode.attribute("page").as_int();
                        ch.chnl = charNode.attribute("chnl").as_int();
                        fontData->font->chars.push_back(ch);
                        charCount++;
                    }
                    LOGD("Loaded %d characters", charCount);
                }

                // Parse kernings
                if (pugi::xml_node kerningsNode = fontNode.child("kernings")) {
                    int kerningCount = 0;
                    for (pugi::xml_node kerningNode : kerningsNode.children("kerning")) {
                        BMFontKerning kerning;
                        kerning.first = kerningNode.attribute("first").as_int();
                        kerning.second = kerningNode.attribute("second").as_int();
                        kerning.amount = kerningNode.attribute("amount").as_int();
                        fontData->font->kernings.push_back(kerning);
                        kerningCount++;
                    }
                    LOGD("Loaded %d kernings", kerningCount);
                }

                fontData->font->buildCharMap();
                break;
            }
        }

        if (fontData->font) {
            // Collect all textures to load
            std::unordered_map<int, std::vector<unsigned char>> pngData;

            for (const auto&[id, file] : fontData->font->pages) {
                std::string pngFile = dirName;
                pngFile += '/';
                pngFile += file;
                if (auto it = files.find(pngFile); it != files.end()) {
                    pngData[id] = std::move(it->second);
                    LOGD("Found texture file: %s (ID: %d, Size: %zu bytes)",
                         pngFile.c_str(), id, it->second.size());
                } else {
                    LOGW("Texture file not found: %s", pngFile.c_str());
                }
            }

            LOGI("Font %s needs to load %zu textures", dirName.c_str(), pngData.size());

            // ===== 启动解码线程（现在 m_fonts 中已经有 fontData 了） =====
            for (auto& [pageId, data] : pngData) {
                LOGD("Starting decode thread: %s page %d", dirName.c_str(), pageId);
                m_decodeThreads.emplace_back(&FontPack::decodeTexture, this, dirName, pageId, std::move(data));
            }

            // 注意：不再需要在这里注册，已经在前面注册了
            // m_fonts[dirName] = fontData;  // ← 移到这里之前
            LOGI("Font %s loaded, waiting for texture decoding", dirName.c_str());
        } else {
            // 如果加载失败，从 m_fonts 中移除
            std::lock_guard lock(m_fontsMutex);
            m_fonts.erase(dirName);
            LOGE("Failed to load font: %s", dirName.c_str());
        }
    }

    LOGI("FontPack extraction completed, loaded %zu fonts", m_fonts.size());
    return true;
}

void FontPack::decodeTexture(const std::string& fontName, const int pageId, const std::vector<unsigned char> &pngData) const {
    LOGD("Starting texture decode: %s page %d (Data size: %zu bytes)",
         fontName.c_str(), pageId, pngData.size());

    const std::shared_ptr<FontData> fontData = getFontData(fontName);
    if (!fontData) {
        LOGE("Failed to get font data: %s", fontName.c_str());
        return;
    }

    stbi_set_flip_vertically_on_load(true);

    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(
        pngData.data(),
        static_cast<int>(pngData.size()),
        &width, &height, &channels,
        4
    );

    if (data) {
        TextureData texture;
        texture.width = width;
        texture.height = height;
        texture.channels = channels;
        texture.data.resize(width * height * channels);
        memcpy(texture.data.data(), data, texture.data.size());

        stbi_image_free(data);

        {
            std::lock_guard lock(fontData->mutex);
            fontData->textures[pageId] = std::move(texture);
        }

        LOGI("Texture decode successful: %s page %d (%dx%d, %d channels)",
             fontName.c_str(), pageId, width, height, channels);
    } else {
        LOGE("Texture decode failed: %s page %d (Error: %s)",
             fontName.c_str(), pageId, stbi_failure_reason());
    }

    // Check if all textures are loaded
    bool allLoaded = true;
    {
        std::lock_guard lock(fontData->mutex);
        for (const auto&[id, file] : fontData->font->pages) {
            if (!fontData->textures.contains(id)) {
                allLoaded = false;
                break;
            }
        }
    }

    if (allLoaded) {
        fontData->loadingComplete = true;
        fontData->cv.notify_all();
        LOGI("Font %s all textures loaded", fontName.c_str());
    }
}

bool FontPack::hasFont(const std::string& fontName) const {
    std::lock_guard lock(m_fontsMutex);
    const bool exists = m_fonts.contains(fontName);
    LOGD("Checking font existence: %s -> %s", fontName.c_str(), exists ? "Yes" : "No");
    return exists;
}

std::shared_ptr<BMFont> FontPack::getFont(const std::string& fontName) const {
    LOGD("Getting font: %s", fontName.c_str());
    const auto fontData = getFontData(fontName);
    auto result = fontData ? fontData->font : nullptr;
    if (result) {
        LOGD("Font retrieved successfully: %s", fontName.c_str());
    } else {
        LOGW("Font not found: %s", fontName.c_str());
    }
    return result;
}

const TextureData* FontPack::getTexture(const std::string& fontName, const int pageId) const {
    LOGD("Getting texture: %s page %d", fontName.c_str(), pageId);

    const std::shared_ptr<FontData> fontData = getFontData(fontName);
    if (!fontData) {
        LOGE("Failed to get font data: %s", fontName.c_str());
        return nullptr;
    }

    std::unique_lock lock(fontData->mutex);
    LOGD("Waiting for texture: %s page %d", fontName.c_str(), pageId);

    fontData->cv.wait(lock, [&] {
        const auto it = fontData->textures.find(pageId);
        return it != fontData->textures.end();
    });

    const auto it = fontData->textures.find(pageId);
    if (it != fontData->textures.end()) {
        LOGD("Texture retrieved successfully: %s page %d", fontName.c_str(), pageId);
        return &it->second;
    }
    LOGE("Failed to get texture: %s page %d", fontName.c_str(), pageId);
    return nullptr;
}

std::vector<TextureData> FontPack::getTextures(const std::string& fontName) const {
    LOGD("Getting all textures for font: %s", fontName.c_str());

    std::vector<TextureData> result;
    const auto fontData = getFontData(fontName);
    if (!fontData) {
        LOGE("Failed to get font data: %s", fontName.c_str());
        return result;
    }

    waitForFont(fontName);

    std::lock_guard lock(fontData->mutex);

    std::vector<int> pageIds;
    for (const auto &id: fontData->textures | std::views::keys) {
        pageIds.push_back(id);
    }
    std::ranges::sort(pageIds);

    for (int id : pageIds) {
        result.push_back(fontData->textures[id]);
        LOGD("Added texture page %d to result", id);
    }

    LOGD("Retrieved %zu textures for font: %s", result.size(), fontName.c_str());
    return result;
}

const BMFontChar* FontPack::getChar(const std::string& fontName, const int charId) const {
    LOGD("Getting character: %s char %d", fontName.c_str(), charId);
    const auto font = getFont(fontName);
    const auto result = font ? font->getChar(charId) : nullptr;
    if (result) {
        LOGD("Character retrieved successfully: %s char %d", fontName.c_str(), charId);
    } else {
        LOGW("Character not found: %s char %d", fontName.c_str(), charId);
    }
    return result;
}

void FontPack::waitForFont(const std::string& fontName) const {
    LOGD("Waiting for font: %s", fontName.c_str());

    const auto fontData = getFontData(fontName);
    if (!fontData) {
        LOGE("Wait for font failed - font not found: %s", fontName.c_str());
        return;
    }

    std::unique_lock lock(fontData->mutex);
    fontData->cv.wait(lock, [&]() {
        return fontData->loadingComplete.load();
    });

    LOGI("Font ready: %s", fontName.c_str());
}

bool FontPack::isFontReady(const std::string& fontName) const {
    const auto fontData = getFontData(fontName);
    const bool ready = fontData ? fontData->loadingComplete.load() : false;
    LOGD("Font ready status: %s -> %s", fontName.c_str(), ready ? "Ready" : "Not ready");
    return ready;
}

std::shared_ptr<FontData> FontPack::getFontData(const std::string& fontName) const {
    std::lock_guard lock(m_fontsMutex);
    const auto it = m_fonts.find(fontName);
    auto result = it != m_fonts.end() ? it->second : nullptr;
    if (result) {
        LOGD("Font data found: %s", fontName.c_str());
    } else {
        LOGD("Font data not found: %s", fontName.c_str());
    }
    return result;
}