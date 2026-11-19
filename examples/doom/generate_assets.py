#!/usr/bin/env python3
"""Generate procedural pixel-art textures, synthesized WAV SFX/music, animsets,
object templates (.pttemplate), and 2 playable 2.5D DOOM scenes (.scene) for examples/doom.
Uses only Python 3 standard library modules.
"""

from __future__ import annotations

import json
import math
import struct
import wave
import zlib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
ASSETS_DIR = ROOT / "assets"

LAYER_DEFAULT = 1 << 0
LAYER_WALL = 1 << 1
LAYER_PLAYER = 1 << 2
LAYER_ENEMY = 1 << 3
LAYER_PICKUP = 1 << 4
LAYER_PROJECTILE = 1 << 5
LAYER_DOOR = 1 << 6
LAYER_BARREL = 1 << 7
ALL_MASK = 4294967295


# ---------------------------------------------------------------------------
# Pure-stdlib RGBA PNG & PCM WAV writers
# ---------------------------------------------------------------------------

def encode_png(width: int, height: int, pixels: bytearray) -> bytes:
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        start = y * stride
        raw.extend(pixels[start : start + stride])

    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw), level=6)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", idat)
        + chunk(b"IEND", b"")
    )


class Canvas:
    def __init__(self, width: int, height: int) -> None:
        self.w = width
        self.h = height
        self.buf = bytearray(width * height * 4)

    def px(self, x: int, y: int, rgba: tuple[int, int, int, int]) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            idx = (y * self.w + x) * 4
            sa = rgba[3]
            if sa == 255:
                self.buf[idx : idx + 4] = bytes(rgba)
            elif sa > 0:
                da = self.buf[idx + 3]
                if da == 0:
                    self.buf[idx : idx + 4] = bytes(rgba)
                else:
                    inv = 255 - sa
                    self.buf[idx] = (rgba[0] * sa + self.buf[idx] * inv) // 255
                    self.buf[idx + 1] = (rgba[1] * sa + self.buf[idx + 1] * inv) // 255
                    self.buf[idx + 2] = (rgba[2] * sa + self.buf[idx + 2] * inv) // 255
                    self.buf[idx + 3] = min(255, sa + (da * inv) // 255)

    def rect(self, x: int, y: int, w: int, h: int, rgba: tuple[int, int, int, int]) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.px(xx, yy, rgba)

    def disc(self, cx: float, cy: float, r: float, rgba: tuple[int, int, int, int]) -> None:
        r2 = r * r
        for y in range(int(math.floor(cy - r)), int(math.ceil(cy + r)) + 1):
            for x in range(int(math.floor(cx - r)), int(math.ceil(cx + r)) + 1):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r2:
                    self.px(x, y, rgba)

    def outline(self, border_rgba: tuple[int, int, int, int]) -> None:
        out = bytearray(self.buf)
        for y in range(self.h):
            for x in range(self.w):
                idx = (y * self.w + x) * 4
                if self.buf[idx + 3] != 0:
                    continue
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if 0 <= nx < self.w and 0 <= ny < self.h:
                        if self.buf[(ny * self.w + nx) * 4 + 3] > 170:
                            out[idx : idx + 4] = bytes(border_rgba)
                            break
        self.buf = out

    def save(self, rel_path: str) -> str:
        full = ASSETS_DIR / rel_path
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_bytes(encode_png(self.w, self.h, self.buf))
        return f"assets/{rel_path}"


def write_wav(rel_path: str, samples: list[float], sample_rate: int = 22050) -> str:
    full = ASSETS_DIR / rel_path
    full.parent.mkdir(parents=True, exist_ok=True)
    pcm = bytearray()
    for s in samples:
        v = max(-1.0, min(1.0, s))
        pcm.extend(struct.pack("<h", int(v * 32767)))
    with wave.open(str(full), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(pcm))
    return f"assets/{rel_path}"


def hash2(x: int, y: int, seed: int = 1337) -> int:
    n = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return (n ^ (n >> 16)) & 0xFF


# ---------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------

def generate_textures() -> dict[str, str]:
    tex: dict[str, str] = {}

    # 1. Wall Tech (UAC Steel Panels + Rivets + Hazard Trim)
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            noise = (hash2(x, y, 1) % 13) - 6
            rib = 10 if (x % 16) in (0, 1) else (-12 if (x % 16) == 15 else 0)
            base = 106 + noise + rib
            if y < 6 or y >= 58:
                base -= 24
            if y in (6, 57):
                base += 20
            c.px(x, y, (max(20, min(220, base)), max(20, min(220, base + 4)), max(25, min(230, base + 10)), 255))
    c.rect(0, 31, 64, 2, (48, 52, 60, 255))
    c.rect(0, 50, 64, 5, (42, 42, 46, 255))
    for rx in range(4, 64, 16):
        for ry in (9, 27, 36, 47):
            c.rect(rx, ry, 2, 2, (168, 174, 186, 255))
    tex["wall_tech"] = c.save("textures/walls/wall_tech.png")

    # 2. Wall Brick (Rust & Hell Masonry)
    c = Canvas(64, 64)
    c.rect(0, 0, 64, 64, (44, 30, 26, 255))
    for row in range(4):
        y0 = row * 16
        offset = 16 if (row % 2) else 0
        for col in range(-1, 3):
            x0 = col * 32 + offset
            for yy in range(y0 + 1, y0 + 15):
                for xx in range(max(0, x0 + 1), min(64, x0 + 31)):
                    n = (hash2(xx, yy, 42) % 19) - 9
                    shade = 136 - (yy - y0) * 2 + n
                    c.px(xx, yy, (max(30, min(210, shade)), max(20, min(140, shade // 2 + 12)), max(15, min(110, shade // 3 + 8)), 255))
    tex["wall_brick"] = c.save("textures/walls/wall_brick.png")

    # 3. Wall Computer (Dark Tech Panel + Glowing Emerald Monitors)
    c = Canvas(64, 64)
    c.rect(0, 0, 64, 64, (42, 48, 60, 255))
    c.rect(2, 2, 60, 60, (56, 64, 78, 255))
    c.rect(8, 10, 48, 28, (22, 28, 34, 255))
    c.rect(10, 12, 44, 24, (14, 48, 28, 255))
    for x in range(12, 52, 2):
        wy = 22 + int(7 * math.sin(x * 0.45))
        c.rect(x, wy, 2, 2, (70, 248, 130, 255))
    c.rect(10, 44, 10, 6, (220, 60, 50, 255))
    c.rect(24, 44, 10, 6, (240, 200, 40, 255))
    c.rect(38, 44, 16, 6, (60, 210, 240, 255))
    tex["wall_comp"] = c.save("textures/walls/wall_comp.png")

    # 4. Wall Pillar (Fluted Reactor Column)
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            flute = int(28 * math.sin(x * math.pi / 8.0))
            n = (hash2(x, y, 77) % 11) - 5
            b = 88 + flute + n
            if y < 8 or y > 55:
                b = 112 + n
            c.px(x, y, (max(20, min(210, b)), max(20, min(210, b - 4)), max(20, min(200, b - 10)), 255))
    c.rect(0, 8, 64, 3, (175, 130, 50, 255))
    c.rect(0, 53, 64, 3, (175, 130, 50, 255))
    tex["wall_pillar"] = c.save("textures/walls/wall_pillar.png")

    # 5. Wall Door (Steel Blast Door)
    c = Canvas(64, 64)
    c.rect(0, 0, 64, 64, (74, 78, 88, 255))
    for y in range(64):
        for x in range(64):
            if y < 6 or y >= 58:
                stripe = ((x + y) // 6) % 2
                c.px(x, y, (230, 185, 30, 255) if stripe == 0 else (26, 26, 30, 255))
            else:
                n = (hash2(x, y, 91) % 11) - 5
                ridge = -22 if abs(x - 32) <= 1 else (12 if x in (6, 57) else 0)
                v = max(25, min(220, 96 + n + ridge))
                c.px(x, y, (v, v + 3, v + 8, 255))
    c.rect(8, 12, 20, 40, (64, 68, 76, 255))
    c.rect(36, 12, 20, 40, (64, 68, 76, 255))
    tex["wall_door"] = c.save("textures/walls/wall_door.png")

    # 6. Wall Door Red (Keycard Locked Door)
    c = Canvas(64, 64)
    c.rect(0, 0, 64, 64, (72, 68, 74, 255))
    c.rect(0, 0, 8, 64, (190, 28, 28, 255))
    c.rect(56, 0, 8, 64, (190, 28, 28, 255))
    c.rect(2, 6, 4, 52, (255, 70, 70, 255))
    c.rect(58, 6, 4, 52, (255, 70, 70, 255))
    c.rect(10, 6, 44, 52, (88, 92, 102, 255))
    c.rect(31, 6, 2, 52, (36, 36, 40, 255))
    c.rect(22, 24, 20, 16, (180, 24, 24, 255))
    c.rect(25, 27, 14, 10, (255, 190, 60, 255))
    tex["wall_door_red"] = c.save("textures/walls/wall_door_red.png")

    # 7. Wall Exit (EXIT Sign Panel)
    c = Canvas(64, 64)
    c.rect(0, 0, 64, 64, (74, 60, 56, 255))
    c.rect(6, 10, 52, 44, (52, 44, 42, 255))
    c.rect(12, 16, 40, 16, (28, 12, 12, 255))
    # Draw crisp pixel "EXIT"
    # E
    c.rect(15, 19, 2, 10, (255, 65, 45, 255))
    c.rect(15, 19, 6, 2, (255, 65, 45, 255))
    c.rect(15, 23, 5, 2, (255, 65, 45, 255))
    c.rect(15, 27, 6, 2, (255, 65, 45, 255))
    # X
    for i in range(6):
        c.rect(24 + i, 19 + i * 2 - (1 if i > 0 else 0), 2, 2, (255, 65, 45, 255))
        c.rect(29 - i, 19 + i * 2 - (1 if i > 0 else 0), 2, 2, (255, 65, 45, 255))
    # I
    c.rect(34, 19, 2, 10, (255, 65, 45, 255))
    # T
    c.rect(39, 19, 8, 2, (255, 65, 45, 255))
    c.rect(42, 19, 2, 10, (255, 65, 45, 255))
    c.rect(24, 38, 16, 12, (220, 45, 35, 255))
    tex["wall_exit"] = c.save("textures/walls/wall_exit.png")

    # 8. Ceiling & Floor Backdrops
    c = Canvas(32, 152)
    for y in range(152):
        t = y / 151.0
        r = int(12 + 18 * t)
        g = int(14 + 20 * t)
        b = int(20 + 28 * t)
        c.rect(0, y, 32, 1, (r, g, b, 255))
    tex["ceiling"] = c.save("textures/world/ceiling.png")

    c = Canvas(32, 152)
    for y in range(152):
        t = y / 151.0
        r = int(28 + 38 * t)
        g = int(26 + 32 * t)
        b = int(24 + 26 * t)
        c.rect(0, y, 32, 1, (r, g, b, 255))
    tex["floor"] = c.save("textures/world/floor.png")

    # 9. Weapons (96x96)
    def make_pistol(firing: bool) -> Canvas:
        w = Canvas(96, 96)
        # Hand / glove
        w.rect(40, 74, 22, 22, (165, 118, 92, 255))
        w.rect(38, 78, 26, 18, (64, 52, 46, 255))
        # Grip
        w.rect(41, 60, 16, 20, (54, 46, 42, 255))
        # Receiver / slide
        w.rect(38, 36, 20, 28, (96, 104, 116, 255))
        w.rect(41, 32, 14, 16, (132, 142, 156, 255))
        w.rect(45, 30, 6, 4, (50, 54, 62, 255))
        if firing:
            w.disc(48, 22, 16, (255, 130, 20, 255))
            w.disc(48, 22, 10, (255, 220, 60, 255))
            w.disc(48, 22, 5, (255, 255, 230, 255))
        w.outline((16, 16, 20, 255))
        return w

    def make_shotgun(firing: bool) -> Canvas:
        w = Canvas(96, 96)
        # Wood pump & stock
        w.rect(36, 62, 24, 34, (135, 76, 38, 255))
        w.rect(40, 48, 16, 20, (112, 62, 30, 255))
        # Dual steel barrels
        w.rect(39, 30, 8, 30, (92, 98, 110, 255))
        w.rect(49, 30, 8, 30, (92, 98, 110, 255))
        w.rect(41, 30, 4, 24, (148, 156, 168, 255))
        w.rect(51, 30, 4, 24, (148, 156, 168, 255))
        if firing:
            w.disc(48, 20, 22, (255, 110, 15, 255))
            w.disc(38, 24, 12, (255, 170, 30, 255))
            w.disc(58, 24, 12, (255, 170, 30, 255))
            w.disc(48, 20, 11, (255, 245, 120, 255))
            w.disc(48, 22, 6, (255, 255, 245, 255))
        w.outline((16, 16, 20, 255))
        return w

    def make_plasma(firing: bool) -> Canvas:
        w = Canvas(96, 96)
        # Heavy housing
        w.rect(32, 60, 32, 36, (52, 62, 82, 255))
        w.rect(38, 34, 20, 34, (74, 92, 124, 255))
        # Glowing cyan coils
        for y in (38, 46, 54):
            glow = (140, 255, 255, 255) if firing else (55, 200, 245, 255)
            w.rect(36, y, 24, 4, glow)
        w.rect(43, 28, 10, 10, (95, 115, 145, 255))
        if firing:
            w.disc(48, 20, 18, (40, 180, 255, 255))
            w.disc(48, 20, 11, (140, 245, 255, 255))
            w.disc(48, 20, 5, (255, 255, 255, 255))
        w.outline((16, 16, 20, 255))
        return w

    tex["pistol_idle"] = make_pistol(False).save("textures/weapons/pistol_idle.png")
    tex["pistol_fire"] = make_pistol(True).save("textures/weapons/pistol_fire.png")
    tex["shotgun_idle"] = make_shotgun(False).save("textures/weapons/shotgun_idle.png")
    tex["shotgun_fire"] = make_shotgun(True).save("textures/weapons/shotgun_fire.png")
    tex["plasma_idle"] = make_plasma(False).save("textures/weapons/plasma_idle.png")
    tex["plasma_fire"] = make_plasma(True).save("textures/weapons/plasma_fire.png")

    # 10. Enemies (64x64)
    def make_trooper(pose: str) -> Canvas:
        e = Canvas(64, 64)
        stride = 4 if pose == "walk0" else (-4 if pose == "walk1" else 0)
        # Legs
        e.rect(20, 42 + max(0, stride), 8, 18, (72, 86, 60, 255))
        e.rect(36, 42 + max(0, -stride), 8, 18, (72, 86, 60, 255))
        e.rect(19, 56 + max(0, stride), 10, 5, (42, 34, 28, 255))
        e.rect(35, 56 + max(0, -stride), 10, 5, (42, 34, 28, 255))
        # Armor torso
        e.rect(18, 22, 28, 22, (88, 108, 72, 255))
        e.rect(22, 25, 20, 14, (112, 134, 92, 255))
        # Rifle across chest
        e.rect(12, 30, 38, 6, (58, 62, 70, 255))
        if pose == "attack":
            e.disc(10, 33, 6, (255, 210, 50, 255))
            e.disc(10, 33, 3, (255, 255, 220, 255))
        # Helmet & crimson visor
        e.disc(32, 15, 10, (96, 114, 78, 255))
        e.rect(25, 13, 14, 4, (235, 45, 45, 255))
        if pose == "hurt":
            e.disc(30, 28, 6, (230, 35, 35, 255))
        e.outline((16, 16, 18, 255))
        return e

    def make_imp(pose: str) -> Canvas:
        e = Canvas(64, 64)
        stride = 4 if pose == "walk0" else (-4 if pose == "walk1" else 0)
        # Legs
        e.rect(20, 42 + max(0, stride), 8, 18, (132, 84, 54, 255))
        e.rect(36, 42 + max(0, -stride), 8, 18, (132, 84, 54, 255))
        # Muscular brown torso
        e.rect(18, 20, 28, 24, (158, 102, 66, 255))
        e.rect(22, 24, 20, 14, (186, 126, 84, 255))
        # Chest spikes
        for sy in (24, 30, 36):
            e.rect(31, sy, 3, 3, (232, 224, 198, 255))
        # Arms
        arm_y = 20 if pose == "attack" else 26
        e.rect(10, arm_y, 8, 14, (148, 92, 58, 255))
        e.rect(46, arm_y, 8, 14, (148, 92, 58, 255))
        if pose == "attack":
            e.disc(14, 18, 6, (255, 135, 20, 255))
            e.disc(50, 18, 6, (255, 135, 20, 255))
        # Head & horns + glowing red eyes
        e.disc(32, 15, 9, (158, 102, 66, 255))
        e.rect(22, 5, 4, 6, (220, 210, 185, 255))
        e.rect(38, 5, 4, 6, (220, 210, 185, 255))
        e.rect(26, 13, 4, 3, (255, 45, 35, 255))
        e.rect(34, 13, 4, 3, (255, 45, 35, 255))
        if pose == "hurt":
            e.disc(34, 28, 7, (240, 40, 40, 255))
        e.outline((18, 12, 10, 255))
        return e

    def make_baron(pose: str) -> Canvas:
        e = Canvas(64, 64)
        stride = 3 if pose == "walk0" else (-3 if pose == "walk1" else 0)
        # Heavy goat legs & hooves
        e.rect(18, 40 + max(0, stride), 10, 20, (108, 76, 56, 255))
        e.rect(36, 40 + max(0, -stride), 10, 20, (108, 76, 56, 255))
        e.rect(17, 56 + max(0, stride), 12, 5, (38, 34, 34, 255))
        e.rect(35, 56 + max(0, -stride), 12, 5, (38, 34, 34, 255))
        # Massive crimson torso
        e.rect(14, 18, 36, 26, (176, 46, 52, 255))
        e.rect(20, 22, 24, 16, (206, 72, 74, 255))
        # Big arms + green hellfire claws
        e.rect(6, 20, 10, 18, (164, 42, 48, 255))
        e.rect(48, 20, 10, 18, (164, 42, 48, 255))
        if pose == "attack":
            e.disc(10, 18, 7, (80, 255, 110, 255))
            e.disc(54, 18, 7, (80, 255, 110, 255))
        # Horned skull head
        e.disc(32, 14, 10, (176, 46, 52, 255))
        e.rect(16, 4, 6, 8, (230, 215, 175, 255))
        e.rect(42, 4, 6, 8, (230, 215, 175, 255))
        e.rect(26, 12, 4, 3, (110, 255, 110, 255))
        e.rect(34, 12, 4, 3, (110, 255, 110, 255))
        if pose == "hurt":
            e.disc(32, 26, 8, (255, 210, 210, 255))
        e.outline((20, 10, 12, 255))
        return e

    for p_name, fn in (("trooper", make_trooper), ("imp", make_imp), ("baron", make_baron)):
        for pose in ("walk0", "walk1", "attack", "hurt"):
            tex[f"{p_name}_{pose}"] = fn(pose).save(f"textures/enemies/{p_name}_{pose}.png")

    # 11. Barrel (48x48)
    c = Canvas(48, 48)
    c.rect(11, 8, 26, 36, (94, 100, 108, 255))
    c.rect(14, 8, 8, 36, (124, 130, 140, 255))
    c.rect(10, 10, 28, 4, (54, 58, 64, 255))
    c.rect(10, 24, 28, 3, (54, 58, 64, 255))
    c.rect(10, 38, 28, 4, (54, 58, 64, 255))
    # Glowing toxic sludge rim & hazard stripe
    c.rect(13, 6, 22, 4, (70, 240, 80, 255))
    c.rect(16, 16, 16, 6, (40, 200, 55, 255))
    c.outline((18, 20, 18, 255))
    tex["barrel"] = c.save("textures/props/barrel.png")

    # 12. Pickups (40x40)
    # Medkit
    c = Canvas(40, 40)
    c.rect(6, 10, 28, 22, (232, 236, 240, 255))
    c.rect(14, 6, 12, 4, (170, 178, 190, 255))
    c.rect(18, 14, 4, 14, (220, 35, 35, 255))
    c.rect(13, 19, 14, 4, (220, 35, 35, 255))
    c.outline((20, 24, 28, 255))
    tex["medkit"] = c.save("textures/items/medkit.png")

    # Armor
    c = Canvas(40, 40)
    c.rect(10, 8, 20, 26, (50, 115, 215, 255))
    c.rect(14, 12, 12, 18, (80, 155, 245, 255))
    c.rect(8, 8, 6, 12, (40, 95, 190, 255))
    c.rect(26, 8, 6, 12, (40, 95, 190, 255))
    c.outline((16, 22, 34, 255))
    tex["armor"] = c.save("textures/items/armor.png")

    # Ammo box
    c = Canvas(40, 40)
    c.rect(8, 14, 24, 18, (150, 100, 45, 255))
    c.rect(8, 14, 24, 5, (195, 45, 35, 255))
    for x in range(11, 28, 5):
        c.rect(x, 21, 3, 8, (235, 205, 70, 255))
    c.outline((24, 18, 12, 255))
    tex["ammo_box"] = c.save("textures/items/ammo_box.png")

    # Shotgun pickup
    c = Canvas(56, 32)
    c.rect(6, 15, 18, 8, (140, 80, 40, 255))
    c.rect(18, 12, 32, 6, (130, 138, 150, 255))
    c.rect(24, 18, 12, 4, (115, 65, 30, 255))
    c.outline((16, 16, 20, 255))
    tex["pickup_shotgun"] = c.save("textures/items/pickup_shotgun.png")

    # Plasma pickup
    c = Canvas(56, 32)
    c.rect(8, 12, 40, 10, (68, 88, 122, 255))
    for x in (20, 28, 36):
        c.rect(x, 10, 5, 12, (70, 225, 255, 255))
    c.rect(14, 20, 8, 6, (50, 62, 80, 255))
    c.outline((16, 20, 30, 255))
    tex["pickup_plasma"] = c.save("textures/items/pickup_plasma.png")

    # Red Keycard
    c = Canvas(32, 32)
    c.rect(6, 9, 20, 14, (225, 35, 40, 255))
    c.rect(8, 11, 16, 4, (255, 95, 95, 255))
    c.rect(18, 17, 6, 4, (245, 220, 75, 255))
    c.outline((24, 10, 10, 255))
    tex["keycard_red"] = c.save("textures/items/keycard_red.png")

    # Exit Beacon
    c = Canvas(48, 48)
    c.disc(24, 24, 18, (215, 55, 30, 255))
    c.disc(24, 24, 12, (255, 160, 40, 255))
    c.disc(24, 24, 6, (255, 245, 175, 255))
    c.outline((40, 14, 10, 255))
    tex["exit_beacon"] = c.save("textures/items/exit_beacon.png")

    # 13. Projectiles & FX
    for idx, r_inner in enumerate((7, 5)):
        fb = Canvas(32, 32)
        fb.disc(16, 16, 11 + (1 - idx), (235, 70, 15, 255))
        fb.disc(16, 16, r_inner, (255, 205, 40, 255))
        fb.disc(16, 16, 3, (255, 255, 210, 255))
        tex[f"fireball_{idx}"] = fb.save(f"textures/fx/fireball_{idx}.png")

        pb = Canvas(32, 32)
        pb.disc(16, 16, 10 + idx, (30, 145, 255, 255))
        pb.disc(16, 16, 7, (110, 235, 255, 255))
        pb.disc(16, 16, 3, (255, 255, 255, 255))
        tex[f"plasma_bolt_{idx}"] = pb.save(f"textures/fx/plasma_bolt_{idx}.png")

    for i, r in enumerate((4, 8, 12)):
        sp = Canvas(32, 32)
        sp.disc(16, 16, r, (255, 185 - i * 35, 45, 255))
        sp.disc(16, 16, max(2, r // 2), (255, 250, 190, 255))
        tex[f"spark_{i}"] = sp.save(f"textures/fx/spark_{i}.png")

        ex = Canvas(64, 64)
        ex.disc(32, 32, 12 + i * 7, (220 - i * 30, 65, 20, 255))
        ex.disc(32, 32, 8 + i * 4, (255, 175 - i * 25, 35, 255))
        ex.disc(32, 32, max(3, 9 - i * 2), (255, 245, 175, 255))
        tex[f"explosion_{i}"] = ex.save(f"textures/fx/explosion_{i}.png")

    # 14. HUD Status Bar, Bitmap Digits, Doomguy Portraits, Crosshair, White, Player Map Arrow
    hud = Canvas(640, 56)
    hud.rect(0, 0, 640, 56, (66, 70, 78, 255))
    hud.rect(0, 0, 640, 3, (112, 118, 130, 255))
    hud.rect(0, 53, 640, 3, (34, 36, 42, 255))
    for bx in (10, 102, 196, 290, 354, 452, 540):
        w_box = 84 if bx in (10, 102, 354) else (56 if bx == 290 else (84 if bx == 196 else (78 if bx == 452 else 90)))
        hud.rect(bx, 6, w_box, 44, (44, 46, 52, 255))
        hud.rect(bx + 2, 8, w_box - 4, 40, (24, 24, 28, 255))
    # Tiny pixel labels at Y=10..15 in HUD panels
    # Tiny indicator blocks for "AMMO", "HP", "WPN 1 2 3", "ARM", "KILLS", "KEY"
    # Weapon labels 1, 2, 3 inside box 196..280
    # 1 at x=216, 2 at x=240, 3 at x=264
    hud.rect(215, 16, 2, 10, (185, 190, 200, 255))  # '1'
    hud.rect(237, 16, 6, 2, (185, 190, 200, 255))   # '2' top
    hud.rect(241, 18, 2, 3, (185, 190, 200, 255))
    hud.rect(237, 20, 6, 2, (185, 190, 200, 255))
    hud.rect(237, 22, 2, 2, (185, 190, 200, 255))
    hud.rect(237, 24, 6, 2, (185, 190, 200, 255))
    hud.rect(261, 16, 6, 2, (185, 190, 200, 255))   # '3'
    hud.rect(265, 16, 2, 10, (185, 190, 200, 255))
    hud.rect(262, 20, 5, 2, (185, 190, 200, 255))
    hud.rect(261, 24, 6, 2, (185, 190, 200, 255))
    # Colored header bars for AMMO (amber), HP (red), ARMOR (blue), KILLS (orange), KEY (red)
    hud.rect(18, 12, 68, 4, (215, 175, 45, 255))
    hud.rect(110, 12, 68, 4, (205, 50, 45, 255))
    hud.rect(362, 12, 68, 4, (65, 145, 235, 255))
    hud.rect(460, 12, 62, 4, (215, 110, 35, 255))
    hud.rect(548, 12, 74, 4, (195, 40, 40, 255))
    tex["hud_bar"] = hud.save("textures/hud/hud_bar.png")

    # 7-segment bold pixel digits 0..9 (each 10x16 -> total 100x16)
    digits = Canvas(100, 16)
    # Segments: a(top), b(tr), c(br), d(bot), e(bl), f(tl), g(mid)
    segs = {
        0: "abcdef",
        1: "bc",
        2: "abged",
        3: "abcdg",
        4: "fgbc",
        5: "afgcd",
        6: "afgecd",
        7: "abc",
        8: "abcdefg",
        9: "abfgcd",
    }
    red = (235, 42, 36, 255)
    hi = (255, 125, 95, 255)
    for num, active in segs.items():
        ox = num * 10 + 1
        if "a" in active:
            digits.rect(ox + 1, 1, 6, 2, red)
        if "d" in active:
            digits.rect(ox + 1, 13, 6, 2, red)
        if "g" in active:
            digits.rect(ox + 1, 7, 6, 2, red)
        if "f" in active:
            digits.rect(ox, 2, 2, 5, red)
        if "b" in active:
            digits.rect(ox + 6, 2, 2, 5, hi)
        if "e" in active:
            digits.rect(ox, 8, 2, 5, red)
        if "c" in active:
            digits.rect(ox + 6, 8, 2, 5, hi)
    tex["digits"] = digits.save("textures/hud/digits.png")

    # Doomguy portraits 128x32 (4 frames x 32x32)
    faces = Canvas(128, 32)
    for fi in range(4):
        ox = fi * 32
        faces.rect(ox + 6, 4, 20, 24, (205, 155, 125, 255))
        faces.rect(ox + 7, 2, 18, 5, (85, 58, 42, 255))  # Hair
        # Eyes
        if fi == 3:
            faces.rect(ox + 10, 11, 4, 2, (40, 20, 20, 255))
            faces.rect(ox + 18, 11, 4, 2, (40, 20, 20, 255))
        else:
            faces.rect(ox + 10, 11, 4, 3, (235, 235, 240, 255))
            faces.rect(ox + 18, 11, 4, 3, (235, 235, 240, 255))
            faces.rect(ox + 11, 11, 2, 3, (40, 95, 175, 255))
            faces.rect(ox + 19, 11, 2, 3, (40, 95, 175, 255))
        # Blood / bruises for frames 1..3
        if fi >= 1:
            faces.rect(ox + 8, 16, 4, 4, (190, 25, 25, 255))
        if fi >= 2:
            faces.rect(ox + 18, 18, 5, 6, (180, 20, 20, 255))
            faces.rect(ox + 12, 6, 6, 3, (175, 20, 20, 255))
        # Mouth
        if fi == 0:
            faces.rect(ox + 12, 21, 8, 2, (115, 55, 48, 255))
        elif fi == 1:
            faces.rect(ox + 11, 20, 10, 3, (235, 235, 235, 255))
        else:
            faces.rect(ox + 12, 20, 8, 4, (75, 18, 18, 255))
    tex["face_strip"] = faces.save("textures/hud/face_strip.png")

    # Crosshair (16x16)
    ch = Canvas(16, 16)
    green = (95, 255, 110, 235)
    ch.rect(7, 1, 2, 4, green)
    ch.rect(7, 11, 2, 4, green)
    ch.rect(1, 7, 4, 2, green)
    ch.rect(11, 7, 4, 2, green)
    tex["crosshair"] = ch.save("textures/hud/crosshair.png")

    # White 4x4 tint swatch
    wh = Canvas(4, 4)
    wh.rect(0, 0, 4, 4, (255, 255, 255, 255))
    tex["white"] = wh.save("textures/hud/white.png")

    # Top-down Player arrow sprite (32x32 facing +X Right)
    pl = Canvas(32, 32)
    for x in range(6, 29):
        half = max(1, int((28 - x) * 0.52))
        pl.rect(x, 16 - half, 1, half * 2, (80, 240, 115, 255))
    pl.outline((16, 32, 16, 255))
    tex["player_map"] = pl.save("textures/map/player_map.png")

    return tex


# ---------------------------------------------------------------------------
# Synthesized Retro Audio
# ---------------------------------------------------------------------------

def generate_audio() -> dict[str, str]:
    sr = 22050
    aud: dict[str, str] = {}

    def lcg(n: int, seed: int) -> list[float]:
        out = []
        s = seed
        for _ in range(n):
            s = (1664525 * s + 1013904223) & 0xFFFFFFFF
            out.append(((s >> 16) / 32767.5) - 1.0)
        return out

    # Pistol bang
    noise = lcg(int(0.18 * sr), 11)
    pistol = [
        (0.55 * math.sin(2 * math.pi * (240.0 * math.exp(-i / (0.04 * sr))) * (i / sr)) + 0.45 * noise[i])
        * math.exp(-i / (0.045 * sr)) * 0.65
        for i in range(len(noise))
    ]
    aud["pistol"] = write_wav("audio/pistol.wav", pistol, sr)

    # Shotgun boom
    noise = lcg(int(0.32 * sr), 22)
    shotgun = [
        (0.45 * math.sin(2 * math.pi * (140.0 * math.exp(-i / (0.06 * sr))) * (i / sr)) + 0.65 * noise[i])
        * math.exp(-i / (0.075 * sr)) * 0.72
        for i in range(len(noise))
    ]
    aud["shotgun"] = write_wav("audio/shotgun.wav", shotgun, sr)

    # Plasma pew
    n = int(0.14 * sr)
    plasma = [
        math.sin(2 * math.pi * (920.0 - 520.0 * (i / n)) * (i / sr)) * math.exp(-i / (0.04 * sr)) * 0.55
        for i in range(n)
    ]
    aud["plasma"] = write_wav("audio/plasma.wav", plasma, sr)

    # Door slide rumble
    noise = lcg(int(0.35 * sr), 33)
    door = [
        (0.5 * math.sin(2 * math.pi * 88.0 * (i / sr)) + 0.35 * noise[i]) * math.sin(math.pi * i / len(noise)) * 0.45
        for i in range(len(noise))
    ]
    aud["door"] = write_wav("audio/door.wav", door, sr)

    # Door locked buzz
    n = int(0.22 * sr)
    locked = [
        (1.0 if math.sin(2 * math.pi * 130.0 * (i / sr)) > 0 else -1.0) * math.exp(-i / (0.09 * sr)) * 0.35
        for i in range(n)
    ]
    aud["door_locked"] = write_wav("audio/door_locked.wav", locked, sr)

    # Barrel explosion
    noise = lcg(int(0.55 * sr), 44)
    exp_sfx = [
        (0.55 * math.sin(2 * math.pi * (75.0 * math.exp(-i / (0.14 * sr))) * (i / sr)) + 0.65 * noise[i])
        * math.exp(-i / (0.16 * sr)) * 0.8
        for i in range(len(noise))
    ]
    aud["explosion"] = write_wav("audio/explosion.wav", exp_sfx, sr)

    # Enemy alert, hurt, death
    n = int(0.22 * sr)
    aud["enemy_alert"] = write_wav(
        "audio/enemy_alert.wav",
        [math.sin(2 * math.pi * (280.0 + 180.0 * (i / n)) * (i / sr)) * math.exp(-i / (0.09 * sr)) * 0.45 for i in range(n)],
        sr,
    )
    n = int(0.14 * sr)
    aud["enemy_hurt"] = write_wav(
        "audio/enemy_hurt.wav",
        [math.sin(2 * math.pi * (260.0 - 110.0 * (i / n)) * (i / sr)) * math.exp(-i / (0.05 * sr)) * 0.45 for i in range(n)],
        sr,
    )
    noise = lcg(int(0.28 * sr), 55)
    aud["enemy_death"] = write_wav(
        "audio/enemy_death.wav",
        [(0.6 * math.sin(2 * math.pi * (190.0 - 130.0 * (i / len(noise))) * (i / sr)) + 0.4 * noise[i])
         * math.exp(-i / (0.10 * sr)) * 0.55 for i in range(len(noise))],
        sr,
    )

    # Player hurt
    n = int(0.18 * sr)
    aud["player_hurt"] = write_wav(
        "audio/player_hurt.wav",
        [math.sin(2 * math.pi * (170.0 - 80.0 * (i / n)) * (i / sr)) * math.exp(-i / (0.07 * sr)) * 0.55 for i in range(n)],
        sr,
    )

    # Pickup & keycard chimes
    n = int(0.16 * sr)
    aud["pickup"] = write_wav(
        "audio/pickup.wav",
        [math.sin(2 * math.pi * (520.0 if i < n // 2 else 780.0) * (i / sr)) * math.exp(-i / (0.08 * sr)) * 0.42
         for i in range(n)],
        sr,
    )
    n = int(0.24 * sr)
    aud["keycard"] = write_wav(
        "audio/keycard.wav",
        [math.sin(2 * math.pi * (587.0 if i < n // 3 else (784.0 if i < 2 * n // 3 else 1046.0)) * (i / sr))
         * math.exp(-i / (0.14 * sr)) * 0.45 for i in range(n)],
        sr,
    )

    # Driving dark industrial BGM loops (E1M1 & E1M2)
    def make_bgm(riff_hz: list[float], tempo_s: float) -> list[float]:
        step_n = int(tempo_s * sr)
        out: list[float] = []
        for idx, f in enumerate(riff_hz):
            for i in range(step_n):
                t = i / sr
                env = math.exp(-i / (tempo_s * 0.55 * sr))
                bass = 1.0 if math.sin(2 * math.pi * f * t) > 0 else -1.0
                sub = math.sin(2 * math.pi * f * 0.5 * t)
                kick = math.sin(2 * math.pi * 65.0 * math.exp(-i / (0.03 * sr)) * t) * math.exp(-i / (0.04 * sr)) if (idx % 2 == 0) else 0.0
                out.append((0.18 * bass * env + 0.16 * sub * env + 0.22 * kick) * 0.45)
        return out

    aud["bgm_e1m1"] = write_wav(
        "audio/bgm_e1m1.wav",
        make_bgm([82.4, 82.4, 164.8, 82.4, 82.4, 146.8, 82.4, 82.4, 130.8, 82.4, 82.4, 116.5, 82.4, 110.0, 123.5, 98.0], 0.14),
        sr,
    )
    aud["bgm_e1m2"] = write_wav(
        "audio/bgm_e1m2.wav",
        make_bgm([73.4, 73.4, 110.0, 73.4, 116.5, 73.4, 98.0, 87.3, 73.4, 73.4, 146.8, 130.8, 110.0, 98.0, 87.3, 82.4], 0.16),
        sr,
    )
    return aud


# ---------------------------------------------------------------------------
# Animsets
# ---------------------------------------------------------------------------

def write_json(rel_path: str, data: Any) -> str:
    full = ASSETS_DIR / rel_path
    full.parent.mkdir(parents=True, exist_ok=True)
    full.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return f"assets/{rel_path}"


def anim_frame(tex_path: str, duration: float) -> dict[str, Any]:
    return {
        "duration": duration,
        "hasSourceRect": False,
        "sourceRect": {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0},
        "textureWrapperFilePath": tex_path,
    }


def generate_animsets(tex: dict[str, str]) -> dict[str, str]:
    anims: dict[str, str] = {}
    for kind in ("trooper", "imp", "baron"):
        anims[f"{kind}_walk"] = write_json(
            f"animations/{kind}_walk.animset",
            {
                "filePath": f"assets/animations/{kind}_walk.animset",
                "loop": True,
                "frames": [anim_frame(tex[f"{kind}_walk0"], 0.24), anim_frame(tex[f"{kind}_walk1"], 0.24)],
            },
        )
        anims[f"{kind}_attack"] = write_json(
            f"animations/{kind}_attack.animset",
            {
                "filePath": f"assets/animations/{kind}_attack.animset",
                "loop": False,
                "frames": [anim_frame(tex[f"{kind}_attack"], 0.28)],
            },
        )
        anims[f"{kind}_hurt"] = write_json(
            f"animations/{kind}_hurt.animset",
            {
                "filePath": f"assets/animations/{kind}_hurt.animset",
                "loop": False,
                "frames": [anim_frame(tex[f"{kind}_hurt"], 0.18)],
            },
        )

    anims["fireball"] = write_json(
        "animations/fireball.animset",
        {
            "filePath": "assets/animations/fireball.animset",
            "loop": True,
            "frames": [anim_frame(tex["fireball_0"], 0.10), anim_frame(tex["fireball_1"], 0.10)],
        },
    )
    anims["plasma_bolt"] = write_json(
        "animations/plasma_bolt.animset",
        {
            "filePath": "assets/animations/plasma_bolt.animset",
            "loop": True,
            "frames": [anim_frame(tex["plasma_bolt_0"], 0.08), anim_frame(tex["plasma_bolt_1"], 0.08)],
        },
    )
    anims["hit_spark"] = write_json(
        "animations/hit_spark.animset",
        {
            "filePath": "assets/animations/hit_spark.animset",
            "loop": False,
            "frames": [anim_frame(tex[f"spark_{i}"], 0.06) for i in range(3)],
        },
    )
    anims["explosion"] = write_json(
        "animations/explosion.animset",
        {
            "filePath": "assets/animations/explosion.animset",
            "loop": False,
            "frames": [anim_frame(tex[f"explosion_{i}"], 0.11) for i in range(3)],
        },
    )
    return anims


# ---------------------------------------------------------------------------
# Helpers for Components & Templates
# ---------------------------------------------------------------------------

def comp_sprite(cid: int, tex_path: str, w: float, h: float, z: int = 0) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 6,
        "flip": 0,
        "width": w,
        "height": h,
        "zOrder": z,
        "color": {"r": 255, "g": 255, "b": 255, "a": 255},
        "sourceRect": None,
        "sourceRectEnabled": False,
        "textureFilePath": tex_path,
    }


def comp_box(cid: int, w: float, h: float, group: int, mask: int, trigger: bool = False) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 3,
        "colliderType": 1,
        "width": w,
        "height": h,
        "offset": {"x": 0.0, "y": 0.0},
        "trigger": trigger,
        "collisionGroup": group,
        "collisionMask": mask,
    }


def comp_circle(cid: int, r: float, group: int, mask: int, trigger: bool = False) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 3,
        "colliderType": 0,
        "radius": r,
        "offset": {"x": 0.0, "y": 0.0},
        "trigger": trigger,
        "collisionGroup": group,
        "collisionMask": mask,
    }


def comp_body(cid: int, body_type: int) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 4,
        "angularVelocity": 0.0,
        "bodyType": body_type,
        "force": {"x": 0.0, "y": 0.0},
        "friction": 0.0,
        "gravity": False,
        "mass": 1.0,
        "restitution": 0.0,
        "torque": 0.0,
        "velocity": {"x": 0.0, "y": 0.0},
    }


def comp_anim(cid: int, clip_path: str, playing: bool = True) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 0,
        "animationClipFilePath": clip_path,
        "currentFrameIndex": 0,
        "playbackSpeed": 1.0,
        "playing": playing,
    }


def comp_audio(cid: int, file_path: str = "", gain: float = 0.65, auto_play: bool = False, loops: int = 0) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 1,
        "autoPlay": auto_play,
        "filePath": file_path,
        "gain": gain,
        "loopCount": loops,
    }


def comp_camera(cid: int, w: float = 640.0, h: float = 360.0) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 2,
        "width": w,
        "height": h,
    }


def comp_script(cid: int, behaviors: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "id": cid,
        "type": 5,
        "behaviors": behaviors,
    }


def make_object(
    oid: int,
    name: str,
    tag: str,
    x: float,
    y: float,
    components: list[dict[str, Any]],
    rotation: float = 0.0,
    active: bool = True,
) -> dict[str, Any]:
    return {
        "id": oid,
        "name": name,
        "tag": tag,
        "position": {"x": x, "y": y},
        "scale": {"x": 1.0, "y": 1.0},
        "rotation": rotation,
        "active": active,
        "parentId": -1,
        "components": components,
        "children": [],
    }


def save_template(name: str, root_obj: dict[str, Any]) -> str:
    return write_json(
        f"templates/{name}.pttemplate",
        {
            "format": "platformator.object-template",
            "version": 1,
            "name": name,
            "root": root_obj,
            "reference_bindings": [],
        },
    )


def generate_templates(tex: dict[str, str], aud: dict[str, str], anims: dict[str, str]) -> dict[str, str]:
    tpl: dict[str, str] = {}

    # Timed FX templates
    tpl["HitSparkFx"] = save_template(
        "HitSparkFx",
        make_object(
            1,
            "HitSparkFx",
            "fx",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["spark_0"], 16.0, 16.0, 10),
                comp_anim(3, anims["hit_spark"], True),
                comp_script(
                    4,
                    [
                        {"type": "DoomBillboard", "worldHeight": 16.0, "verticalOffset": 0.5, "fullBright": True},
                        {"type": "DoomTimedFx", "lifetime": 0.18},
                    ],
                ),
            ],
        ),
    )

    tpl["ExplosionFx"] = save_template(
        "ExplosionFx",
        make_object(
            1,
            "ExplosionFx",
            "fx",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["explosion_0"], 48.0, 48.0, 10),
                comp_anim(3, anims["explosion"], True),
                comp_script(
                    4,
                    [
                        {"type": "DoomBillboard", "worldHeight": 56.0, "verticalOffset": 0.5, "fullBright": True},
                        {"type": "DoomTimedFx", "lifetime": 0.34},
                    ],
                ),
            ],
        ),
    )

    # Projectiles
    tpl["PlayerPlasma"] = save_template(
        "PlayerPlasma",
        make_object(
            1,
            "PlayerPlasma",
            "projectile",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["plasma_bolt_0"], 16.0, 16.0, 8),
                comp_circle(3, 6.0, LAYER_PROJECTILE, LAYER_WALL | LAYER_DOOR | LAYER_ENEMY | LAYER_BARREL, True),
                comp_body(4, 2),
                comp_anim(5, anims["plasma_bolt"], True),
                comp_script(
                    6,
                    [
                        {"type": "DoomBillboard", "worldHeight": 18.0, "verticalOffset": 0.5, "fullBright": True},
                        {
                            "type": "DoomProjectile",
                            "friendly": True,
                            "speed": 520.0,
                            "damage": 28,
                            "lifetime": 2.2,
                            "impactFxTemplate": tpl["HitSparkFx"],
                        },
                    ],
                ),
            ],
        ),
    )

    tpl["ImpFireball"] = save_template(
        "ImpFireball",
        make_object(
            1,
            "ImpFireball",
            "projectile",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["fireball_0"], 16.0, 16.0, 8),
                comp_circle(3, 6.0, LAYER_PROJECTILE, LAYER_WALL | LAYER_DOOR | LAYER_PLAYER, True),
                comp_body(4, 2),
                comp_anim(5, anims["fireball"], True),
                comp_script(
                    6,
                    [
                        {"type": "DoomBillboard", "worldHeight": 20.0, "verticalOffset": 0.5, "fullBright": True},
                        {
                            "type": "DoomProjectile",
                            "friendly": False,
                            "speed": 310.0,
                            "damage": 14,
                            "lifetime": 3.0,
                            "impactFxTemplate": tpl["HitSparkFx"],
                        },
                    ],
                ),
            ],
        ),
    )

    # Player template
    tpl["Player"] = save_template(
        "Player",
        make_object(
            1,
            "Player",
            "player",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["player_map"], 28.0, 28.0, 5),
                comp_circle(3, 14.0, LAYER_PLAYER, ALL_MASK, False),
                comp_body(4, 0),
                comp_audio(5, "", 0.7, False, 0),
                comp_script(
                    6,
                    [
                        {
                            "type": "DoomPlayer",
                            "moveSpeed": 165.0,
                            "sprintMultiplier": 1.45,
                            "turnSpeed": 2.4,
                            "startYaw": 0.0,
                            "maxHp": 100,
                            "hp": 100,
                            "armor": 25,
                            "ammo": 28,
                            "maxAmmo": 100,
                            "currentWeapon": 0,
                            "hasShotgun": False,
                            "hasPlasma": False,
                            "plasmaBoltTemplate": tpl["PlayerPlasma"],
                            "hitSparkTemplate": tpl["HitSparkFx"],
                            "pistolSound": aud["pistol"],
                            "shotgunSound": aud["shotgun"],
                            "plasmaSound": aud["plasma"],
                            "hurtSound": aud["player_hurt"],
                            "emptySound": aud["door_locked"],
                        }
                    ],
                ),
            ],
        ),
    )

    # BGM template
    tpl["BGM"] = save_template(
        "BGM",
        make_object(
            1,
            "BGM",
            "audio",
            0.0,
            0.0,
            [comp_audio(2, aud["bgm_e1m1"], 0.35, True, -1)],
        ),
    )

    # Pickups
    pickup_defs = [
        ("Medkit", tex["medkit"], 0, 25, 22.0, aud["pickup"]),
        ("Armor", tex["armor"], 1, 25, 22.0, aud["pickup"]),
        ("AmmoBox", tex["ammo_box"], 2, 20, 20.0, aud["pickup"]),
        ("ShotgunPickup", tex["pickup_shotgun"], 3, 12, 20.0, aud["pickup"]),
        ("PlasmaPickup", tex["pickup_plasma"], 4, 30, 20.0, aud["pickup"]),
        ("RedKeycard", tex["keycard_red"], 5, 1, 18.0, aud["keycard"]),
        ("ExitPortal", tex["exit_beacon"], 6, 1, 40.0, aud["keycard"]),
    ]
    for pname, ptex, pkind, pamt, pheight, psound in pickup_defs:
        tpl[pname] = save_template(
            pname,
            make_object(
                1,
                pname,
                "pickup",
                0.0,
                0.0,
                [
                    comp_sprite(2, ptex, 28.0, 28.0, 3),
                    comp_circle(3, 14.0, LAYER_PICKUP, LAYER_PLAYER, True),
                    comp_audio(4, "", 0.65, False, 0),
                    comp_script(
                        5,
                        [
                            {"type": "DoomBillboard", "worldHeight": pheight, "verticalOffset": 0.0, "fullBright": pkind in (5, 6)},
                            {
                                "type": "DoomPickup",
                                "pickupKind": pkind,
                                "amount": pamt,
                                "targetScenePath": "",
                                "pickupSound": psound,
                            },
                        ],
                    ),
                ],
            ),
        )

    # Barrel
    tpl["Barrel"] = save_template(
        "Barrel",
        make_object(
            1,
            "Barrel",
            "barrel",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["barrel"], 28.0, 28.0, 4),
                comp_circle(3, 13.0, LAYER_BARREL, ALL_MASK, False),
                comp_body(4, 1),
                comp_audio(5, "", 0.85, False, 0),
                comp_script(
                    6,
                    [
                        {"type": "DoomBillboard", "worldHeight": 34.0, "verticalOffset": 0.0, "fullBright": False},
                        {
                            "type": "DoomBarrel",
                            "hp": 20,
                            "blastRadius": 140.0,
                            "blastDamage": 85,
                            "explosionTemplate": tpl["ExplosionFx"],
                            "explodeSound": aud["explosion"],
                        },
                    ],
                ),
            ],
        ),
    )

    # Enemies
    enemy_defs = [
        ("Trooper", "trooper", 0, 36, 65.0, 14.0, 44.0, 1.35, 8, "", tpl["AmmoBox"], aud["pistol"]),
        ("Imp", "imp", 1, 60, 82.0, 15.0, 48.0, 1.45, 14, tpl["ImpFireball"], "", aud["plasma"]),
        ("Baron", "baron", 2, 220, 68.0, 20.0, 56.0, 1.35, 16, tpl["ImpFireball"], tpl["RedKeycard"], aud["shotgun"]),
    ]
    for ename, anim_prefix, ekind, ehp, espeed, erad, eheight, ecooldown, edmg, eproj, edrop, eattack_snd in enemy_defs:
        tpl[ename] = save_template(
            ename,
            make_object(
                1,
                ename,
                "enemy",
                0.0,
                0.0,
                [
                    comp_sprite(2, tex[f"{anim_prefix}_walk0"], 34.0, 34.0, 5),
                    comp_circle(3, erad, LAYER_ENEMY, ALL_MASK, False),
                    comp_body(4, 0),
                    comp_anim(5, anims[f"{anim_prefix}_walk"], True),
                    comp_audio(6, "", 0.65, False, 0),
                    comp_script(
                        7,
                        [
                            {"type": "DoomBillboard", "worldHeight": eheight, "verticalOffset": 0.0, "fullBright": False},
                            {
                                "type": "DoomEnemy",
                                "enemyKind": ekind,
                                "hp": ehp,
                                "maxHp": ehp,
                                "moveSpeed": espeed,
                                "detectRange": 520.0,
                                "attackRange": 380.0,
                                "attackCooldown": ecooldown,
                                "attackDamage": edmg,
                                "fireballTemplate": eproj,
                                "dropTemplate": edrop,
                                "deathFxTemplate": tpl["HitSparkFx"],
                                "walkAnim": anims[f"{anim_prefix}_walk"],
                                "attackAnim": anims[f"{anim_prefix}_attack"],
                                "hurtAnim": anims[f"{anim_prefix}_hurt"],
                                "alertSound": aud["enemy_alert"],
                                "attackSound": eattack_snd,
                                "hurtSound": aud["enemy_hurt"],
                                "deathSound": aud["enemy_death"],
                            },
                        ],
                    ),
                ],
            ),
        )

    # Architecture: Walls, Pillar, Doors
    for wname, wtex in (
        ("WallTech", tex["wall_tech"]),
        ("WallBrick", tex["wall_brick"]),
        ("WallComp", tex["wall_comp"]),
        ("WallExit", tex["wall_exit"]),
    ):
        tpl[wname] = save_template(
            wname,
            make_object(
                1,
                wname,
                "wall",
                0.0,
                0.0,
                [
                    comp_sprite(2, wtex, 64.0, 32.0, 1),
                    comp_box(3, 64.0, 32.0, LAYER_WALL, ALL_MASK, False),
                    comp_body(4, 1),
                ],
            ),
        )

    tpl["Pillar"] = save_template(
        "Pillar",
        make_object(
            1,
            "Pillar",
            "wall",
            0.0,
            0.0,
            [
                comp_sprite(2, tex["wall_pillar"], 44.0, 44.0, 2),
                comp_circle(3, 22.0, LAYER_WALL, ALL_MASK, False),
                comp_body(4, 1),
            ],
        ),
    )

    for dname, dtex, dkey in (("Door", tex["wall_door"], False), ("DoorRed", tex["wall_door_red"], True)):
        tpl[dname] = save_template(
            dname,
            make_object(
                1,
                dname,
                "door",
                0.0,
                0.0,
                [
                    comp_sprite(2, dtex, 68.0, 16.0, 3),
                    comp_box(3, 68.0, 16.0, LAYER_DOOR, ALL_MASK, False),
                    comp_body(4, 2),
                    comp_audio(5, "", 0.7, False, 0),
                    comp_script(
                        6,
                        [
                            {
                                "type": "DoomDoor",
                                "slideDirX": 1.0,
                                "slideDirY": 0.0,
                                "slideDistance": 62.0,
                                "slideSpeed": 140.0,
                                "requiresRedKey": dkey,
                                "autoCloseDelay": 4.5,
                                "doorSound": aud["door"],
                                "lockedSound": aud["door_locked"],
                            }
                        ],
                    ),
                ],
            ),
        )

    return tpl


