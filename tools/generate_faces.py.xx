#!/usr/bin/env python3
"""
Converts assets/faces/mochi_*.zip (each a zip of numbered PNG frames, one
"idle mood" animation) into a generated C++ source file of PROGMEM 1-bit
bitmap arrays, plus a small header with extern declarations.

Why this exists instead of committing the generated C++ directly: PNG data
is already compressed, but a text C array of the same pixel data is much
bigger (each byte becomes "0xNN, " - roughly 5-6x larger) and diffs
terribly in git. So the repo keeps the small zips under assets/faces/, and
this script regenerates the actual firmware source every build - either
automatically (platformio.ini's `extra_scripts = pre:` hook, for local
`pio run`) or as an explicit CI step (see .github/workflows/build.yml).
Output lands in include/generated/ and src/generated/, which are
.gitignore'd - they're build artifacts, not source.

Usage:
    python3 tools/generate_faces.py            # run standalone
    (or invoked automatically by PlatformIO as a pre: extra_script)

Requires: Pillow (`pip install pillow`).
"""

import os
import re
import sys
import zipfile
import io

# ---------------------------------------------------------------------------
# Detect whether we're running as a PlatformIO extra_script (SCons injects an
# `env` global and an `Import` builtin) or as a plain standalone script.
# ---------------------------------------------------------------------------
try:
    Import("env")  # noqa: F821 - only defined under PlatformIO/SCons
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
    _PIO_MODE = True
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    _PIO_MODE = False

try:
    from PIL import Image
except ImportError:
    if _PIO_MODE:
        # PlatformIO runs extra scripts inside its own Python environment,
        # which usually doesn't have Pillow - install it there on demand.
        env.Execute("$PYTHONEXE -m pip install pillow")  # noqa: F821
        from PIL import Image
    else:
        print("[generate_faces] ERROR: Pillow is required (`pip install pillow`).",
              file=sys.stderr)
        sys.exit(1)

ASSETS_DIR = os.path.join(PROJECT_DIR, "assets", "faces")
OUT_HEADER_DIR = os.path.join(PROJECT_DIR, "include", "generated")
OUT_SOURCE_DIR = os.path.join(PROJECT_DIR, "src", "generated")
OUT_HEADER = os.path.join(OUT_HEADER_DIR, "face_frames.h")
OUT_SOURCE = os.path.join(OUT_SOURCE_DIR, "face_frames.cpp")

FRAME_W = 98
FRAME_H = 64
ROW_BYTES = (FRAME_W + 7) // 8

# The idle blink is one real "eyes closed" frame lifted from the mood loops
# themselves (found by scanning every set for near-rest frames whose eyes
# are shut): frame 7 of set 8 (mochi_8) is a clean one-frame blink sitting
# right between the neutral pose and the heart-eyes moment. Showing it for
# ~130ms between two neutral frames reads as a natural snappy blink, unlike
# the long eyes-closed segment in set 0, which is a ~3s "content" moment.
BLINK_SET = 8
BLINK_SOURCE_FRAME = 7  # 1-indexed source frame number (007_mochi_8.png)


def log(msg):
    print(f"[generate_faces] {msg}")


