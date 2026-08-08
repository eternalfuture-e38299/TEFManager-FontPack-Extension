/*******************************************************************************
 * fontpack_extension - core
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

#include "tefkernel-cpp-wrapper/tefkernel/module/module_core.h"
#include "tefkernel-cpp-wrapper/patchlib/property.hpp"
#include "FontManager.hpp"

#include <filesystem>
#include <fstream>
#include <optional>

static constexpr module_info_t g_module_info = {
    .pkg_id = "eternal.future.fontpackextension", // 唯一包名
    .name = "FontPack Extension", // 插件名称
    .author = "eternalfuture-e38299", // 作者
    .version = "1.0.0", // 版本
    .version_code = 1, // 版本代码
    .api_version = 1, // API版本
    .plugin_dependencies_sizes = 0, // 依赖数量（无依赖设为0）
    .plugin_dependencies = nullptr, // 依赖列表（无依赖设为NULL）
};

static std::optional<std::filesystem::path> GetSelectedFontPack(const std::filesystem::path& privateDir) {
    const auto configPath = privateDir / "selected.json";

    // 检查配置文件是否存在
    if (!std::filesystem::exists(configPath)) {
        return std::nullopt;
    }

    // 打开配置文件
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        return std::nullopt;
    }

    // 读取内容
    std::string content;
    std::getline(configFile, content);
    configFile.close();

    // 去除首尾空白
    const auto start = content.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return std::nullopt; // 只有空白字符
    }

    const auto end = content.find_last_not_of(" \t\n\r\f\v");
    content = content.substr(start, end - start + 1);

    // 检查是否为 null 或空
    if (content.empty() || content == "null") {
        return std::nullopt;
    }

    // 去除可能的引号（如果是从 JSON 来的）
    if (content.size() >= 2 && content.front() == '"' && content.back() == '"') {
        content = content.substr(1, content.size() - 2);
    }

    return privateDir / "font_packs" / content;
}

static bool init_module(module_entry_t *entry) {
    (void) entry; // 消除未使用参数警告

    const auto selectedPath = GetSelectedFontPack(entry->private_dir);
    if (!selectedPath.has_value()) return false;

    FontManager::font_pack = std::make_unique<FontPack>(selectedPath.value());

    const TEFKernel::PatchLib::Type SpriteFont("Microsoft.Xna.Framework.Graphics", "SpriteFont");
    const TEFKernel::PatchLib::Type Rectangle("Microsoft.Xna.Framework", "Rectangle");

    FontManager::Glyph = SpriteFont.GetInnerType("Glyph");

    FontManager::SpriteFont_ctor = SpriteFont.GetMethod(".ctor", 5);
    FontManager::Rectangle_ctor = Rectangle.GetMethod(".ctor", 4);

    const TEFKernel::PatchLib::Type ContentManager("Microsoft.Xna.Framework.Content", "ContentManager");
    const auto LoadSpriteFont = ContentManager.GetMethod("LoadSpriteFont", 1);

    patchlib_install_prepost_hook(LoadSpriteFont.GetHandle(), FontManager::LoadSpriteFont_Hook, nullptr);

    return true;
}

static bool cleanup_module(module_entry_t *entry) {
    (void) entry; // 消除未使用参数警告

    return true; // 返回true表示清理成功
}

static void hot_reload(module_entry_t *entry) {
    (void) entry; // 消除未使用参数警告
}

static const module_info_t *get_info() {
    return &g_module_info;
}

static constexpr module_ops_t g_module_ops = {
    .init_module = init_module,
    .cleanup_module = cleanup_module,
    .hot_reload = hot_reload,
    .get_info = get_info,
};

API_EXPORT const module_ops_t * API_CALL module_create(void) {
    return &g_module_ops;
}