# ---------------------------------------------------------------------------
# Scene Builder Helpers
# ---------------------------------------------------------------------------

class SceneBuilder:
    def __init__(self, tex: dict[str, str], aud: dict[str, str], anims: dict[str, str]) -> None:
        self.tex = tex
        self.aud = aud
        self.anims = anims
        self.next_id = 1
        self.objects: list[dict[str, Any]] = []

    def alloc_id(self) -> int:
        val = self.next_id
        self.next_id += 1
        return val

    def add_wall(
        self,
        name: str,
        x: float,
        y: float,
        w: float,
        h: float,
        wall_tex_key: str,
        rot: float = 0.0,
    ) -> int:
        oid = self.alloc_id()
        self.objects.append(
            make_object(
                oid,
                name,
                "wall",
                x,
                y,
                [
                    comp_sprite(self.alloc_id(), self.tex[wall_tex_key], w, h, 1),
                    comp_box(self.alloc_id(), w, h, LAYER_WALL, ALL_MASK, False),
                    comp_body(self.alloc_id(), 1),
                ],
                rotation=rot,
            )
        )
        return oid

    def add_pillar(self, name: str, x: float, y: float, radius: float = 22.0) -> int:
        oid = self.alloc_id()
        self.objects.append(
            make_object(
                oid,
                name,
                "wall",
                x,
                y,
                [
                    comp_sprite(self.alloc_id(), self.tex["wall_pillar"], radius * 2.0, radius * 2.0, 2),
                    comp_circle(self.alloc_id(), radius, LAYER_WALL, ALL_MASK, False),
                    comp_body(self.alloc_id(), 1),
                ],
            )
        )
        return oid

    def add_door(
        self,
        name: str,
        x: float,
        y: float,
        w: float,
        h: float,
        slide_dx: float,
        slide_dy: float,
        requires_key: bool = False,
    ) -> int:
        oid = self.alloc_id()
        self.objects.append(
            make_object(
                oid,
                name,
                "door",
                x,
                y,
                [
                    comp_sprite(self.alloc_id(), self.tex["wall_door_red" if requires_key else "wall_door"], w, h, 3),
                    comp_box(self.alloc_id(), w, h, LAYER_DOOR, ALL_MASK, False),
                    comp_body(self.alloc_id(), 2),
                    comp_audio(self.alloc_id(), "", 0.7, False, 0),
                    comp_script(
                        self.alloc_id(),
                        [
                            {
                                "type": "DoomDoor",
                                "slideDirX": slide_dx,
                                "slideDirY": slide_dy,
                                "slideDistance": 62.0,
                                "slideSpeed": 140.0,
                                "requiresRedKey": requires_key,
                                "autoCloseDelay": 4.5,
                                "doorSound": self.aud["door"],
                                "lockedSound": self.aud["door_locked"],
                            }
                        ],
                    ),
                ],
            )
        )
        return oid

    def add_barrel(self, name: str, x: float, y: float) -> int:
        oid = self.alloc_id()
        self.objects.append(
            make_object(
                oid,
                name,
                "barrel",
                x,
                y,
                [
                    comp_sprite(self.alloc_id(), self.tex["barrel"], 28.0, 28.0, 4),
                    comp_circle(self.alloc_id(), 13.0, LAYER_BARREL, ALL_MASK, False),
                    comp_body(self.alloc_id(), 1),
                    comp_audio(self.alloc_id(), "", 0.85, False, 0),
                    comp_script(
                        self.alloc_id(),
                        [
                            {"type": "DoomBillboard", "worldHeight": 34.0, "verticalOffset": 0.0, "fullBright": False},
                            {
                                "type": "DoomBarrel",
                                "hp": 20,
                                "blastRadius": 140.0,
                                "blastDamage": 85,
                                "explosionTemplate": "assets/templates/ExplosionFx.pttemplate",
                                "explodeSound": self.aud["explosion"],
                            },
                        ],
                    ),
                ],
            )
        )
        return oid

    def add_pickup(
        self,
        name: str,
        x: float,
        y: float,
        kind: int,
        amount: int,
        target_scene: str = "",
    ) -> int:
        tex_map = {
            0: (self.tex["medkit"], 22.0, False, self.aud["pickup"]),
            1: (self.tex["armor"], 22.0, False, self.aud["pickup"]),
            2: (self.tex["ammo_box"], 20.0, False, self.aud["pickup"]),
            3: (self.tex["pickup_shotgun"], 20.0, False, self.aud["pickup"]),
            4: (self.tex["pickup_plasma"], 20.0, False, self.aud["pickup"]),
            5: (self.tex["keycard_red"], 18.0, True, self.aud["keycard"]),
            6: (self.tex["exit_beacon"], 40.0, True, self.aud["keycard"]),
        }
        ptex, wh, bright, psnd = tex_map[kind]
        oid = self.alloc_id()
        self.objects.append(
            make_object(
                oid,
                name,
                "pickup",
                x,
                y,
                [
                    comp_sprite(self.alloc_id(), ptex, 28.0, 28.0, 3),
                    comp_circle(self.alloc_id(), 14.0, LAYER_PICKUP, LAYER_PLAYER, True),
                    comp_audio(self.alloc_id(), "", 0.65, False, 0),
                    comp_script(
                        self.alloc_id(),
                        [
                            {"type": "DoomBillboard", "worldHeight": wh, "verticalOffset": 0.0, "fullBright": bright},
                            {
                                "type": "DoomPickup",
                                "pickupKind": kind,
                                "amount": amount,
                                "targetScenePath": target_scene,
                                "pickupSound": psnd,
                            },
                        ],
                    ),
                ],
            )
        )
        return oid

    def add_enemy(
        self,
        name: str,
        x: float,
        y: float,
        kind: int,
        hp_override: int | None = None,
    ) -> int:
        if kind == 0:
            prefix, hp, speed, rad, wh, cd, dmg, proj, drop, snd = (
                "trooper", 36, 65.0, 14.0, 44.0, 1.35, 8, "", "assets/templates/AmmoBox.pttemplate", self.aud["pistol"]
            )
        elif kind == 1:
            prefix, hp, speed, rad, wh, cd, dmg, proj, drop, snd = (
                "imp", 60, 82.0, 15.0, 48.0, 1.45, 14, "assets/templates/ImpFireball.pttemplate", "", self.aud["plasma"]
            )
        else:
            prefix, hp, speed, rad, wh, cd, dmg, proj, drop, snd = (
                "baron", 200, 68.0, 20.0, 56.0, 1.35, 16, "assets/templates/ImpFireball.pttemplate",
                "assets/templates/RedKeycard.pttemplate", self.aud["shotgun"]
            )
        if hp_override is not None:
            hp = hp_override
        oid = self.alloc_id()
        self.objects.append(
            make_object(
                oid,
                name,
                "enemy",
                x,
                y,
                [
                    comp_sprite(self.alloc_id(), self.tex[f"{prefix}_walk0"], 34.0, 34.0, 5),
                    comp_circle(self.alloc_id(), rad, LAYER_ENEMY, ALL_MASK, False),
                    comp_body(self.alloc_id(), 0),
                    comp_anim(self.alloc_id(), self.anims[f"{prefix}_walk"], True),
                    comp_audio(self.alloc_id(), "", 0.65, False, 0),
                    comp_script(
                        self.alloc_id(),
                        [
                            {"type": "DoomBillboard", "worldHeight": wh, "verticalOffset": 0.0, "fullBright": False},
                            {
                                "type": "DoomEnemy",
                                "enemyKind": kind,
                                "hp": hp,
                                "maxHp": hp,
                                "moveSpeed": speed,
                                "detectRange": 520.0,
                                "attackRange": 380.0,
                                "attackCooldown": cd,
                                "attackDamage": dmg,
                                "fireballTemplate": proj,
                                "dropTemplate": drop,
                                "deathFxTemplate": "assets/templates/HitSparkFx.pttemplate",
                                "walkAnim": self.anims[f"{prefix}_walk"],
                                "attackAnim": self.anims[f"{prefix}_attack"],
                                "hurtAnim": self.anims[f"{prefix}_hurt"],
                                "alertSound": self.aud["enemy_alert"],
                                "attackSound": snd,
                                "hurtSound": self.aud["enemy_hurt"],
                                "deathSound": self.aud["enemy_death"],
                            },
                        ],
                    ),
                ],
            )
        )
        return oid


