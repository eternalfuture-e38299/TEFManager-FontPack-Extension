# TEFManager-FontPack-Extension

> Core module of TEFManager font packs  
> Dedicated to **Terraria Mobile (Android)** custom font pack loading solution  
> Performance close to original texture fonts, supports RGBA (32bit PNG)

**Important Restriction**: Terraria Mobile only supports the **UTF-16 character set**. All characters in the font pack must fall within the UTF-16 encoding range (U+0020 ~ U+FFFF).

## 1. Font Pack Structure

The root directory of the ZIP package must contain:

pack_info.json

death_text/     ← Title font (e.g., "You died")

mouse_text/     ← Content font (UI text)

combat_text/    ← Damage numbers

combat_crit/    ← Critical hit numbers

item_stack/     ← Item stack count / Buff bar numbers


**Rules**:
- At least **1** directory must exist
- Any **4** directories can be omitted
- Missing directories → Terraria uses the original font

## 2. pack_info.json

```json
{

"type": "FontPack",
"name": "Test Font Pack",
"author": "eternalfuture-e38299",
"description": "This is a custom font pack",
"version": "1.0.0"
}
```

Required fields: `type`, `name`, `author`, `version`

## 3. BMFont Export Specifications

### Core Parameters
- Output format: **PNG**, bit depth **32** (RGBA)
- Configuration file format: **XML**
- Filename: The directory name itself (BMFont will automatically add the `.fnt` suffix)

### Directory Example

```txt
mouse_text/
├── mouse_text.fnt
└── mouse_text_0.png
```

### ⚠️ UTF-16 Character Set Restriction (Terraria Mobile)
- When exporting with BMFont, **only select characters within the UTF-16 range**
- Exclude characters beyond the BMP (Basic Multilingual Plane), such as some emojis and rare Chinese characters
- Recommended character range: U+0020 ~ U+FFFF
- Recommended export: 3500 common Chinese characters + English letters + numbers + punctuation marks (common Terraria terms)

## 4. Recommended Font Sizes

| Slot | Recommended Size | Description |
|------|-----------------|-------------|
| death_text | 48~52 | Large title, can have outline |
| mouse_text | 24~26 | UI text baseline |
| combat_text | 20~22 | Damage numbers |
| combat_crit | 22~24 | Critical hit numbers, can be bolder |
| item_stack | 16~20 | Stack count numbers |

## 5. Packaging Process

1. Create the directory structure
2. Use BMFont to generate font files for each directory (ensure characters are within UTF-16 range)
3. Place the `.fnt` and `.png` files into their corresponding directories
4. Compress `pack_info.json` and all directories together into a **ZIP** file

### Minimum Working Example

txt

pack_info.json

mouse_text/mouse_text.fnt

mouse_text/mouse_text_0.png


## 6. Notes

- **Only applicable to Terraria Mobile (Android)**
- Fonts must be pre-baked with BMFont, cannot directly use TTF files
- 32bit PNG is required to support RGBA color/transparency effects
- Character set is strictly limited to **UTF-16**; characters outside this range will not display in the game
- When packaging, select the files/folders directly for compression, do not nest them inside an extra folder layer
