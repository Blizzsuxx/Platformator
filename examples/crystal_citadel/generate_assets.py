#!/usr/bin/env python3
"""Generate procedural pixel-art sprites, synthesized audio, animsets,
object templates (.pttemplate), and 4 playable levels (.scene) for Crystal Citadel.
Uses only Python 3 standard library modules.
"""

from __future__ import annotations

import json
import math
import os
import struct
import wave
import zlib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
ASSETS_DIR = ROOT / "assets"

LAYER_DEFAULT = 1 << 0
LAYER_PLAYER = 1 << 1
LAYER_ENEMY = 1 << 2
LAYER_PLATFORMS = 1 << 3
LAYER_PROJECTILE = 1 << 4
LAYER_COLLECTIBLE = 1 << 5
LAYER_HAZARD = 1 << 6
ALL_MASK = 4294967295


# ---------------------------------------------------------------------------
# Pure-stdlib RGBA PNG & PCM WAV writers
# ---------------------------------------------------------------------------

def encode_png(width: int, height: int, pixels: bytearray) -> bytes:
    """Encode raw RGBA bytes (len == width * height * 4) into a valid PNG."""
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

    def ring(self, cx: float, cy: float, r_outer: float, r_inner: float, rgba: tuple[int, int, int, int]) -> None:
        ro2 = r_outer * r_outer
        ri2 = r_inner * r_inner
        for y in range(int(math.floor(cy - r_outer)), int(math.ceil(cy + r_outer)) + 1):
            for x in range(int(math.floor(cx - r_outer)), int(math.ceil(cx + r_outer)) + 1):
                d2 = (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2
                if ri2 <= d2 <= ro2:
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
                        if self.buf[(ny * self.w + nx) * 4 + 3] > 180:
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
        clamped = max(-0.95, min(0.95, s))
        pcm.extend(struct.pack("<h", int(clamped * 32767)))
    with wave.open(str(full), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(pcm))
    return f"assets/{rel_path}"


def synth_sfx(
    duration: float,
    freq_start: float,
    freq_end: float,
    waveform: str = "sine",
    volume: float = 0.45,
    sr: int = 22050,
) -> list[float]:
    n = max(1, int(duration * sr))
    out: list[float] = []
    phase = 0.0
    for i in range(n):
        t = i / n
        f = freq_start + (freq_end - freq_start) * t
        phase += 2.0 * math.pi * f / sr
        env = min(1.0, t * 14.0) * ((1.0 - t) ** 1.4)
        if waveform == "square":
            v = 1.0 if math.sin(phase) >= 0.0 else -1.0
        elif waveform == "tri":
            v = (2.0 / math.pi) * math.asin(math.sin(phase))
        elif waveform == "noise":
            v = math.sin(phase * 1.7) * 0.5 + math.sin(i * 12.9898) * 0.5
        else:
            v = math.sin(phase)
        out.append(v * env * volume)
    return out


def synth_bgm(notes: list[float], bpm: float = 128.0, volume: float = 0.22, sr: int = 22050) -> list[float]:
    beat_sec = 60.0 / bpm / 2.0
    step_samples = int(beat_sec * sr)
    out: list[float] = []
    phase = 0.0
    bass_phase = 0.0
    for idx, freq in enumerate(notes):
        bass_freq = freq * 0.5 if (idx % 2 == 0) else notes[max(0, idx - 1)] * 0.5
        for i in range(step_samples):
            t = i / step_samples
            phase += 2.0 * math.pi * freq / sr
            bass_phase += 2.0 * math.pi * bass_freq / sr
            env = math.sin(math.pi * min(1.0, t * 5.0)) * ((1.0 - 0.5 * t))
            lead = (2.0 / math.pi) * math.asin(math.sin(phase)) * 0.6 + math.sin(phase * 2.0) * 0.2
            bass = (1.0 if math.sin(bass_phase) >= 0 else -1.0) * 0.25
            out.append((lead * env + bass * 0.5) * volume)
    return out


# ---------------------------------------------------------------------------
# Procedural sprite & audio builders
# ---------------------------------------------------------------------------

def build_wizard(frame_kind: str, step: int = 0) -> Canvas:
    c = Canvas(32, 40)
    outline = (16, 18, 34, 255)
    robe_dark = (32, 62, 130, 255)
    robe_mid = (52, 102, 198, 255)
    robe_hi = (92, 156, 238, 255)
    trim = (242, 198, 78, 255)
    visor = (90, 245, 255, 255)
    boot = (56, 42, 72, 255)
    bob = 1 if (frame_kind == "idle" and step == 1) else 0
    if frame_kind == "jump":
        bob = -2

    # Pointed wizard hood & cowl
    for row in range(10):
        w = 4 + row * 2
        x0 = 16 - w // 2
        col = robe_hi if row < 4 else robe_mid
        c.rect(x0, 3 + row + bob, w, 1, col)
    c.rect(7, 12 + bob, 18, 6, robe_dark)
    # Glowing visor eyes
    c.rect(11, 13 + bob, 10, 4, (14, 18, 38, 255))
    c.rect(13, 14 + bob, 2, 2, visor)
    c.rect(18, 14 + bob, 2, 2, visor)

    # Torso & cloak
    c.rect(9, 18 + bob, 14, 13, robe_mid)
    c.rect(10, 18 + bob, 5, 11, robe_hi)
    c.rect(9, 25 + bob, 14, 2, trim)
    if frame_kind == "fall":
        c.rect(7, 26 + bob, 18, 5, robe_dark)

    # Crystal staff on right side
    c.rect(25, 11 + bob, 2, 22, (145, 95, 58, 255))
    c.disc(26.0, 9.5 + bob, 3.2, (70, 245, 255, 255))
    c.px(25, 8 + bob, (235, 255, 255, 255))

    # Boots
    if frame_kind == "run":
        l_off = -3 if step == 0 else (3 if step == 2 else 0)
        r_off = 3 if step == 0 else (-3 if step == 2 else 0)
        c.rect(10 + l_off, 32, 5, 6, boot)
        c.rect(17 + r_off, 32, 5, 6, boot)
    elif frame_kind == "jump":
        c.rect(10, 30, 5, 5, boot)
        c.rect(17, 29, 5, 5, boot)
    else:
        c.rect(10, 32, 5, 6, boot)
        c.rect(17, 32, 5, 6, boot)

    c.outline(outline)
    return c


def build_crawler(mode: str, step: int = 0) -> Canvas:
    c = Canvas(32, 28)
    outline = (22, 12, 24, 255)
    shell = (76, 46, 98, 255)
    shell_hi = (128, 76, 162, 255)
    crystal = (255, 162, 62, 255)
    if mode == "squash":
        c.rect(3, 18, 26, 8, shell)
        c.rect(7, 17, 18, 3, shell_hi)
        c.rect(14, 15, 4, 4, crystal)
        c.outline(outline)
        return c

    shift = -1 if step == 1 else 1
    c.disc(16.0, 15.0, 10.5, shell)
    c.rect(9, 8, 14, 7, shell_hi)
    # Dorsal crystal shard
    c.rect(14, 3, 4, 6, crystal)
    c.px(15, 2, (255, 232, 160, 255))
    # Amber eyes
    c.rect(8, 14, 3, 3, (255, 215, 70, 255))
    c.rect(13, 14, 3, 3, (255, 215, 70, 255))
    # Legs
    for lx in (7, 14, 21):
        c.rect(lx + shift, 23, 3, 4, (52, 32, 68, 255))
    c.outline(outline)
    return c


def build_slime(step: int) -> Canvas:
    c = Canvas(32, 28)
    squish = 2 if step == 1 else 0
    outer = (44, 210, 138, 235)
    inner = (130, 255, 195, 255)
    core = (225, 255, 242, 255)
    y_top = 7 + squish
    h = 19 - squish
    for y in range(y_top, y_top + h):
        frac = (y - y_top) / max(1, h - 1)
        half_w = int(6 + 8 * math.sin(frac * math.pi * 0.68))
        c.rect(16 - half_w, y, half_w * 2, 1, outer)
    c.disc(16.0, 16.0 + squish * 0.5, 5.5, inner)
    c.disc(15.0, 14.5 + squish * 0.5, 2.5, core)
    # Eyes
    c.rect(11, 14 + squish, 2, 3, (15, 38, 28, 255))
    c.rect(19, 14 + squish, 2, 3, (15, 38, 28, 255))
    c.outline((16, 42, 32, 255))
    return c


def build_turret_base() -> Canvas:
    c = Canvas(32, 32)
    c.rect(4, 24, 24, 6, (56, 60, 78, 255))
    c.rect(7, 12, 18, 12, (82, 90, 118, 255))
    c.rect(10, 6, 12, 8, (182, 140, 68, 255))
    c.disc(16.0, 18.0, 4.0, (80, 235, 255, 255))
    c.outline((18, 20, 32, 255))
    return c


def build_turret_barrel() -> Canvas:
    c = Canvas(32, 16)
    c.rect(4, 4, 22, 8, (150, 115, 62, 255))
    c.rect(8, 5, 16, 4, (206, 166, 92, 255))
    c.rect(25, 3, 5, 10, (118, 86, 188, 255))
    c.rect(27, 5, 3, 6, (255, 110, 145, 255))
    c.outline((24, 16, 36, 255))
    return c


def build_archon_body() -> Canvas:
    c = Canvas(64, 64)
    c.disc(32.0, 32.0, 24.0, (68, 34, 112, 255))
    c.disc(32.0, 30.0, 18.0, (108, 54, 168, 255))
    # Armor plates
    c.rect(10, 22, 10, 22, (48, 52, 76, 255))
    c.rect(44, 22, 10, 22, (48, 52, 76, 255))
    c.rect(20, 16, 24, 6, (218, 178, 84, 255))
    # Core & glowing eyes
    c.disc(32.0, 36.0, 8.0, (90, 240, 255, 255))
    c.disc(32.0, 36.0, 4.0, (240, 255, 255, 255))
    c.rect(23, 24, 5, 4, (255, 92, 130, 255))
    c.rect(36, 24, 5, 4, (255, 92, 130, 255))
    c.outline((20, 10, 32, 255))
    return c


def build_archon_crown() -> Canvas:
    c = Canvas(40, 24)
    c.rect(6, 14, 28, 6, (226, 182, 74, 255))
    for tip_x, h in ((8, 10), (18, 14), (28, 10)):
        c.rect(tip_x, 14 - h, 4, h, (250, 212, 106, 255))
    c.disc(20.0, 8.0, 3.5, (186, 98, 255, 255))
    c.outline((28, 16, 36, 255))
    return c


def build_archon_orb() -> Canvas:
    c = Canvas(22, 22)
    c.disc(11.0, 11.0, 8.5, (148, 62, 218, 255))
    c.ring(11.0, 11.0, 8.5, 6.2, (242, 182, 92, 255))
    c.disc(11.0, 11.0, 4.0, (255, 112, 158, 255))
    c.outline((26, 12, 36, 255))
    return c


def build_gem(frame: int) -> Canvas:
    c = Canvas(24, 24)
    widths = (8, 5, 3, 5)
    hw = widths[frame % 4]
    col_main = (65, 232, 255, 255)
    col_hi = (210, 252, 255, 255)
    col_shadow = (128, 92, 245, 255)
    for dy in range(-8, 9):
        w = max(1, int(hw * (1.0 - abs(dy) / 9.0)))
        for dx in range(-w, w + 1):
            col = col_hi if dx < 0 and dy < 0 else (col_shadow if dx > 0 else col_main)
            c.px(12 + dx, 12 + dy, col)
    c.outline((22, 24, 52, 255))
    return c


def build_shield_core(step: int) -> Canvas:
    c = Canvas(24, 24)
    r = 8.5 + (0.8 if step == 1 else 0.0)
    c.disc(12.0, 12.0, r, (74, 210, 255, 255))
    c.ring(12.0, 12.0, r, r - 2.2, (248, 216, 98, 255))
    c.disc(12.0, 12.0, 3.8, (255, 255, 255, 255))
    c.outline((24, 30, 56, 255))
    return c


def build_orbit_shield() -> Canvas:
    c = Canvas(18, 18)
    c.disc(9.0, 9.0, 7.2, (92, 242, 255, 235))
    c.ring(9.0, 9.0, 7.2, 4.8, (248, 212, 88, 255))
    c.outline((20, 28, 52, 255))
    return c


def build_crate() -> Canvas:
    c = Canvas(32, 32)
    c.rect(2, 2, 28, 28, (132, 86, 48, 255))
    c.rect(5, 5, 22, 22, (166, 112, 64, 255))
    # Brass corner trim & diagonal crossbar
    c.rect(2, 2, 28, 4, (206, 166, 78, 255))
    c.rect(2, 26, 28, 4, (206, 166, 78, 255))
    c.rect(2, 2, 4, 28, (206, 166, 78, 255))
    c.rect(26, 2, 4, 28, (206, 166, 78, 255))
    for i in range(6, 26):
        c.rect(i, i, 2, 2, (112, 70, 38, 255))
        c.rect(i, 30 - i, 2, 2, (112, 70, 38, 255))
    c.disc(16.0, 16.0, 3.5, (80, 236, 255, 255))
    c.outline((28, 18, 14, 255))
    return c


def build_portal(step: int) -> Canvas:
    c = Canvas(40, 56)
    c.rect(4, 4, 32, 48, (54, 56, 78, 255))
    c.rect(8, 8, 24, 44, (28, 20, 56, 255))
    pulse = step * 1.5
    c.disc(20.0, 29.0, 11.0, (124, 62, 225, 230))
    c.disc(20.0, 29.0, 6.5 + pulse * 0.6, (84, 242, 255, 245))
    c.disc(20.0, 29.0, 2.8, (240, 255, 255, 255))
    c.rect(2, 50, 36, 5, (88, 96, 128, 255))
    c.outline((16, 14, 30, 255))
    return c


def build_spawner() -> Canvas:
    c = Canvas(36, 28)
    c.rect(3, 20, 30, 7, (58, 52, 78, 255))
    c.rect(8, 10, 20, 10, (78, 62, 104, 255))
    c.rect(12, 4, 12, 8, (196, 62, 112, 255))
    c.disc(18.0, 15.0, 4.0, (255, 118, 162, 255))
    c.outline((20, 14, 30, 255))
    return c


def build_player_bolt(step: int) -> Canvas:
    c = Canvas(20, 12)
    tail = 2 if step == 1 else 0
    c.rect(2 + tail, 4, 10, 4, (64, 186, 255, 220))
    c.disc(14.0, 6.0, 4.2, (110, 245, 255, 255))
    c.disc(14.5, 6.0, 2.0, (245, 255, 255, 255))
    c.outline((22, 32, 64, 255))
    return c


def build_enemy_bolt(step: int) -> Canvas:
    c = Canvas(16, 16)
    outer = (255, 76, 98, 255) if step == 0 else (255, 138, 64, 255)
    c.disc(8.0, 8.0, 6.0, outer)
    c.disc(8.0, 8.0, 3.0, (255, 236, 152, 255))
    c.outline((38, 12, 24, 255))
    return c


def build_spark(step: int) -> Canvas:
    c = Canvas(24, 24)
    r = 4.0 + step * 3.2
    col = (108, 245, 255, 240) if step < 2 else (255, 226, 112, 190)
    c.ring(12.0, 12.0, r, max(1.0, r - 2.4), col)
    c.rect(11, max(1, int(12 - r)), 2, int(r * 2), (240, 255, 255, 210))
    c.rect(max(1, int(12 - r)), 11, int(r * 2), 2, (240, 255, 255, 210))
    return c


def build_jump_fx(step: int) -> Canvas:
    c = Canvas(28, 16)
    w = 18 + step * 6
    x0 = (28 - w) // 2
    c.rect(x0, 5 + step * 2, w, 4, (96, 242, 255, 225 - step * 65))
    c.rect(x0 + 3, 4 + step * 2, max(2, w - 6), 2, (240, 255, 255, 230 - step * 65))
    return c


def build_stone_ground() -> Canvas:
    c = Canvas(64, 32)
    c.rect(0, 0, 64, 32, (46, 52, 72, 255))
    c.rect(0, 0, 64, 5, (72, 92, 128, 255))
    c.rect(0, 5, 64, 2, (98, 206, 228, 255))
    for bx in (0, 20, 42):
        c.rect(bx + 1, 9, 18, 9, (58, 66, 90, 255))
    for bx in (10, 32, 52):
        c.rect(bx, 20, 16, 9, (54, 60, 84, 255))
    return c


def build_crystal_platform() -> Canvas:
    c = Canvas(64, 20)
    c.rect(1, 2, 62, 16, (44, 64, 108, 255))
    c.rect(1, 2, 62, 4, (86, 224, 255, 255))
    c.rect(4, 10, 56, 4, (64, 96, 152, 255))
    c.outline((16, 22, 44, 255))
    return c


def build_moving_platform() -> Canvas:
    c = Canvas(96, 20)
    c.rect(2, 2, 92, 14, (82, 72, 62, 255))
    c.rect(2, 2, 92, 4, (218, 178, 76, 255))
    c.disc(14.0, 15.0, 3.0, (96, 240, 255, 255))
    c.disc(82.0, 15.0, 3.0, (96, 240, 255, 255))
    c.outline((24, 18, 20, 255))
    return c


def build_jump_pad() -> Canvas:
    c = Canvas(36, 18)
    c.rect(2, 10, 32, 7, (58, 64, 88, 255))
    c.rect(5, 3, 26, 7, (78, 242, 228, 255))
    c.rect(8, 3, 20, 3, (220, 255, 252, 255))
    c.outline((18, 24, 42, 255))
    return c


def build_spike_hazard() -> Canvas:
    c = Canvas(48, 20)
    for sx in (2, 14, 26, 38):
        for row in range(14):
            w = max(1, (row * 8) // 14)
            c.rect(sx + 4 - w // 2, 4 + row, w, 1, (228, 52, 96, 255))
            c.px(sx + 4, 4 + row, (255, 196, 212, 255))
    c.rect(0, 16, 48, 4, (92, 32, 56, 255))
    c.outline((32, 10, 20, 255))
    return c


def build_death_zone() -> Canvas:
    c = Canvas(64, 16)
    c.rect(0, 6, 64, 10, (130, 30, 80, 96))
    return c


# ---------------------------------------------------------------------------
# Component / object JSON helpers
# ---------------------------------------------------------------------------

class IdGen:
    def __init__(self, start: int = 1) -> None:
        self.next_id = start

    def take(self) -> int:
        val = self.next_id
        self.next_id += 1
        return val


def comp_animator(ids: IdGen, clip: str = "", playing: bool = False, speed: float = 1.0) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 0,
        "animationClipFilePath": clip,
        "currentFrameIndex": 0,
        "playbackSpeed": speed,
        "playing": playing,
    }


def comp_audio(ids: IdGen, file_path: str = "", auto_play: bool = False, gain: float = 0.7, loop_count: int = 0) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 1,
        "autoPlay": auto_play,
        "filePath": file_path,
        "gain": gain,
        "loopCount": loop_count,
    }


def comp_camera(ids: IdGen, width: float = 640.0, height: float = 480.0) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 2,
        "width": width,
        "height": height,
    }