def build_core_objects(
    sb: SceneBuilder,
    title: str,
    next_scene: str,
    level_index: int,
    bgm_path: str,
    player_x: float,
    player_y: float,
    player_yaw: float,
) -> None:
    gm_id = sb.alloc_id()
    gm_script_id = sb.alloc_id()
    bgm_id = sb.alloc_id()
    bgm_audio_id = sb.alloc_id()
    player_id = sb.alloc_id()
    cam_id = sb.alloc_id()
    cam_comp_id = sb.alloc_id()

    # GameManager goes FIRST so its start() runs before other behaviors in Phase::Starting
    sb.objects.append(
        make_object(
            gm_id,
            "GameManager",
            "manager",
            0.0,
            0.0,
            [
                comp_script(
                    gm_script_id,
                    [
                        {
                            "type": "DoomGameManager",
                            "levelTitle": title,
                            "nextScenePath": next_scene,
                            "firstScenePath": "assets/scenes/e1m1_hangar.scene",
                            "levelIndex": level_index,
                            "player": player_id,
                            "camera": cam_comp_id,
                            "bgm": bgm_audio_id,
                        }
                    ],
                )
            ],
        )
    )

    sb.objects.append(
        make_object(
            bgm_id,
            "BGM",
            "audio",
            0.0,
            0.0,
            [comp_audio(bgm_audio_id, bgm_path, 0.30, True, -1)],
        )
    )

    # Player goes BEFORE Main Camera / DoomRaycaster so player input updates before column raycasts
    sb.objects.append(
        make_object(
            player_id,
            "Player",
            "player",
            player_x,
            player_y,
            [
                comp_sprite(sb.alloc_id(), sb.tex["player_map"], 28.0, 28.0, 5),
                comp_circle(sb.alloc_id(), 14.0, LAYER_PLAYER, ALL_MASK, False),
                comp_body(sb.alloc_id(), 0),
                comp_audio(sb.alloc_id(), "", 0.70, False, 0),
                comp_script(
                    sb.alloc_id(),
                    [
                        {
                            "type": "DoomPlayer",
                            "moveSpeed": 165.0,
                            "sprintMultiplier": 1.45,
                            "turnSpeed": 2.4,
                            "startYaw": player_yaw,
                            "maxHp": 100,
                            "hp": 100,
                            "armor": 25,
                            "ammo": 28,
                            "maxAmmo": 100,
                            "currentWeapon": 0,
                            "hasShotgun": False,
                            "hasPlasma": False,
                            "plasmaBoltTemplate": "assets/templates/PlayerPlasma.pttemplate",
                            "hitSparkTemplate": "assets/templates/HitSparkFx.pttemplate",
                            "pistolSound": sb.aud["pistol"],
                            "shotgunSound": sb.aud["shotgun"],
                            "plasmaSound": sb.aud["plasma"],
                            "hurtSound": sb.aud["player_hurt"],
                            "emptySound": sb.aud["door_locked"],
                        }
                    ],
                ),
            ],
            rotation=player_yaw,
        )
    )

    sb.objects.append(
        make_object(
            cam_id,
            "Main Camera",
            "camera",
            3200.0,
            0.0,
            [
                comp_camera(cam_comp_id, 640.0, 360.0),
                comp_script(
                    sb.alloc_id(),
                    [
                        {
                            "type": "DoomRaycaster",
                            "camera": cam_comp_id,
                            "ceilingTex": sb.tex["ceiling"],
                            "floorTex": sb.tex["floor"],
                            "hudBarTex": sb.tex["hud_bar"],
                            "digitsTex": sb.tex["digits"],
                            "faceTex": sb.tex["face_strip"],
                            "crosshairTex": sb.tex["crosshair"],
                            "keyIconTex": sb.tex["keycard_red"],
                            "whiteTex": sb.tex["white"],
                            "defaultWallTex": sb.tex["wall_tech"],
                            "pistolTex": sb.tex["pistol_idle"],
                            "pistolFireTex": sb.tex["pistol_fire"],
                            "shotgunTex": sb.tex["shotgun_idle"],
                            "shotgunFireTex": sb.tex["shotgun_fire"],
                            "plasmaTex": sb.tex["plasma_idle"],
                            "plasmaFireTex": sb.tex["plasma_fire"],
                        }
                    ],
                ),
            ],
        )
    )