def pack_1bit(img):
    """Pack a PIL '1' mode image into MSB-first, byte-per-row-boundary bytes
    - the format Adafruit_GFX::drawBitmap() expects. A set bit = lit pixel.
    """
    w, h = img.size
    row_bytes = (w + 7) // 8
    out = bytearray(row_bytes * h)
    px = img.load()
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            if px[x, y]:
                out[base + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def load_and_pack(png_bytes):
    im = Image.open(io.BytesIO(png_bytes)).convert("L")
    if im.size != (FRAME_W, FRAME_H):
        im = im.resize((FRAME_W, FRAME_H))
    # Default Pillow dithering (Floyd-Steinberg) when going L -> 1 keeps the
    # source art's soft gradients/shading legible on a 1-bit OLED, instead of
    # a harsh flat 50% threshold.
    im1 = im.convert("1")
    return pack_1bit(im1)


def frame_sort_key(name):
    m = re.match(r"(\d+)_", name)
    return int(m.group(1)) if m else 0


def main():
    if not os.path.isdir(ASSETS_DIR):
        log(f"no {ASSETS_DIR} found - skipping (nothing to generate)")
        return

    zips = sorted(
        f for f in os.listdir(ASSETS_DIR) if re.match(r"mochi_\d+\.zip$", f)
    )
    if not zips:
        log(f"no mochi_*.zip files in {ASSETS_DIR} - skipping")
        return

    # Skip regenerating when the outputs are already newer than every zip and
    # this script: rewriting the ~9MB generated .cpp on every build would
    # force PlatformIO to recompile it each time for no reason.
    if "--force" not in sys.argv and os.path.exists(OUT_SOURCE) and os.path.exists(OUT_HEADER):
        newest_input = max(
            [os.path.getmtime(os.path.join(ASSETS_DIR, z)) for z in zips]
            + [os.path.getmtime(os.path.join(PROJECT_DIR, "tools", "generate_faces.py"))]
        )
        if min(os.path.getmtime(OUT_SOURCE), os.path.getmtime(OUT_HEADER)) >= newest_input:
            log("generated sources are up to date - skipping (use --force to rebuild)")
            return

    os.makedirs(OUT_HEADER_DIR, exist_ok=True)
    os.makedirs(OUT_SOURCE_DIR, exist_ok=True)

    anim_frame_counts = []
    cpp_chunks = []
    table_chunks = []

    for anim_index, zip_name in enumerate(zips):
        m = re.match(r"mochi_(\d+)\.zip$", zip_name)
        set_index = int(m.group(1))
        zpath = os.path.join(ASSETS_DIR, zip_name)
        with zipfile.ZipFile(zpath) as zf:
            names = sorted(
                (n for n in zf.namelist() if n.lower().endswith(".png")),
                key=frame_sort_key,
            )
            log(f"{zip_name}: {len(names)} frames")
            frame_count = len(names)
            anim_frame_counts.append(frame_count)

            frame_ptr_names = []
            for frame_pos, name in enumerate(names):
                data = pack_1bit_wrapper(zf.read(name))
                sym = f"faceSet{set_index}Frame{frame_pos}"
                frame_ptr_names.append(sym)
                cpp_chunks.append(format_array(sym, data))

            table_sym = f"faceSet{set_index}Frames"
            table_chunks.append(format_pointer_table(table_sym, frame_ptr_names))

    # ---- write src/generated/face_frames.cpp -----------------------------
    with open(OUT_SOURCE, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_faces.py - do not edit by hand.\n")
        f.write('#include "generated/face_frames.h"\n\n')
        f.write("\n".join(cpp_chunks))
        f.write("\n\n")
        f.write("\n".join(table_chunks))
        f.write("\n\n")

        f.write(f"const uint16_t faceAnimFrameCount[FACE_ANIM_COUNT] PROGMEM = {{\n")
        f.write("    " + ", ".join(str(c) for c in anim_frame_counts) + "\n};\n\n")

        f.write(
            "const unsigned char *const *const faceAnimFrames[FACE_ANIM_COUNT] = {\n"
        )
        f.write(
            "    " + ", ".join(f"faceSet{i}Frames" for i in range(len(zips))) + "\n"
        )
        f.write("};\n")

    # ---- write include/generated/face_frames.h ---------------------------
    with open(OUT_HEADER, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_faces.py - do not edit by hand.\n")
        f.write("#ifndef FACE_FRAMES_H\n#define FACE_FRAMES_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define FACE_FRAME_W {FRAME_W}\n")
        f.write(f"#define FACE_FRAME_H {FRAME_H}\n")
        f.write(f"#define FACE_ANIM_COUNT {len(zips)}\n\n")
        f.write(f"#define FACE_BLINK_SET {BLINK_SET}\n")
        f.write(f"#define FACE_BLINK_FRAME {BLINK_SOURCE_FRAME - 1}\n\n")  # 0-indexed
        f.write("// faceAnimFrameCount[set] = frame count; faceAnimFrames[set][frame] =\n")
        f.write("// pointer to that frame's packed 1-bit bitmap (FACE_FRAME_W x FACE_FRAME_H,\n")
        f.write("// MSB-first, Adafruit_GFX::drawBitmap() layout).\n")
        f.write("extern const uint16_t faceAnimFrameCount[FACE_ANIM_COUNT];\n")
        f.write("extern const unsigned char *const *const faceAnimFrames[FACE_ANIM_COUNT];\n\n")
        f.write("#endif // FACE_FRAMES_H\n")

    total_bytes = sum(anim_frame_counts) * ROW_BYTES * FRAME_H
    log(
        f"wrote {len(zips)} animations, {sum(anim_frame_counts)} frames total, "
        f"~{total_bytes / 1024:.0f} KiB of bitmap data -> {OUT_SOURCE}"
    )


def pack_1bit_wrapper(png_bytes):
    return load_and_pack(png_bytes)


def format_array(sym, data):
    lines = [f"static const unsigned char {sym}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), 16):
        row = ", ".join(f"0x{b:02X}" for b in data[i : i + 16])
        lines.append(f"    {row},")
    lines.append("};")
    return "\n".join(lines)


def format_pointer_table(sym, frame_syms):
    lines = [f"static const unsigned char *const {sym}[{len(frame_syms)}] PROGMEM = {{"]
    for i in range(0, len(frame_syms), 8):
        row = ", ".join(frame_syms[i : i + 8])
        lines.append(f"    {row},")
    lines.append("};")
    return "\n".join(lines)


main()