def comp_box_collider(
    ids: IdGen,
    width: float,
    height: float,
    group: int,
    mask: int,
    trigger: bool = False,
    offset_x: float = 0.0,
    offset_y: float = 0.0,
) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 3,
        "colliderType": 1,
        "width": float(width),
        "height": float(height),
        "offset": {"x": float(offset_x), "y": float(offset_y)},
        "trigger": bool(trigger),
        "collisionGroup": int(group),
        "collisionMask": int(mask),
    }


def comp_circle_collider(
    ids: IdGen,
    radius: float,
    group: int,
    mask: int,
    trigger: bool = False,
    offset_x: float = 0.0,
    offset_y: float = 0.0,
) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 3,
        "colliderType": 0,
        "radius": float(radius),
        "offset": {"x": float(offset_x), "y": float(offset_y)},
        "trigger": bool(trigger),
        "collisionGroup": int(group),
        "collisionMask": int(mask),
    }


def comp_rigidbody(
    ids: IdGen,
    body_type: int = 1,
    mass: float = 1.0,
    friction: float = 0.0,
    gravity: bool = False,
) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 4,
        "angularVelocity": 0.0,
        "bodyType": int(body_type),
        "force": {"x": 0.0, "y": 0.0},
        "friction": float(friction),
        "gravity": bool(gravity),
        "mass": float(mass),
        "restitution": 0.0,
        "torque": 0.0,
        "velocity": {"x": 0.0, "y": 0.0},
    }