def build_e1m1_scene(tex: dict[str, str], aud: dict[str, str], anims: dict[str, str]) -> str:
    sb = SceneBuilder(tex, aud, anims)
    # Player faces straight East-Northeast toward trooper & barrel cluster in the bay
    build_core_objects(
        sb,
        "DOOM E1M1 - Phobos Hangar",
        "assets/scenes/e1m2_reactor.scene",
        1,
        aud["bgm_e1m1"],
        120.0,
        700.0,
        -0.45,
    )

    # Outer enclosed perimeter [0..1024] x [0..896] (overlapping corners so 0 ray leaks)
    sb.add_wall("WallNorth", 512.0, 16.0, 1056.0, 32.0, "wall_tech")
    sb.add_wall("WallSouth", 512.0, 880.0, 1056.0, 32.0, "wall_tech")
    sb.add_wall("WallWest", 16.0, 448.0, 32.0, 896.0, "wall_tech")
    sb.add_wall("WallEast", 1008.0, 448.0, 32.0, 896.0, "wall_tech")

    # SW Bay -> North Tech Hall wall at Y=448 with doorway centered at X=240
    sb.add_wall("DivSW_Left", 120.0, 448.0, 192.0, 32.0, "wall_tech")
    sb.add_wall("DivSW_Right", 368.0, 448.0, 192.0, 32.0, "wall_tech")
    sb.add_door("DoorNorth", 240.0, 448.0, 68.0, 16.0, 1.0, 0.0, False)

    # SW Bay -> SE Vault wall at X=448 with doorway centered at Y=660
    sb.add_wall("DivBay_Top", 448.0, 530.0, 32.0, 196.0, "wall_brick")
    sb.add_wall("DivBay_Bot", 448.0, 786.0, 32.0, 196.0, "wall_brick")

    # SE Key Vault -> NE Exit Chamber horizontal separator wall at Y=448 (X=448..992)
    sb.add_wall("DivEastMid", 728.0, 448.0, 560.0, 32.0, "wall_brick")

    # North Tech Hall -> NE Exit Chamber wall at X=704 with Red Keycard locked door at Y=240
    sb.add_wall("DivNE_Top", 704.0, 120.0, 32.0, 200.0, "wall_comp")
    sb.add_wall("DivNE_Bot", 704.0, 360.0, 32.0, 200.0, "wall_comp")
    sb.add_door("DoorRed", 704.0, 240.0, 16.0, 68.0, 0.0, -1.0, True)

    # Angled 45-degree rotated bunker & alcove walls (showcasing rotated BoxCollider raycasts)
    sb.add_wall("AngledBunker_SW", 280.0, 760.0, 128.0, 24.0, "wall_comp", rot=-0.785398)
    sb.add_wall("AngledComp_SE", 840.0, 550.0, 128.0, 24.0, "wall_comp", rot=0.785398)

    # Round CircleCollider pillars
    sb.add_pillar("Pillar_SW", 150.0, 560.0, 22.0)
    sb.add_pillar("Pillar_NorthA", 200.0, 200.0, 24.0)
    sb.add_pillar("Pillar_NorthB", 460.0, 200.0, 24.0)

    # Exit sign panel on East wall inside NE Exit Chamber
    sb.add_wall("WallExitPanel", 990.0, 240.0, 16.0, 64.0, "wall_exit")

    # Explosive Barrels (near player start sightline + SE vault chain cluster)
    sb.add_barrel("Barrel_SW", 250.0, 600.0)
    sb.add_barrel("Barrel_SE_A", 650.0, 640.0)
    sb.add_barrel("Barrel_SE_B", 688.0, 660.0)

    # Enemies
    sb.add_enemy("Trooper_SW", 310.0, 590.0, 0)
    sb.add_enemy("Trooper_SE", 640.0, 740.0, 0)
    sb.add_enemy("Imp_SE", 820.0, 690.0, 1)
    sb.add_enemy("Imp_North", 340.0, 140.0, 1)
    sb.add_enemy("Trooper_North", 580.0, 260.0, 0)
    sb.add_enemy("Imp_Exit", 850.0, 160.0, 1)

    # Pickups & Exit
    sb.add_pickup("Ammo_Start", 80.0, 790.0, 2, 20)
    sb.add_pickup("Medkit_SW", 370.0, 790.0, 0, 25)
    sb.add_pickup("ShotgunPickup", 540.0, 660.0, 3, 12)
    sb.add_pickup("Medkit_SE", 920.0, 790.0, 0, 25)
    sb.add_pickup("RedKeycard", 910.0, 530.0, 5, 1)
    sb.add_pickup("Armor_North", 100.0, 100.0, 1, 50)
    sb.add_pickup("Ammo_North", 580.0, 100.0, 2, 24)
    sb.add_pickup("ExitPortal", 910.0, 240.0, 6, 1, "assets/scenes/e1m2_reactor.scene")

    return write_json("scenes/e1m1_hangar.scene", sb.objects)


