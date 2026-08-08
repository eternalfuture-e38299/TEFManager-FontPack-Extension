# TEFManager-FontPack-Extension ([English](README-en.md))

> TEFManager 字体包核心模块  
> 专用于 **Terraria 移动端（Android）** 的自定义字体包加载方案  
> 性能接近原版纹理字体，支持 RGBA（32bit PNG）

**重要限制**：Terraria 移动端仅支持 **UTF-16 字符集**，所有字体包内的字符必须属于 UTF-16 编码范围（U+0020 ~ U+FFFF）。

## 一、字体包结构

ZIP 包根目录必须包含：

pack_info.json

death_text/     ← 标题字体（如“你死了”）

mouse_text/     ← 内容字体（UI 正文）

combat_text/    ← 伤害数字

combat_crit/    ← 暴击数字

item_stack/     ← 物品堆叠数量 / Buff 栏数字

**规则**：
- 至少存在 **1 个**目录
- 可缺少任意 **4 个**目录
- 缺失的目录 → Terraria 使用原版字体

## 二、pack_info.json

```json
{
    "type": "FontPack",
    "name": "测试字体包",
    "author": "eternalfuture-e38299",
    "description": "这是一个自定义的字体包",
    "version": "1.0.0"
}
```

必填字段：`type`、`name`、`author`、`version`

## 三、BMFont 导出规范

### 核心参数
- 输出格式：**PNG**，位数 **32**（RGBA）
- 配置文件格式：**XML**
- 文件名：目录名本身（BMFont 会自动加 `.fnt` 后缀）

### 目录内示例
```txt
mouse_text/
├── mouse_text.fnt
└── mouse_text_0.png
```

### ⚠️ UTF-16 字符集限制（Terraria 移动端）
- BMFont 导出时，**只选择 UTF-16 范围内的字符**
- 排除超出 BMP（基本多语言平面）的字符，如部分 Emoji 和生僻汉字
- 建议字符范围：U+0020 ~ U+FFFF
- 推荐导出：3500 常用汉字 + 英文 + 数字 + 标点符号（Terraria 常用词）

## 四、推荐字号

| 槽位 | 推荐大小 | 说明 |
|------|---------|------|
| death_text | 48~52 | 大标题，可带描边 |
| mouse_text | 24~26 | UI 正文基准 |
| combat_text | 20~22 | 伤害跳字 |
| combat_crit | 22~24 | 暴击跳字，可更粗 |
| item_stack | 16~20 | 堆叠数字 |

## 五、打包流程

1. 创建目录结构
2. 用 BMFont 为每个目录生成字体文件（确保字符在 UTF-16 范围内）
3. 将 `.fnt` 和 `.png` 放入对应目录
4. 把 `pack_info.json` 和所有目录一起压缩为 **ZIP**

### 最小可用示例
```txt
pack_info.json
mouse_text/mouse_text.fnt
mouse_text/mouse_text_0.png
```

## 六、注意事项

- **仅适用于 Terraria 移动端（Android）**
- 字体必须用 BMFont 预烘焙，不能直接放 TTF
- 32bit PNG 才能支持 RGBA 彩色/半透明效果
- 字符集严格限制在 **UTF-16**，超出的字符在游戏中不会显示
- 打包时请直接选中文件/文件夹压缩，不要外层再套一层文件夹