def comp_script(ids: IdGen, type_name: str, **fields: Any) -> dict[str, Any]:
    behavior = {"type": type_name}
    behavior.update(fields)
    return {
        "id": ids.take(),
        "type": 5,
        "behaviors": [behavior],
    }


def comp_sprite(ids: IdGen, texture_path: str, width: float, height: float) -> dict[str, Any]:
    return {
        "id": ids.take(),
        "type": 6,
        "flip": 0,
        "width": float(width),
        "height": float(height),
        "sourceRect": None,
        "sourceRectEnabled": False,
        "textureFilePath": texture_path,
    }


def make_object(
    ids: IdGen,
    name: str,
    tag: str,
    x: float,
    y: float,
    components: list[dict[str, Any]],
    children: list[dict[str, Any]] | None = None,
    parent_id: int = -1,
    active: bool = True,
) -> dict[str, Any]:
    obj_id = ids.take()
    child_list = children or []
    for ch in child_list:
        ch["parentId"] = obj_id
    return {
        "active": active,
        "children": child_list,
        "components": components,
        "id": obj_id,
        "name": name,
        "parentId": parent_id,
        "position": {"x": float(x), "y": float(y)},
        "rotation": 0.0,
        "scale": {"x": 1.0, "y": 1.0},
        "tag": tag,
    }


# ---------------------------------------------------------------------------
# Template definitions (also reusable as scene builders with template links!)
# ---------------------------------------------------------------------------

def save_animset(rel_path: str, loop: bool, frames: list[tuple[float, str]]) -> str:
    doc = {
        "filePath": f"assets/{rel_path}",
        "loop": loop,
        "frames": [
            {
                "duration": float(dur),
                "hasSourceRect": False,
                "sourceRect": {"x": 0.0, "y": 0.0, "w": 0.0, "h": 0.0},
                "textureWrapperFilePath": tex,
            }
            for dur, tex in frames
        ],
    }
    full = ASSETS_DIR / rel_path
    full.parent.mkdir(parents=True, exist_ok=True)
    full.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    return f"assets/{rel_path}"


def validate_unique_ids(objects: list[dict[str, Any]], label: str) -> None:
    seen: set[int] = set()

    def walk(obj: dict[str, Any]) -> None:
        oid = obj["id"]
        if not isinstance(oid, int) or oid < 0:
            raise ValueError(f"{label}: invalid object id {oid}")
        if oid in seen:
            raise ValueError(f"{label}: duplicate id {oid}")
        seen.add(oid)
        for comp in obj.get("components", []):
            cid = comp["id"]
            if not isinstance(cid, int) or cid < 0:
                raise ValueError(f"{label}: invalid component id {cid}")
            if cid in seen:
                raise ValueError(f"{label}: duplicate component id {cid}")
            seen.add(cid)
        for ch in obj.get("children", []):
            walk(ch)

    for obj in objects:
        walk(obj)