def build_e1m2_scene(tex: dict[str, str], aud: dict[str, str], anims: dict[str, str]) -> str:
    sb = SceneBuilder(tex, aud, anims)
    build_core_objects(
        sb,
        "DOOM E1M2 - Reactor Core",
        "assets/scenes/e1m1_hangar.scene",
        2,
        aud["bgm_e1m2"],
        120.0,
        480.0,
        0.0,
    )

    # Outer enclosed perimeter [0..1152] x [0..960]
    sb.add_wall("WallNorth", 576.0, 16.0, 1184.0, 32.0, "wall_brick")
    sb.add_wall("WallSouth", 576.0, 944.0, 1184.0, 32.0, "wall_brick")
    sb.add_wall("WallWest", 16.0, 480.0, 32.0, 960.0, "wall_tech")
    sb.add_wall("WallEast", 1136.0, 480.0, 32.0, 960.0, "wall_tech")

    # Entry Hall divider at X=320 with sliding Door at Y=480
    sb.add_wall("DivEntry_Top", 320.0, 240.0, 32.0, 448.0, "wall_tech")
    sb.add_wall("DivEntry_Bot", 320.0, 720.0, 32.0, 448.0, "wall_tech")
    sb.add_door("DoorEntry", 320.0, 480.0, 16.0, 68.0, 0.0, -1.0, False)

    # Exit Vault divider at X=896 with locked DoorRed at Y=480
    sb.add_wall("DivExit_Top", 896.0, 240.0, 32.0, 448.0, "wall_comp")
    sb.add_wall("DivExit_Bot", 896.0, 720.0, 32.0, 448.0, "wall_comp")
    sb.add_door("DoorRed", 896.0, 480.0, 16.0, 68.0, 0.0, -1.0, True)

    # Octagonal angled corner walls inside Central Reactor Chamber (X=320..896, Y=32..928)
    sb.add_wall("Oct_NW", 390.0, 110.0, 160.0, 28.0, "wall_comp", rot=-0.785398)
    sb.add_wall("Oct_NE", 826.0, 110.0, 160.0, 28.0, "wall_comp", rot=0.785398)
    sb.add_wall("Oct_SW", 390.0, 850.0, 160.0, 28.0, "wall_comp", rot=0.785398)
    sb.add_wall("Oct_SE", 826.0, 850.0, 160.0, 28.0, "wall_comp", rot=-0.785398)

    # Four cylindrical reactor core pillars
    sb.add_pillar("ReactorPillar_NW", 495.0, 330.0, 24.0)
    sb.add_pillar("ReactorPillar_NE", 720.0, 330.0, 24.0)
    sb.add_pillar("ReactorPillar_SW", 495.0, 630.0, 24.0)
    sb.add_pillar("ReactorPillar_SE", 720.0, 630.0, 24.0)

    # Exit sign panel on far East wall
    sb.add_wall("WallExitPanel", 1118.0, 480.0, 16.0, 64.0, "wall_exit")

    # Chained explosive barrels in the Reactor Core
    sb.add_barrel("Barrel_Core_1", 560.0, 465.0)
    sb.add_barrel("Barrel_Core_2", 590.0, 500.0)
    sb.add_barrel("Barrel_Core_3", 625.0, 465.0)

    # Enemies + Reactor Baron Boss (drops RedKeycard on death!)
    sb.add_enemy("Trooper_Entry", 230.0, 390.0, 0)
    sb.add_enemy("Imp_North", 605.0, 200.0, 1)
    sb.add_enemy("Imp_South", 605.0, 760.0, 1)
    sb.add_enemy("ReactorBaron", 755.0, 480.0, 2, 180)

    # Arsenal & supplies
    sb.add_pickup("ShotgunPickup", 170.0, 360.0, 3, 12)
    sb.add_pickup("PlasmaPickup", 170.0, 600.0, 4, 40)
    sb.add_pickup("Armor_Entry", 90.0, 280.0, 1, 50)
    sb.add_pickup("Medkit_Entry", 90.0, 680.0, 0, 25)
    sb.add_pickup("Ammo_Reactor_N", 605.0, 330.0, 2, 24)
    sb.add_pickup("Medkit_Reactor_S", 605.0, 630.0, 0, 25)
    sb.add_pickup("Ammo_Vault", 970.0, 380.0, 2, 24)
    sb.add_pickup("ExitPortal", 1040.0, 480.0, 6, 1, "assets/scenes/e1m1_hangar.scene")

    return write_json("scenes/e1m2_reactor.scene", sb.objects)


def main() -> None:
    tex = generate_textures()
    aud = generate_audio()
    anims = generate_animsets(tex)
    tpl = generate_templates(tex, aud, anims)
    s1 = build_e1m1_scene(tex, aud, anims)
    s2 = build_e1m2_scene(tex, aud, anims)
    print(f"Generated {len(tex)} textures, {len(aud)} audio files, {len(anims)} animsets, "
          f"{len(tpl)} templates, scenes: {s1}, {s2}")


if __name__ == "__main__":
    main()
