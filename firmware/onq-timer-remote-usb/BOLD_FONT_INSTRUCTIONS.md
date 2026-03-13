# How to Create Bold 96pt Timer Font

The current `montserrat_96.c` font was generated from **Montserrat-VariableFont_wght.ttf** (regular weight).

To make the timer font bold, you need to regenerate the font file using a bold version.

## Steps to Create Bold Font

### Option 1: Use Montserrat-Bold.ttf (Recommended)

1. **Download Montserrat-Bold.ttf**
   - Go to https://fonts.google.com/specimen/Montserrat
   - Click "Download family"
   - Extract the ZIP file
   - Find `Montserrat-Bold.ttf` in the `/static` folder

2. **Use LVGL Online Font Converter**
   - Visit: https://lvgl.io/tools/fontconverter
   - **Settings**:
     - **Name**: `montserrat_96`
     - **Size**: `96` px
     - **Bpp**: `4` (best quality)
     - **TTF/OTF font**: Upload `Montserrat-Bold.ttf`
     - **Range**: `0x30-0x3A` (digits 0-9 and colon `:` only)
     - **Format**: C array
   - Click "Convert"
   - Download the generated `.c` file

3. **Replace the Font File**
   - **BACKUP** the current `montserrat_96.c` file (rename to `montserrat_96_regular.c`)
   - Save the downloaded file as `montserrat_96.c` in the same directory
   - The header file `montserrat_96.h` does NOT need to be changed

4. **Compile and Upload**
   - Recompile the Arduino sketch
   - Upload to the ESP32-S3
   - The timer will now display in **bold**

### Option 2: Use Variable Font with Higher Weight

If you want more control over the weight:

1. Use the same `Montserrat-VariableFont_wght.ttf` you already have
2. In the LVGL Font Converter, look for a **weight** setting
3. Set weight to `700` (bold) or `800` (extra bold)
4. Follow steps 2-4 from Option 1

## Current Font Details

**File**: `montserrat_96.c`
**Source**: Montserrat-VariableFont_wght.ttf
**Weight**: Regular (400)
**Size**: 96px
**Characters**: 0-9 and `:` only (to save flash memory)

## After Regenerating

The bold font will:
- Be more prominent and easier to read
- Match the watch aesthetic better
- Still use the same ~50-100KB of flash memory (characters only, not full ASCII)

## No Code Changes Needed

Once you replace `montserrat_96.c` with the bold version:
- The code already references `montserrat_96`
- No other changes are required
- Just recompile and upload