def write_template(
    name: str,
    root: dict[str, Any],
    reference_bindings: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    validate_unique_ids([root], f"template {name}")
    doc: dict[str, Any] = {
        "format": "platformator.object-template",
        "version": 1,
        "name": name,
        "root": root,
    }
    if reference_bindings:
        doc["reference_bindings"] = reference_bindings
    path = ASSETS_DIR / "templates" / f"{name}.pttemplate"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    return doc


def instantiate_from_template(
    template_doc: dict[str, Any],
    template_name: str,
    ids: IdGen,
    name: str | None = None,
    x: float = 0.0,
    y: float = 0.0,
    script_overrides: dict[str, Any] | None = None,
    audio_file_override: str | None = None,
    box_size_override: tuple[float, float] | None = None,
    sprite_size_override: tuple[float, float] | None = None,
) -> dict[str, Any]:
    """Deep-clone a template subtree into scene objects with fresh scene IDs
    and template provenance metadata, remapping any internal reference_bindings.
    """
    proto_to_scene: dict[int, int] = {}

    def clone_node(proto_node: dict[str, Any], parent_id: int) -> dict[str, Any]:
        new_obj_id = ids.take()
        proto_to_scene[proto_node["id"]] = new_obj_id

        new_comps: list[dict[str, Any]] = []
        for comp in proto_node.get("components", []):
            new_comp = json.loads(json.dumps(comp))
            proto_comp_id = comp["id"]
            new_comp_id = ids.take()
            proto_to_scene[proto_comp_id] = new_comp_id
            new_comp["id"] = new_comp_id
            new_comp["templateComponentId"] = proto_comp_id
            new_comps.append(new_comp)

        new_children: list[dict[str, Any]] = []
        for ch in proto_node.get("children", []):
            new_children.append(clone_node(ch, new_obj_id))

        obj = json.loads(json.dumps(proto_node))
        obj["id"] = new_obj_id
        obj["parentId"] = parent_id
        obj["components"] = new_comps
        obj["children"] = new_children
        obj["template"] = f"templates/{template_name}.pttemplate"
        obj["templateNodeId"] = proto_node["id"]
        return obj

    cloned = clone_node(template_doc["root"], -1)
    if name is not None:
        cloned["name"] = name
    cloned["position"] = {"x": float(x), "y": float(y)}

    # Remap internal ObjectReference IDs if present in bindings
    for binding in template_doc.get("reference_bindings", []):
        if binding.get("scope") != "internal":
            continue
        path = binding.get("path", "")
        field_name = path.rsplit("/", 1)[-1]
        for comp in cloned.get("components", []):
            if comp.get("type") == 5 and comp.get("behaviors"):
                beh = comp["behaviors"][0]
                if field_name in beh and isinstance(beh[field_name], int):
                    target_proto = beh[field_name]
                    if target_proto in proto_to_scene:
                        beh[field_name] = proto_to_scene[target_proto]

    if script_overrides:
        for comp in cloned["components"]:
            if comp["type"] == 5 and comp["behaviors"]:
                comp["behaviors"][0].update(script_overrides)
    if audio_file_override is not None:
        for comp in cloned["components"]:
            if comp["type"] == 1:
                comp["filePath"] = audio_file_override
    if box_size_override is not None:
        bw, bh = box_size_override
        for comp in cloned["components"]:
            if comp["type"] == 3 and comp.get("colliderType") == 1:
                comp["width"] = float(bw)
                comp["height"] = float(bh)
    if sprite_size_override is not None:
        sw, sh = sprite_size_override
        for comp in cloned["components"]:
            if comp["type"] == 6:
                comp["width"] = float(sw)
                comp["height"] = float(sh)

    return cloned


def find_component(obj: dict[str, Any], comp_type: int) -> dict[str, Any]:
    for c in obj["components"]:
        if c["type"] == comp_type:
            return c
    raise KeyError(f"Component type {comp_type} missing on {obj['name']}")


# ---------------------------------------------------------------------------
# Build all assets
# ---------------------------------------------------------------------------

def main() -> None:
    print("Generating Crystal Citadel textures...")
    tex = {
        "wizard_idle_0": build_wizard("idle", 0).save("textures/player/wizard_idle_0.png"),
        "wizard_idle_1": build_wizard("idle", 1).save("textures/player/wizard_idle_1.png"),
        "wizard_run_0": build_wizard("run", 0).save("textures/player/wizard_run_0.png"),
        "wizard_run_1": build_wizard("run", 1).save("textures/player/wizard_run_1.png"),
        "wizard_run_2": build_wizard("run", 2).save("textures/player/wizard_run_2.png"),
        "wizard_jump": build_wizard("jump").save("textures/player/wizard_jump.png"),
        "wizard_fall": build_wizard("fall").save("textures/player/wizard_fall.png"),
        "crawler_0": build_crawler("walk", 0).save("textures/enemies/crawler_0.png"),
        "crawler_1": build_crawler("walk", 1).save("textures/enemies/crawler_1.png"),
        "crawler_squash": build_crawler("squash").save("textures/enemies/crawler_squash.png"),
        "slime_0": build_slime(0).save("textures/enemies/slime_0.png"),
        "slime_1": build_slime(1).save("textures/enemies/slime_1.png"),
        "turret_base": build_turret_base().save("textures/enemies/turret_base.png"),
        "turret_barrel": build_turret_barrel().save("textures/enemies/turret_barrel.png"),
        "archon_body": build_archon_body().save("textures/enemies/archon_body.png"),
        "archon_crown": build_archon_crown().save("textures/enemies/archon_crown.png"),
        "archon_orb": build_archon_orb().save("textures/enemies/archon_orb.png"),
        "gem_0": build_gem(0).save("textures/items/gem_0.png"),
        "gem_1": build_gem(1).save("textures/items/gem_1.png"),
        "gem_2": build_gem(2).save("textures/items/gem_2.png"),
        "gem_3": build_gem(3).save("textures/items/gem_3.png"),
        "shield_core_0": build_shield_core(0).save("textures/items/shield_core_0.png"),
        "shield_core_1": build_shield_core(1).save("textures/items/shield_core_1.png"),
        "orbit_shield": build_orbit_shield().save("textures/items/orbit_shield.png"),
        "crate": build_crate().save("textures/items/crate.png"),
        "portal_0": build_portal(0).save("textures/items/portal_0.png"),
        "portal_1": build_portal(1).save("textures/items/portal_1.png"),
        "portal_2": build_portal(2).save("textures/items/portal_2.png"),
        "spawner": build_spawner().save("textures/items/spawner.png"),
        "player_bolt_0": build_player_bolt(0).save("textures/projectiles/player_bolt_0.png"),
        "player_bolt_1": build_player_bolt(1).save("textures/projectiles/player_bolt_1.png"),
        "enemy_bolt_0": build_enemy_bolt(0).save("textures/projectiles/enemy_bolt_0.png"),
        "enemy_bolt_1": build_enemy_bolt(1).save("textures/projectiles/enemy_bolt_1.png"),
        "spark_0": build_spark(0).save("textures/fx/spark_0.png"),
        "spark_1": build_spark(1).save("textures/fx/spark_1.png"),
        "spark_2": build_spark(2).save("textures/fx/spark_2.png"),
        "jump_fx_0": build_jump_fx(0).save("textures/fx/jump_fx_0.png"),
        "jump_fx_1": build_jump_fx(1).save("textures/fx/jump_fx_1.png"),
        "stone_ground": build_stone_ground().save("textures/tiles/stone_ground.png"),
        "crystal_platform": build_crystal_platform().save("textures/tiles/crystal_platform.png"),
        "moving_platform": build_moving_platform().save("textures/tiles/moving_platform.png"),
        "jump_pad": build_jump_pad().save("textures/tiles/jump_pad.png"),
        "spike_hazard": build_spike_hazard().save("textures/tiles/spike_hazard.png"),
        "death_zone": build_death_zone().save("textures/tiles/death_zone.png"),
    }

    print("Generating Crystal Citadel audio...")
    sfx = {
        "bgm_hub": write_wav("audio/bgm_hub.wav", synth_bgm([261.6, 329.6, 392.0, 493.9, 392.0, 329.6, 293.7, 392.0] * 2, bpm=116)),
        "bgm_cavern": write_wav("audio/bgm_cavern.wav", synth_bgm([220.0, 261.6, 329.6, 440.0, 392.0, 329.6, 293.7, 246.9] * 2, bpm=132)),
        "bgm_foundry": write_wav("audio/bgm_foundry.wav", synth_bgm([196.0, 233.1, 293.7, 349.2, 293.7, 233.1, 220.0, 261.6] * 2, bpm=142)),
        "bgm_sanctum": write_wav("audio/bgm_sanctum.wav", synth_bgm([246.9, 293.7, 370.0, 493.9, 440.0, 370.0, 329.6, 293.7] * 2, bpm=150)),
        "jump": write_wav("audio/jump.wav", synth_sfx(0.16, 280.0, 620.0, "tri", 0.42)),
        "shoot": write_wav("audio/shoot.wav", synth_sfx(0.12, 740.0, 310.0, "square", 0.30)),
        "hurt": write_wav("audio/hurt.wav", synth_sfx(0.22, 240.0, 95.0, "square", 0.45)),
        "gem": write_wav("audio/gem.wav", synth_sfx(0.18, 660.0, 1180.0, "sine", 0.45)),
        "crate_break": write_wav("audio/crate_break.wav", synth_sfx(0.20, 220.0, 75.0, "noise", 0.50)),
        "slime_split": write_wav("audio/slime_split.wav", synth_sfx(0.19, 340.0, 680.0, "sine", 0.42)),
        "turret_fire": write_wav("audio/turret_fire.wav", synth_sfx(0.15, 520.0, 190.0, "square", 0.34)),
        "bounce": write_wav("audio/bounce.wav", synth_sfx(0.22, 240.0, 820.0, "sine", 0.46)),
        "warp": write_wav("audio/warp.wav", synth_sfx(0.32, 330.0, 990.0, "tri", 0.44)),
        "enemy_hit": write_wav("audio/enemy_hit.wav", synth_sfx(0.14, 410.0, 150.0, "square", 0.36)),
    }

    print("Generating animsets...")
    anim = {
        "player_idle": save_animset("animations/player_idle.animset", True, [(0.35, tex["wizard_idle_0"]), (0.35, tex["wizard_idle_1"])]),
        "player_run": save_animset("animations/player_run.animset", True, [(0.11, tex["wizard_run_0"]), (0.11, tex["wizard_run_1"]), (0.11, tex["wizard_run_2"])]),
        "player_jump": save_animset("animations/player_jump.animset", False, [(0.2, tex["wizard_jump"])]),
        "player_fall": save_animset("animations/player_fall.animset", False, [(0.2, tex["wizard_fall"])]),
        "crawler_walk": save_animset("animations/crawler_walk.animset", True, [(0.18, tex["crawler_0"]), (0.18, tex["crawler_1"])]),
        "crawler_squash": save_animset("animations/crawler_squash.animset", False, [(0.25, tex["crawler_squash"])]),
        "slime_bounce": save_animset("animations/slime_bounce.animset", True, [(0.22, tex["slime_0"]), (0.22, tex["slime_1"])]),
        "gem_spin": save_animset("animations/gem_spin.animset", True, [(0.12, tex["gem_0"]), (0.12, tex["gem_1"]), (0.12, tex["gem_2"]), (0.12, tex["gem_3"])]),
        "shield_pulse": save_animset("animations/shield_pulse.animset", True, [(0.24, tex["shield_core_0"]), (0.24, tex["shield_core_1"])]),
        "portal_swirl": save_animset("animations/portal_swirl.animset", True, [(0.14, tex["portal_0"]), (0.14, tex["portal_1"]), (0.14, tex["portal_2"])]),
        "spark_burst": save_animset("animations/spark_burst.animset", False, [(0.09, tex["spark_0"]), (0.10, tex["spark_1"]), (0.14, tex["spark_2"])]),
        "jump_burst": save_animset("animations/jump_burst.animset", False, [(0.10, tex["jump_fx_0"]), (0.14, tex["jump_fx_1"])]),
    }

    print("Generating .pttemplate files...")
    templates: dict[str, dict[str, Any]] = {}

    def build_simple_templates() -> None:
        # 1. HitSparkFx
        g = IdGen(1)
        templates["HitSparkFx"] = write_template(
            "HitSparkFx",
            make_object(
                g, "HitSparkFx", "fx", 0, 0,
                [
                    comp_sprite(g, tex["spark_0"], 24, 24),
                    comp_animator(g, anim["spark_burst"], True, 1.0),
                    comp_script(g, "CitadelTimedEffect", duration=0.35, rotationSpeed=7.0, riseSpeed=18.0, clip=anim["spark_burst"]),
                ],
            ),
        )

        # 2. JumpBurstFx
        g = IdGen(1)
        templates["JumpBurstFx"] = write_template(
            "JumpBurstFx",
            make_object(
                g, "JumpBurstFx", "fx", 0, 0,
                [
                    comp_sprite(g, tex["jump_fx_0"], 28, 16),
                    comp_animator(g, anim["jump_burst"], True, 1.0),
                    comp_script(g, "CitadelTimedEffect", duration=0.25, rotationSpeed=0.0, riseSpeed=28.0, clip=anim["jump_burst"]),
                ],
            ),
        )

        # 3. OrbitShield
        g = IdGen(1)
        templates["OrbitShield"] = write_template(
            "OrbitShield",
            make_object(
                g, "OrbitShield", "shield", 0, 0,
                [
                    comp_sprite(g, tex["orbit_shield"], 18, 18),
                    comp_circle_collider(g, 11.0, LAYER_PLAYER, LAYER_PROJECTILE, trigger=True),
                    comp_rigidbody(g, body_type=2, mass=0.5, friction=0.0, gravity=False),
                    comp_script(g, "CitadelOrbitShield", orbitRadius=34.0, orbitSpeed=3.2, currentAngle=0.0, shatterFxTemplate="assets/templates/HitSparkFx.pttemplate"),
                ],
            ),
        )

        # 4. PlayerBolt
        g = IdGen(1)
        templates["PlayerBolt"] = write_template(
            "PlayerBolt",
            make_object(
                g, "PlayerBolt", "projectile", 0, 0,
                [
                    comp_sprite(g, tex["player_bolt_0"], 20, 12),
                    comp_circle_collider(g, 7.0, LAYER_PROJECTILE, LAYER_DEFAULT | LAYER_PLAYER | LAYER_ENEMY | LAYER_PLATFORMS, trigger=True),
                    comp_rigidbody(g, body_type=2, mass=0.2, friction=0.0, gravity=False),
                    comp_script(g, "CitadelProjectile", speedX=380.0, speedY=0.0, lifetime=1.6, damage=1, friendly=True, impactFxTemplate="assets/templates/HitSparkFx.pttemplate"),
                ],
            ),
        )

        # 5. EnemyBolt
        g = IdGen(1)
        templates["EnemyBolt"] = write_template(
            "EnemyBolt",
            make_object(
                g, "EnemyBolt", "projectile", 0, 0,
                [
                    comp_sprite(g, tex["enemy_bolt_0"], 16, 16),
                    comp_circle_collider(g, 7.0, LAYER_PROJECTILE, LAYER_DEFAULT | LAYER_PLAYER | LAYER_ENEMY | LAYER_PLATFORMS, trigger=True),
                    comp_rigidbody(g, body_type=2, mass=0.2, friction=0.0, gravity=False),
                    comp_script(g, "CitadelProjectile", speedX=-220.0, speedY=0.0, lifetime=2.2, damage=1, friendly=False, impactFxTemplate="assets/templates/HitSparkFx.pttemplate"),
                ],
            ),
        )

        # 6. CrystalGem
        g = IdGen(1)
        templates["CrystalGem"] = write_template(
            "CrystalGem",
            make_object(
                g, "CrystalGem", "collectible", 0, 0,
                [
                    comp_sprite(g, tex["gem_0"], 24, 24),
                    comp_circle_collider(g, 11.0, LAYER_COLLECTIBLE, LAYER_PLAYER, trigger=True),
                    comp_rigidbody(g, body_type=1, mass=0.1, friction=0.0, gravity=False),
                    comp_animator(g, anim["gem_spin"], True, 1.0),
                    comp_audio(g, sfx["gem"], False, 0.55, 0),
                    comp_script(g, "CitadelCollectible", gemValue=1, healAmount=0, grantsShield=False, idleAnim=anim["gem_spin"], pickupFxTemplate="assets/templates/HitSparkFx.pttemplate", pickupSound=sfx["gem"]),
                ],
            ),
        )

        # 7. ShieldCore
        g = IdGen(1)
        templates["ShieldCore"] = write_template(
            "ShieldCore",
            make_object(
                g, "ShieldCore", "collectible", 0, 0,
                [
                    comp_sprite(g, tex["shield_core_0"], 24, 24),
                    comp_circle_collider(g, 12.0, LAYER_COLLECTIBLE, LAYER_PLAYER, trigger=True),
                    comp_rigidbody(g, body_type=1, mass=0.1, friction=0.0, gravity=False),
                    comp_animator(g, anim["shield_pulse"], True, 1.0),
                    comp_audio(g, sfx["gem"], False, 0.65, 0),
                    comp_script(g, "CitadelCollectible", gemValue=0, healAmount=1, grantsShield=True, idleAnim=anim["shield_pulse"], pickupFxTemplate="assets/templates/HitSparkFx.pttemplate", pickupSound=sfx["gem"]),
                ],
            ),
        )

        # 8. Player
        g = IdGen(1)
        templates["Player"] = write_template(
            "Player",
            make_object(
                g, "Player", "player", 0, 0,
                [
                    comp_sprite(g, tex["wizard_idle_0"], 32, 40),
                    comp_box_collider(g, 24, 38, LAYER_PLAYER, ALL_MASK, trigger=False),
                    comp_rigidbody(g, body_type=2, mass=1.0, friction=0.0, gravity=False),
                    comp_animator(g, anim["player_idle"], True, 1.0),
                    comp_audio(g, "", False, 0.6, 0),
                    comp_script(
                        g,
                        "CitadelPlayer",
                        walkSpeed=195.0,
                        jumpSpeed=275.0,
                        gravity=98.10 * 4.2,
                        maxHp=5,
                        hp=5,
                        fireCooldown=0.28,
                        boltTemplate="assets/templates/PlayerBolt.pttemplate",
                        jumpFxTemplate="assets/templates/JumpBurstFx.pttemplate",
                        shieldTemplate="assets/templates/OrbitShield.pttemplate",
                        idleAnim=anim["player_idle"],
                        runAnim=anim["player_run"],
                        jumpAnim=anim["player_jump"],
                        fallAnim=anim["player_fall"],
                        jumpSound=sfx["jump"],
                        shootSound=sfx["shoot"],
                        hurtSound=sfx["hurt"],
                    ),
                ],
            ),
        )

        # 9. BGM
        g = IdGen(1)
        templates["BGM"] = write_template(
            "BGM",
            make_object(g, "BGM", "audio", 0, 0, [comp_audio(g, sfx["bgm_hub"], True, 0.42, -1)]),
        )

        # 10. StoneGround
        g = IdGen(1)
        templates["StoneGround"] = write_template(
            "StoneGround",
            make_object(
                g, "StoneGround", "solid", 0, 0,
                [
                    comp_sprite(g, tex["stone_ground"], 64, 32),
                    comp_box_collider(g, 64, 32, LAYER_PLATFORMS, ALL_MASK, trigger=False),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                ],
            ),
        )

        # 11. CrystalPlatform
        g = IdGen(1)
        templates["CrystalPlatform"] = write_template(
            "CrystalPlatform",
            make_object(
                g, "CrystalPlatform", "solid", 0, 0,
                [
                    comp_sprite(g, tex["crystal_platform"], 64, 20),
                    comp_box_collider(g, 64, 20, LAYER_PLATFORMS, ALL_MASK, trigger=False),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                ],
            ),
        )

        # 12. MovingPlatform
        g = IdGen(1)
        templates["MovingPlatform"] = write_template(
            "MovingPlatform",
            make_object(
                g, "MovingPlatform", "solid", 0, 0,
                [
                    comp_sprite(g, tex["moving_platform"], 96, 20),
                    comp_box_collider(g, 96, 20, LAYER_PLATFORMS, ALL_MASK, trigger=False),
                    comp_rigidbody(g, body_type=2, mass=1.0, friction=0.0, gravity=False),
                    comp_script(g, "CitadelMovingPlatform", pointA={"x": 0.0, "y": 0.0}, pointB={"x": 140.0, "y": 0.0}, speed=70.0, movingToB=True),
                ],
            ),
        )

        # 13. JumpPad
        g = IdGen(1)
        templates["JumpPad"] = write_template(
            "JumpPad",
            make_object(
                g, "JumpPad", "jumppad", 0, 0,
                [
                    comp_sprite(g, tex["jump_pad"], 36, 18),
                    comp_box_collider(g, 36, 16, LAYER_PLATFORMS, ALL_MASK, trigger=False),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                    comp_audio(g, sfx["bounce"], False, 0.55, 0),
                    comp_script(g, "CitadelJumpPad", bounceFactor=1.55, burstFxTemplate="assets/templates/JumpBurstFx.pttemplate", bounceSound=sfx["bounce"]),
                ],
            ),
        )

        # 14. SpikeHazard
        g = IdGen(1)
        templates["SpikeHazard"] = write_template(
            "SpikeHazard",
            make_object(
                g, "SpikeHazard", "hazard", 0, 0,
                [
                    comp_sprite(g, tex["spike_hazard"], 48, 20),
                    comp_box_collider(g, 44, 14, LAYER_HAZARD, LAYER_PLAYER, trigger=True, offset_x=0.0, offset_y=2.0),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                ],
            ),
        )

        # 15. DeathZone
        g = IdGen(1)
        templates["DeathZone"] = write_template(
            "DeathZone",
            make_object(
                g, "DeathZone", "death", 0, 0,
                [
                    comp_sprite(g, tex["death_zone"], 64, 16),
                    comp_box_collider(g, 2600, 48, LAYER_HAZARD, LAYER_PLAYER, trigger=True),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                ],
            ),
        )

        # 16. LevelPortal
        g = IdGen(1)
        templates["LevelPortal"] = write_template(
            "LevelPortal",
            make_object(
                g, "LevelPortal", "portal", 0, 0,
                [
                    comp_sprite(g, tex["portal_0"], 40, 56),
                    comp_box_collider(g, 32, 48, LAYER_DEFAULT, LAYER_PLAYER, trigger=True),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                    comp_animator(g, anim["portal_swirl"], True, 1.0),
                    comp_audio(g, sfx["warp"], False, 0.65, 0),
                    comp_script(g, "CitadelLevelPortal", targetScenePath="assets/scenes/hub.scene", requiredGems=0, portalAnim=anim["portal_swirl"], warpFxTemplate="assets/templates/HitSparkFx.pttemplate", warpSound=sfx["warp"]),
                ],
            ),
        )

        # 17. BreakableCrate
        g = IdGen(1)
        templates["BreakableCrate"] = write_template(
            "BreakableCrate",
            make_object(
                g, "BreakableCrate", "crate", 0, 0,
                [
                    comp_sprite(g, tex["crate"], 32, 32),
                    comp_box_collider(g, 32, 32, LAYER_DEFAULT, ALL_MASK, trigger=False),
                    comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                    comp_audio(g, sfx["crate_break"], False, 0.65, 0),
                    comp_script(g, "CitadelBreakableCrate", hitPoints=1, dropCount=2, lootTemplate="assets/templates/CrystalGem.pttemplate", bonusLootTemplate="assets/templates/ShieldCore.pttemplate", breakFxTemplate="assets/templates/HitSparkFx.pttemplate", breakSound=sfx["crate_break"]),
                ],
            ),
        )

        # 18. CaveCrawler
        g = IdGen(1)
        templates["CaveCrawler"] = write_template(
            "CaveCrawler",
            make_object(
                g, "CaveCrawler", "enemy", 0, 0,
                [
                    comp_sprite(g, tex["crawler_0"], 32, 28),
                    comp_box_collider(g, 28, 24, LAYER_ENEMY, LAYER_DEFAULT | LAYER_PLAYER | LAYER_PLATFORMS | LAYER_PROJECTILE, trigger=False),
                    comp_rigidbody(g, body_type=2, mass=1.0, friction=0.0, gravity=False),
                    comp_animator(g, anim["crawler_walk"], True, 1.0),
                    comp_audio(g, sfx["enemy_hit"], False, 0.5, 0),
                    comp_script(g, "CitadelPatrolEnemy", walkSpeed=65.0, minX=0.0, maxX=0.0, direction=-1.0, hp=2, dropTemplate="assets/templates/CrystalGem.pttemplate", deathFxTemplate="assets/templates/HitSparkFx.pttemplate", walkAnim=anim["crawler_walk"], squashAnim=anim["crawler_squash"], hitSound=sfx["enemy_hit"]),
                ],
            ),
        )

        # 19. Slime (self-referential!)
        g = IdGen(1)
        templates["Slime"] = write_template(
            "Slime",
            make_object(
                g, "Slime", "enemy", 0, 0,
                [
                    comp_sprite(g, tex["slime_0"], 32, 28),
                    comp_box_collider(g, 26, 22, LAYER_ENEMY, LAYER_DEFAULT | LAYER_PLAYER | LAYER_PLATFORMS | LAYER_PROJECTILE, trigger=False),
                    comp_rigidbody(g, body_type=2, mass=0.9, friction=0.0, gravity=False),
                    comp_animator(g, anim["slime_bounce"], True, 1.0),
                    comp_audio(g, sfx["slime_split"], False, 0.55, 0),
                    comp_script(g, "CitadelSplittingSlime", generation=1, maxGeneration=2, hp=1, walkSpeed=75.0, minX=0.0, maxX=0.0, direction=-1.0, selfTemplate="assets/templates/Slime.pttemplate", gemTemplate="assets/templates/CrystalGem.pttemplate", splitFxTemplate="assets/templates/HitSparkFx.pttemplate", bounceAnim=anim["slime_bounce"], splitSound=sfx["slime_split"]),
                ],
            ),
        )

        # 20. WaveSpawner
        g = IdGen(1)
        templates["WaveSpawner"] = write_template(
            "WaveSpawner",
            make_object(
                g, "WaveSpawner", "spawner", 0, 0,
                [
                    comp_sprite(g, tex["spawner"], 36, 28),
                    comp_script(g, "CitadelWaveSpawner", primaryEnemyTemplate="assets/templates/Slime.pttemplate", secondaryEnemyTemplate="assets/templates/CaveCrawler.pttemplate", spawnFxTemplate="assets/templates/HitSparkFx.pttemplate", spawnInterval=4.2, activationRange=620.0, maxSpawns=3, spawnedCount=0, spawnOnStart=True),
                ],
            ),
        )

        # 21. ArcaneTurret (Composite template with internal ObjectReferences!)
        g = IdGen(1)
        barrel_sprite = comp_sprite(g, tex["turret_barrel"], 32, 16)
        barrel_obj = make_object(g, "TurretBarrel", "turret_part", 0.0, -4.0, [barrel_sprite])
        turret_script = comp_script(
            g,
            "CitadelArcaneTurret",
            barrelObject=barrel_obj["id"],
            barrelSprite=barrel_sprite["id"],
            boltTemplate="assets/templates/EnemyBolt.pttemplate",
            muzzleFxTemplate="assets/templates/HitSparkFx.pttemplate",
            fireSound=sfx["turret_fire"],
            fireInterval=2.1,
            detectRange=480.0,
            boltSpeed=230.0,
        )
        turret_root = make_object(
            g,
            "ArcaneTurret",
            "enemy",
            0,
            0,
            [
                comp_sprite(g, tex["turret_base"], 32, 32),
                comp_box_collider(g, 30, 30, LAYER_ENEMY, LAYER_DEFAULT | LAYER_PLAYER | LAYER_PLATFORMS | LAYER_PROJECTILE, trigger=False),
                comp_rigidbody(g, body_type=1, mass=1.0, friction=0.0, gravity=False),
                comp_audio(g, sfx["turret_fire"], False, 0.5, 0),
                turret_script,
            ],
            children=[barrel_obj],
        )
        templates["ArcaneTurret"] = write_template(
            "ArcaneTurret",
            turret_root,
            reference_bindings=[
                {
                    "owner_id": turret_root["id"],
                    "component_id": turret_script["id"],
                    "path": "/behaviors/0/barrelObject",
                    "target_kind": "GameObject",
                    "scope": "internal",
                },
                {
                    "owner_id": turret_root["id"],
                    "component_id": turret_script["id"],
                    "path": "/behaviors/0/barrelSprite",
                    "target_kind": "Sprite",
                    "scope": "internal",
                },
            ],
        )

        # 22. CrystalArchonBoss (Composite boss with 3 internal child references!)
        g = IdGen(1)
        crown_obj = make_object(g, "ArchonCrown", "boss_part", 0.0, -42.0, [comp_sprite(g, tex["archon_crown"], 40, 24)])
        left_orb = make_object(g, "ArchonOrbL", "boss_part", -42.0, 0.0, [comp_sprite(g, tex["archon_orb"], 22, 22)])
        right_orb = make_object(g, "ArchonOrbR", "boss_part", 42.0, 0.0, [comp_sprite(g, tex["archon_orb"], 22, 22)])
        boss_script = comp_script(
            g,
            "CitadelArchonBoss",
            maxHp=12,
            hp=12,
            arenaMinX=160.0,
            arenaMaxX=780.0,
            hoverY=230.0,
            moveSpeed=75.0,
            attackInterval=2.2,
            summonInterval=6.5,
            crownChild=crown_obj["id"],
            leftOrbChild=left_orb["id"],
            rightOrbChild=right_orb["id"],
            bossBoltTemplate="assets/templates/EnemyBolt.pttemplate",
            minionTemplate="assets/templates/Slime.pttemplate",
            rewardGemTemplate="assets/templates/CrystalGem.pttemplate",
            defeatPortalTemplate="assets/templates/LevelPortal.pttemplate",
            explosionFxTemplate="assets/templates/HitSparkFx.pttemplate",
        )
        boss_root = make_object(
            g,
            "CrystalArchonBoss",
            "boss",
            0,
            0,
            [
                comp_sprite(g, tex["archon_body"], 64, 64),
                comp_box_collider(g, 52, 56, LAYER_ENEMY, LAYER_DEFAULT | LAYER_PLAYER | LAYER_PLATFORMS | LAYER_PROJECTILE, trigger=False),
                comp_rigidbody(g, body_type=2, mass=3.0, friction=0.0, gravity=False),
                comp_audio(g, sfx["enemy_hit"], False, 0.65, 0),
                boss_script,
            ],
            children=[crown_obj, left_orb, right_orb],
        )
        templates["CrystalArchonBoss"] = write_template(
            "CrystalArchonBoss",
            boss_root,
            reference_bindings=[
                {
                    "owner_id": boss_root["id"],
                    "component_id": boss_script["id"],
                    "path": "/behaviors/0/crownChild",
                    "target_kind": "GameObject",
                    "scope": "internal",
                },
                {
                    "owner_id": boss_root["id"],
                    "component_id": boss_script["id"],
                    "path": "/behaviors/0/leftOrbChild",
                    "target_kind": "GameObject",
                    "scope": "internal",
                },
                {
                    "owner_id": boss_root["id"],
                    "component_id": boss_script["id"],
                    "path": "/behaviors/0/rightOrbChild",
                    "target_kind": "GameObject",
                    "scope": "internal",
                },
            ],
        )

    build_simple_templates()

    # -----------------------------------------------------------------------
    # Scene assembly helpers
    # -----------------------------------------------------------------------

    def add_ground_span(objects: list[dict[str, Any]], ids: IdGen, start_x: float, end_x: float, y: float, prefix: str = "Ground") -> None:
        x = start_x
        idx = 0
        while x <= end_x:
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"{prefix}_{idx}", x=x, y=y))
            x += 64.0
            idx += 1

    def add_platform_span(objects: list[dict[str, Any]], ids: IdGen, start_x: float, count: int, y: float, prefix: str = "Plat") -> None:
        for i in range(count):
            objects.append(instantiate_from_template(templates["CrystalPlatform"], "CrystalPlatform", ids, name=f"{prefix}_{i}", x=start_x + i * 64.0, y=y))

    def finish_scene(
        scene_id: str,
        asset_path: str,
        title: str,
        next_scene: str,
        required_gems: int,
        bgm_path: str,
        player_x: float,
        player_y: float,
        max_cam_x: float,
        build_fn: Any,
    ) -> None:
        ids = IdGen(1)
        objects: list[dict[str, Any]] = []

        player_obj = instantiate_from_template(templates["Player"], "Player", ids, name="Player", x=player_x, y=player_y)
        bgm_obj = instantiate_from_template(templates["BGM"], "BGM", ids, name="BGM", x=player_x, y=player_y - 120.0, audio_file_override=bgm_path)
        bgm_audio = find_component(bgm_obj, 1)

        cam_comp = comp_camera(ids, 640.0, 480.0)
        cam_rig = comp_script(
            ids,
            "CitadelCameraRig",
            camera=cam_comp["id"],
            target=player_obj["id"],
            minX=0.0,
            maxX=max_cam_x,
            minY=-80.0,
            maxY=120.0,
            verticalOffset=40.0,
        )
        cam_obj = make_object(ids, "Main Camera", "camera", max(0.0, player_x - 200.0), 0.0, [cam_comp, cam_rig])

        gm_script = comp_script(
            ids,
            "CitadelGameManager",
            levelTitle=title,
            nextScenePath=next_scene,
            hubScenePath="assets/scenes/hub.scene",
            requiredGems=required_gems,
            collectedGems=0,
            enemiesDefeated=0,
            player=player_obj["id"],
            bgm=bgm_audio["id"],
            victoryEffectTemplate="assets/templates/HitSparkFx.pttemplate",
        )
        gm_obj = make_object(ids, "GameManager", "manager", 0.0, 0.0, [gm_script])

        objects.extend([gm_obj, bgm_obj, cam_obj, player_obj])
        build_fn(objects, ids)
        validate_unique_ids(objects, asset_path)

        rel = asset_path.removeprefix("assets/")
        out = ASSETS_DIR / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(objects, indent=2) + "\n", encoding="utf-8")

    # Scene 1: Hub
    def build_hub(objects: list[dict[str, Any]], ids: IdGen) -> None:
        add_ground_span(objects, ids, -64, 1088, 432, "HubFloor")
        # Left & right boundary walls
        for wy in (368, 304, 240, 176):
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallL_{wy}", x=-64, y=wy))
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallR_{wy}", x=1088, y=wy))
        # Central shrine terrace
        add_platform_span(objects, ids, 320, 5, 320, "ShrinePlat")
        add_platform_span(objects, ids, 680, 4, 270, "BalconyPlat")
        objects.append(instantiate_from_template(templates["JumpPad"], "JumpPad", ids, name="HubJumpPad", x=272, y=408))
        # Interactive showcase: Crate drops gems + ShieldCore; plus direct ShieldCore & Gems
        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="TrainingCrate", x=210, y=400))
        objects.append(instantiate_from_template(templates["ShieldCore"], "ShieldCore", ids, name="HubShieldCore", x=384, y=288))
        objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name="HubGem_0", x=448, y=288))
        objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name="HubGem_1", x=512, y=288))
        objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name="HubGem_2", x=744, y=236))
        # Training crawler on far alcove
        objects.append(instantiate_from_template(templates["CaveCrawler"], "CaveCrawler", ids, name="PracticeCrawler", x=620, y=400, script_overrides={"minX": 540.0, "maxX": 780.0, "direction": -1.0}))
        # Level Portals
        objects.append(instantiate_from_template(templates["LevelPortal"], "LevelPortal", ids, name="PortalToCavern", x=930, y=388, script_overrides={"targetScenePath": "assets/scenes/level1_cavern.scene", "requiredGems": 0}))
        objects.append(instantiate_from_template(templates["LevelPortal"], "LevelPortal", ids, name="PortalToFoundry", x=712, y=232, script_overrides={"targetScenePath": "assets/scenes/level2_foundry.scene", "requiredGems": 0}))
        objects.append(instantiate_from_template(templates["LevelPortal"], "LevelPortal", ids, name="PortalToSanctum", x=840, y=232, script_overrides={"targetScenePath": "assets/scenes/level3_sanctum.scene", "requiredGems": 0}))
        objects.append(instantiate_from_template(templates["DeathZone"], "DeathZone", ids, name="Abyss", x=512, y=560))

    finish_scene(
        "hub",
        "assets/scenes/hub.scene",
        "Crystal Citadel - Sanctuary Hub",
        "assets/scenes/level1_cavern.scene",
        0,
        sfx["bgm_hub"],
        120.0,
        380.0,
        520.0,
        build_hub,
    )

    # Scene 2: Level 1 - Echoing Caverns
    def build_cavern(objects: list[dict[str, Any]], ids: IdGen) -> None:
        add_ground_span(objects, ids, -64, 576, 432, "CavernFloorA")
        add_ground_span(objects, ids, 768, 1280, 432, "CavernFloorB")
        add_ground_span(objects, ids, 1408, 1856, 432, "CavernFloorC")
        for wy in (368, 304, 240, 176):
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallL_{wy}", x=-64, y=wy))
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallR_{wy}", x=1856, y=wy))

        add_platform_span(objects, ids, 320, 3, 336, "CavernLedgeA")
        add_platform_span(objects, ids, 608, 2, 376, "CavernBridge")
        add_platform_span(objects, ids, 960, 4, 304, "CavernLedgeB")
        add_platform_span(objects, ids, 1300, 2, 360, "CavernGapStep")

        objects.append(instantiate_from_template(templates["JumpPad"], "JumpPad", ids, name="CavernJump_0", x=480, y=408))
        objects.append(instantiate_from_template(templates["JumpPad"], "JumpPad", ids, name="CavernJump_1", x=910, y=408))
        objects.append(instantiate_from_template(templates["SpikeHazard"], "SpikeHazard", ids, name="CavernSpikes_0", x=1024, y=408))

        objects.append(instantiate_from_template(templates["CaveCrawler"], "CaveCrawler", ids, name="CavernCrawler_0", x=360, y=400, script_overrides={"minX": 240.0, "maxX": 520.0}))
        objects.append(instantiate_from_template(templates["Slime"], "Slime", ids, name="CavernSlime_0", x=860, y=400, script_overrides={"minX": 780.0, "maxX": 980.0}))
        objects.append(instantiate_from_template(templates["Slime"], "Slime", ids, name="CavernSlime_1", x=1560, y=400, script_overrides={"minX": 1440.0, "maxX": 1720.0}))
        objects.append(instantiate_from_template(templates["WaveSpawner"], "WaveSpawner", ids, name="CavernSpawner", x=1160, y=402, script_overrides={"maxSpawns": 3, "spawnInterval": 4.5}))

        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="CavernCrate_0", x=384, y=310))
        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="CavernCrate_1", x=1088, y=278))
        for i, (gx, gy) in enumerate(((260, 392), (350, 300), (640, 340), (1024, 268), (1500, 392), (1650, 392))):
            objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name=f"CavernGem_{i}", x=gx, y=gy))
        objects.append(instantiate_from_template(templates["ShieldCore"], "ShieldCore", ids, name="CavernShield", x=1120, y=268))

        objects.append(instantiate_from_template(templates["LevelPortal"], "LevelPortal", ids, name="ExitPortal", x=1770, y=388, script_overrides={"targetScenePath": "assets/scenes/level2_foundry.scene", "requiredGems": 3}))
        objects.append(instantiate_from_template(templates["DeathZone"], "DeathZone", ids, name="Abyss", x=900, y=560))

    finish_scene(
        "level1_cavern",
        "assets/scenes/level1_cavern.scene",
        "Crystal Citadel - Echoing Caverns",
        "assets/scenes/level2_foundry.scene",
        3,
        sfx["bgm_cavern"],
        96.0,
        380.0,
        1220.0,
        build_cavern,
    )

    # Scene 3: Level 2 - Runic Foundry
    def build_foundry(objects: list[dict[str, Any]], ids: IdGen) -> None:
        add_ground_span(objects, ids, -64, 448, 432, "FoundryFloorA")
        add_ground_span(objects, ids, 896, 1280, 432, "FoundryFloorB")
        add_ground_span(objects, ids, 1536, 1920, 432, "FoundryFloorC")
        for wy in (368, 304, 240, 176):
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallL_{wy}", x=-64, y=wy))
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallR_{wy}", x=1920, y=wy))

        objects.append(instantiate_from_template(templates["MovingPlatform"], "MovingPlatform", ids, name="FoundryFerry_0", x=520, y=390, script_overrides={"pointA": {"x": 520.0, "y": 390.0}, "pointB": {"x": 810.0, "y": 390.0}, "speed": 75.0}))
        objects.append(instantiate_from_template(templates["MovingPlatform"], "MovingPlatform", ids, name="FoundryFerry_1", x=1330, y=370, script_overrides={"pointA": {"x": 1320.0, "y": 370.0}, "pointB": {"x": 1480.0, "y": 290.0}, "speed": 65.0}))

        add_platform_span(objects, ids, 220, 3, 320, "FoundryUpperA")
        add_platform_span(objects, ids, 960, 4, 300, "FoundryUpperB")
        add_platform_span(objects, ids, 1600, 3, 290, "FoundryUpperC")

        objects.append(instantiate_from_template(templates["ArcaneTurret"], "ArcaneTurret", ids, name="FoundryTurret_0", x=348, y=294, script_overrides={"fireInterval": 2.0, "detectRange": 480.0}))
        objects.append(instantiate_from_template(templates["ArcaneTurret"], "ArcaneTurret", ids, name="FoundryTurret_1", x=1088, y=274, script_overrides={"fireInterval": 1.8, "detectRange": 500.0}))

        objects.append(instantiate_from_template(templates["JumpPad"], "JumpPad", ids, name="FoundryJump", x=180, y=408))
        objects.append(instantiate_from_template(templates["SpikeHazard"], "SpikeHazard", ids, name="FoundrySpikes", x=1040, y=408))
        objects.append(instantiate_from_template(templates["WaveSpawner"], "WaveSpawner", ids, name="FoundrySpawner", x=1190, y=402, script_overrides={"maxSpawns": 3, "spawnInterval": 4.0}))
        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="FoundryCrate_0", x=280, y=294))
        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="FoundryCrate_1", x=1664, y=264))

        for i, (gx, gy) in enumerate(((240, 284), (660, 350), (1024, 264), (1152, 264), (1600, 392), (1728, 392))):
            objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name=f"FoundryGem_{i}", x=gx, y=gy))
        objects.append(instantiate_from_template(templates["ShieldCore"], "ShieldCore", ids, name="FoundryShield", x=980, y=264))

        objects.append(instantiate_from_template(templates["LevelPortal"], "LevelPortal", ids, name="ExitPortal", x=1836, y=388, script_overrides={"targetScenePath": "assets/scenes/level3_sanctum.scene", "requiredGems": 3}))
        objects.append(instantiate_from_template(templates["DeathZone"], "DeathZone", ids, name="Abyss", x=960, y=560))

    finish_scene(
        "level2_foundry",
        "assets/scenes/level2_foundry.scene",
        "Crystal Citadel - Runic Foundry",
        "assets/scenes/level3_sanctum.scene",
        3,
        sfx["bgm_foundry"],
        96.0,
        380.0,
        1280.0,
        build_foundry,
    )

    # Scene 4: Level 3 - Archon Sanctum
    def build_sanctum(objects: list[dict[str, Any]], ids: IdGen) -> None:
        add_ground_span(objects, ids, -64, 960, 432, "SanctumFloor")
        for wy in (368, 304, 240, 176):
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallL_{wy}", x=-64, y=wy))
            objects.append(instantiate_from_template(templates["StoneGround"], "StoneGround", ids, name=f"WallR_{wy}", x=960, y=wy))

        add_platform_span(objects, ids, 128, 3, 320, "SanctumLeftBalcony")
        add_platform_span(objects, ids, 640, 3, 320, "SanctumRightBalcony")
        objects.append(instantiate_from_template(templates["JumpPad"], "JumpPad", ids, name="SanctumJumpL", x=84, y=408))
        objects.append(instantiate_from_template(templates["JumpPad"], "JumpPad", ids, name="SanctumJumpR", x=844, y=408))

        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="SanctumCrateL", x=192, y=294))
        objects.append(instantiate_from_template(templates["BreakableCrate"], "BreakableCrate", ids, name="SanctumCrateR", x=704, y=294))
        objects.append(instantiate_from_template(templates["ShieldCore"], "ShieldCore", ids, name="SanctumShield", x=152, y=284))
        objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name="SanctumGem_0", x=256, y=284))
        objects.append(instantiate_from_template(templates["CrystalGem"], "CrystalGem", ids, name="SanctumGem_1", x=640, y=284))

        objects.append(instantiate_from_template(templates["CrystalArchonBoss"], "CrystalArchonBoss", ids, name="CrystalArchon", x=480, y=210, script_overrides={"arenaMinX": 180.0, "arenaMaxX": 740.0, "maxHp": 12, "hp": 12}))
        objects.append(instantiate_from_template(templates["DeathZone"], "DeathZone", ids, name="Abyss", x=460, y=560))

    finish_scene(
        "level3_sanctum",
        "assets/scenes/level3_sanctum.scene",
        "Crystal Citadel - Archon Sanctum",
        "assets/scenes/hub.scene",
        0,
        sfx["bgm_sanctum"],
        130.0,
        380.0,
        360.0,
        build_sanctum,
    )

    print("All Crystal Citadel assets, templates, and scenes generated successfully!")


if __name__ == "__main__":
    main()
