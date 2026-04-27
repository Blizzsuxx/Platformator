#!/usr/bin/env python3
"""
Platformator Scene Editor
=========================
A visual level design tool for the Platformator game engine.

Usage:
    python tools/scene_editor.py [scene_file.scene]

Requirements:
    Python 3.8+, tkinter (standard library)
    Optional: Pillow (pip install Pillow) for sprite thumbnail previews
"""

from __future__ import annotations

import math
import os
import sys
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog
from copy import deepcopy
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ─────────────────────────────────────────────────────────────────────────────
# Optional Pillow support for sprite previews
# ─────────────────────────────────────────────────────────────────────────────
try:
    from PIL import Image, ImageTk  # type: ignore

    PILLOW_AVAILABLE = True
except ImportError:
    PILLOW_AVAILABLE = False

# ─────────────────────────────────────────────────────────────────────────────
# Constants
# ─────────────────────────────────────────────────────────────────────────────

SCREEN_WIDTH = 640
SCREEN_HEIGHT = 480
APP_TITLE = "Platformator Scene Editor"
CANVAS_BG = "#1a1a2e"
GRID_COLOR = "#2a2a4a"
VIEWPORT_COLOR = "#4444aa"
SELECTION_COLOR = "#ffee00"
OBJECT_FILL = "#2d5a8e"
OBJECT_OUTLINE = "#6699cc"
TRIGGER_FILL = "#1a3a1a"
TRIGGER_OUTLINE = "#44aa44"
CAMERA_FILL = "#00000000"
CAMERA_OUTLINE = "#4488ff"
SNAP_GRID = 16


# ─────────────────────────────────────────────────────────────────────────────
# Data model
# ─────────────────────────────────────────────────────────────────────────────


@dataclass
class RigidbodyConfig:
    body_type: str = "dynamic"  # dynamic | static | kinematic
    gravity: bool = True
    mass: float = 1.0
    velocity: Tuple[float, float] = (0.0, 0.0)
    force: Tuple[float, float] = (0.0, 0.0)
    angular_velocity: float = 0.0
    torque: float = 0.0
    friction: float = 1.0
    restitution: float = 0.0


@dataclass
class BoxColliderConfig:
    width: float = 32.0
    height: float = 32.0
    is_trigger: bool = False
    collision_group: int = 1
    collision_mask: int = 1


@dataclass
class CircleColliderConfig:
    radius: float = 16.0
    is_trigger: bool = False
    collision_group: int = 1
    collision_mask: int = 1


@dataclass
class SpriteConfig:
    path: str = ""
    flip: str = "none"  # none | horizontal | vertical | both
    width: float = 32.0
    height: float = 32.0


@dataclass
class CameraConfig:
    x: float = 0.0
    y: float = 0.0
    width: float = float(SCREEN_WIDTH)
    height: float = float(SCREEN_HEIGHT)


@dataclass
class AudioConfig:
    path: str = ""
    autoplay: bool = False
    loops: int = 0
    gain: float = 1.0


@dataclass
class AnimationFrame:
    path: str = ""
    duration: float = 0.0
    has_source_rect: bool = False
    source_rect: Tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0)


@dataclass
class AnimationClip:
    name: str = "clip"
    fps: float = 12.0
    loop: bool = True
    width: float = 0.0
    height: float = 0.0
    frames: List[AnimationFrame] = field(default_factory=list)


@dataclass
class AnimatorConfig:
    play: str = ""
    playback_speed: float = 1.0
    clips: List[AnimationClip] = field(default_factory=list)


@dataclass
class ScriptProperty:
    name: str = ""
    value: str = ""
    is_string: bool = False


@dataclass
class ScriptDescriptor:
    script_type: str = ""
    properties: List[ScriptProperty] = field(default_factory=list)


@dataclass
class SceneObject:
    name: str = "New Object"
    tag: str = ""
    active: bool = True
    position: Tuple[float, float] = (0.0, 0.0)
    scale: Tuple[float, float] = (1.0, 1.0)
    rotation: float = 0.0
    rigidbody: Optional[RigidbodyConfig] = None
    box_collider: Optional[BoxColliderConfig] = None
    circle_collider: Optional[CircleColliderConfig] = None
    sprite: Optional[SpriteConfig] = None
    camera: Optional[CameraConfig] = None
    audio: Optional[AudioConfig] = None
    animator: Optional[AnimatorConfig] = None
    scripts: List[ScriptDescriptor] = field(default_factory=list)

    def visual_width(self) -> float:
        """Return the best-guess display width for the canvas."""
        if self.box_collider:
            return self.box_collider.width
        if self.circle_collider:
            return self.circle_collider.radius * 2
        if self.sprite:
            return self.sprite.width
        if self.camera:
            return self.camera.width
        return 32.0

    def visual_height(self) -> float:
        """Return the best-guess display height for the canvas."""
        if self.box_collider:
            return self.box_collider.height
        if self.circle_collider:
            return self.circle_collider.radius * 2
        if self.sprite:
            return self.sprite.height
        if self.camera:
            return self.camera.height
        return 32.0

    def is_circle(self) -> bool:
        return self.circle_collider is not None and self.box_collider is None


# ─────────────────────────────────────────────────────────────────────────────
# Scene tokenizer
# ─────────────────────────────────────────────────────────────────────────────


@dataclass
class Token:
    value: str
    is_string: bool = False


def tokenize(text: str) -> List[Token]:
    tokens: List[Token] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        # Skip whitespace
        if c in " \t\r\n":
            i += 1
            continue
        # Line comment: # or //
        if c == "#":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            i += 2
            while i < n and text[i] != "\n":
                i += 1
            continue
        # Braces
        if c in "{}":
            tokens.append(Token(c, False))
            i += 1
            continue
        # Quoted string
        if c == '"':
            i += 1
            buf = []
            while i < n:
                sc = text[i]
                if sc == '"':
                    i += 1
                    break
                if sc == "\\" and i + 1 < n:
                    i += 1
                    buf.append(text[i])
                    i += 1
                    continue
                buf.append(sc)
                i += 1
            tokens.append(Token("".join(buf), True))
            continue
        # Bare word
        start = i
        while i < n:
            tc = text[i]
            if tc in " \t\r\n{}#":
                break
            if tc == "/" and i + 1 < n and text[i + 1] == "/":
                break
            i += 1
        tokens.append(Token(text[start:i], False))
    return tokens


# ─────────────────────────────────────────────────────────────────────────────
# Scene parser
# ─────────────────────────────────────────────────────────────────────────────


class ParseError(Exception):
    pass


class TokenStream:
    def __init__(self, tokens: List[Token]):
        self._tokens = tokens
        self._idx = 0

    def empty(self) -> bool:
        return self._idx >= len(self._tokens)

    def peek(self) -> Optional[Token]:
        if self.empty():
            return None
        return self._tokens[self._idx]

    def consume(self) -> Token:
        if self.empty():
            raise ParseError("Unexpected end of file")
        t = self._tokens[self._idx]
        self._idx += 1
        return t

    def consume_value(self) -> str:
        return self.consume().value

    def expect(self, value: str) -> None:
        t = self.consume()
        if t.value != value:
            raise ParseError(f"Expected '{value}' but found '{t.value}'")

    def match(self, value: str) -> bool:
        if not self.empty() and self._tokens[self._idx].value == value:
            self._idx += 1
            return True
        return False


def _parse_bool(s: str) -> bool:
    low = s.lower()
    if low in ("true", "1", "yes"):
        return True
    if low in ("false", "0", "no"):
        return False
    raise ParseError(f"Invalid boolean '{s}'")


def _parse_float(s: str) -> float:
    try:
        return float(s)
    except ValueError:
        raise ParseError(f"Invalid float '{s}'")


def _parse_int(s: str) -> int:
    try:
        return int(s)
    except ValueError:
        raise ParseError(f"Invalid int '{s}'")


def _parse_vec2(ts: TokenStream) -> Tuple[float, float]:
    return (_parse_float(ts.consume_value()), _parse_float(ts.consume_value()))


def _parse_rigidbody(ts: TokenStream) -> RigidbodyConfig:
    cfg = RigidbodyConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "bodytype":
            cfg.body_type = ts.consume_value().lower()
        elif key == "gravity":
            cfg.gravity = _parse_bool(ts.consume_value())
        elif key == "mass":
            cfg.mass = _parse_float(ts.consume_value())
        elif key == "velocity":
            cfg.velocity = _parse_vec2(ts)
        elif key == "force":
            cfg.force = _parse_vec2(ts)
        elif key == "angularvelocity":
            cfg.angular_velocity = _parse_float(ts.consume_value())
        elif key == "torque":
            cfg.torque = _parse_float(ts.consume_value())
        elif key == "friction":
            cfg.friction = _parse_float(ts.consume_value())
        elif key == "restitution":
            cfg.restitution = _parse_float(ts.consume_value())
        else:
            raise ParseError(f"Unknown rigidbody property '{key}'")
    return cfg


def _parse_box_collider(ts: TokenStream) -> BoxColliderConfig:
    cfg = BoxColliderConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "size":
            cfg.width = _parse_float(ts.consume_value())
            cfg.height = _parse_float(ts.consume_value())
        elif key == "width":
            cfg.width = _parse_float(ts.consume_value())
        elif key == "height":
            cfg.height = _parse_float(ts.consume_value())
        elif key in ("trigger", "istrigger"):
            cfg.is_trigger = _parse_bool(ts.consume_value())
        elif key in ("collisiongroup", "group"):
            cfg.collision_group = _parse_int(ts.consume_value())
        elif key in ("collisionmask", "mask"):
            cfg.collision_mask = _parse_int(ts.consume_value())
        else:
            raise ParseError(f"Unknown boxCollider property '{key}'")
    return cfg


def _parse_circle_collider(ts: TokenStream) -> CircleColliderConfig:
    cfg = CircleColliderConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "radius":
            cfg.radius = _parse_float(ts.consume_value())
        elif key in ("trigger", "istrigger"):
            cfg.is_trigger = _parse_bool(ts.consume_value())
        elif key in ("collisiongroup", "group"):
            cfg.collision_group = _parse_int(ts.consume_value())
        elif key in ("collisionmask", "mask"):
            cfg.collision_mask = _parse_int(ts.consume_value())
        else:
            raise ParseError(f"Unknown circleCollider property '{key}'")
    return cfg


def _parse_sprite(ts: TokenStream) -> SpriteConfig:
    cfg = SpriteConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "path":
            cfg.path = ts.consume_value()
        elif key == "flip":
            cfg.flip = ts.consume_value().lower()
        elif key == "size":
            cfg.width = _parse_float(ts.consume_value())
            cfg.height = _parse_float(ts.consume_value())
        elif key == "width":
            cfg.width = _parse_float(ts.consume_value())
        elif key == "height":
            cfg.height = _parse_float(ts.consume_value())
        else:
            raise ParseError(f"Unknown sprite property '{key}'")
    return cfg


def _parse_camera(ts: TokenStream) -> CameraConfig:
    cfg = CameraConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "viewport":
            cfg.x = _parse_float(ts.consume_value())
            cfg.y = _parse_float(ts.consume_value())
            cfg.width = _parse_float(ts.consume_value())
            cfg.height = _parse_float(ts.consume_value())
        elif key == "position":
            cfg.x = _parse_float(ts.consume_value())
            cfg.y = _parse_float(ts.consume_value())
        elif key == "size":
            cfg.width = _parse_float(ts.consume_value())
            cfg.height = _parse_float(ts.consume_value())
        else:
            raise ParseError(f"Unknown camera property '{key}'")
    return cfg


def _parse_audio(ts: TokenStream) -> AudioConfig:
    cfg = AudioConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "path":
            cfg.path = ts.consume_value()
        elif key == "autoplay":
            cfg.autoplay = _parse_bool(ts.consume_value())
        elif key == "loops":
            cfg.loops = _parse_int(ts.consume_value())
        elif key == "gain":
            cfg.gain = _parse_float(ts.consume_value())
        else:
            raise ParseError(f"Unknown audio property '{key}'")
    return cfg


def _parse_animation_clip(ts: TokenStream) -> AnimationClip:
    clip = AnimationClip()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "name":
            clip.name = ts.consume_value()
        elif key == "fps":
            clip.fps = _parse_float(ts.consume_value())
        elif key == "loop":
            clip.loop = _parse_bool(ts.consume_value())
        elif key == "size":
            clip.width = _parse_float(ts.consume_value())
            clip.height = _parse_float(ts.consume_value())
        elif key == "width":
            clip.width = _parse_float(ts.consume_value())
        elif key == "height":
            clip.height = _parse_float(ts.consume_value())
        elif key == "frame":
            frame = AnimationFrame()
            if ts.match("{"):
                while not ts.match("}"):
                    fkey = ts.consume_value().lower()
                    if fkey == "path":
                        frame.path = ts.consume_value()
                    elif fkey == "duration":
                        frame.duration = _parse_float(ts.consume_value())
                    elif fkey in ("rect", "sourcerect"):
                        x = _parse_float(ts.consume_value())
                        y = _parse_float(ts.consume_value())
                        w = _parse_float(ts.consume_value())
                        h = _parse_float(ts.consume_value())
                        frame.source_rect = (x, y, w, h)
                        frame.has_source_rect = True
                    else:
                        raise ParseError(f"Unknown frame property '{fkey}'")
            else:
                frame.path = ts.consume_value()
            clip.frames.append(frame)
        else:
            raise ParseError(f"Unknown clip property '{key}'")
    return clip


def _parse_animator(ts: TokenStream) -> AnimatorConfig:
    cfg = AnimatorConfig()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "play":
            cfg.play = ts.consume_value()
        elif key in ("playbackspeed", "speed"):
            cfg.playback_speed = _parse_float(ts.consume_value())
        elif key == "clip":
            cfg.clips.append(_parse_animation_clip(ts))
        else:
            raise ParseError(f"Unknown animator property '{key}'")
    return cfg


def _parse_script(ts: TokenStream) -> ScriptDescriptor:
    desc = ScriptDescriptor()
    ts.expect("{")
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "type":
            desc.script_type = ts.consume_value()
        else:
            val_token = ts.consume()
            desc.properties.append(ScriptProperty(key, val_token.value, val_token.is_string))
    if not desc.script_type:
        raise ParseError("script block requires a 'type'")
    return desc


def _parse_object(ts: TokenStream) -> SceneObject:
    kw = ts.consume_value().lower()
    if kw != "object":
        raise ParseError(f"Expected 'object' but found '{kw}'")
    ts.expect("{")
    obj = SceneObject()
    while not ts.match("}"):
        key = ts.consume_value().lower()
        if key == "name":
            obj.name = ts.consume_value()
        elif key == "tag":
            obj.tag = ts.consume_value()
        elif key == "active":
            obj.active = _parse_bool(ts.consume_value())
        elif key == "position":
            obj.position = _parse_vec2(ts)
        elif key == "scale":
            obj.scale = _parse_vec2(ts)
        elif key == "rotation":
            obj.rotation = _parse_float(ts.consume_value())
        elif key == "rigidbody":
            obj.rigidbody = _parse_rigidbody(ts)
        elif key == "boxcollider":
            obj.box_collider = _parse_box_collider(ts)
        elif key == "circlecollider":
            obj.circle_collider = _parse_circle_collider(ts)
        elif key == "sprite":
            obj.sprite = _parse_sprite(ts)
        elif key == "camera":
            obj.camera = _parse_camera(ts)
        elif key == "audio":
            obj.audio = _parse_audio(ts)
        elif key == "animator":
            obj.animator = _parse_animator(ts)
        elif key == "script":
            obj.scripts.append(_parse_script(ts))
        else:
            raise ParseError(f"Unknown object property '{key}'")
    if obj.box_collider and obj.circle_collider:
        raise ParseError("Object cannot have both boxCollider and circleCollider")
    return obj


def parse_scene(text: str) -> List[SceneObject]:
    tokens = tokenize(text)
    ts = TokenStream(tokens)
    objects: List[SceneObject] = []
    while not ts.empty():
        objects.append(_parse_object(ts))
    return objects


# ─────────────────────────────────────────────────────────────────────────────
# Scene writer
# ─────────────────────────────────────────────────────────────────────────────


def _fmt_float(v: float) -> str:
    """Format a float matching the engine's precision output."""
    s = f"{v:.9g}"
    return s


def _fmt_bool(v: bool) -> str:
    return "true" if v else "false"


def _escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def _q(s: str) -> str:
    return f'"{_escape(s)}"'


def _ind(level: int) -> str:
    return "    " * level


def write_scene(objects: List[SceneObject]) -> str:
    parts: List[str] = []
    first = True
    for obj in objects:
        if not first:
            parts.append("\n")
        first = False

        lines: List[str] = []
        lines.append("object {")
        lines.append(f'{_ind(1)}name {_q(obj.name)}')
        if obj.tag:
            lines.append(f'{_ind(1)}tag {_q(obj.tag)}')
        lines.append(f'{_ind(1)}active {_fmt_bool(obj.active)}')
        px, py = obj.position
        lines.append(f'{_ind(1)}position {_fmt_float(px)} {_fmt_float(py)}')
        lines.append(f'{_ind(1)}rotation {_fmt_float(obj.rotation)}')
        sx, sy = obj.scale
        lines.append(f'{_ind(1)}scale {_fmt_float(sx)} {_fmt_float(sy)}')

        # camera
        if obj.camera is not None:
            c = obj.camera
            lines.append(f'{_ind(1)}camera {{')
            lines.append(
                f'{_ind(2)}viewport {_fmt_float(c.x)} {_fmt_float(c.y)} '
                f'{_fmt_float(c.width)} {_fmt_float(c.height)}'
            )
            lines.append(f'{_ind(1)}}}')

        # rigidbody
        if obj.rigidbody is not None:
            r = obj.rigidbody
            lines.append(f'{_ind(1)}rigidbody {{')
            lines.append(f'{_ind(2)}bodyType {r.body_type}')
            lines.append(f'{_ind(2)}gravity {_fmt_bool(r.gravity)}')
            lines.append(f'{_ind(2)}mass {_fmt_float(r.mass)}')
            vx, vy = r.velocity
            lines.append(f'{_ind(2)}velocity {_fmt_float(vx)} {_fmt_float(vy)}')
            fx, fy = r.force
            lines.append(f'{_ind(2)}force {_fmt_float(fx)} {_fmt_float(fy)}')
            lines.append(f'{_ind(2)}angularVelocity {_fmt_float(r.angular_velocity)}')
            lines.append(f'{_ind(2)}torque {_fmt_float(r.torque)}')
            lines.append(f'{_ind(2)}friction {_fmt_float(r.friction)}')
            lines.append(f'{_ind(2)}restitution {_fmt_float(r.restitution)}')
            lines.append(f'{_ind(1)}}}')

        # boxCollider
        if obj.box_collider is not None:
            bc = obj.box_collider
            lines.append(f'{_ind(1)}boxCollider {{')
            lines.append(f'{_ind(2)}size {_fmt_float(bc.width)} {_fmt_float(bc.height)}')
            lines.append(f'{_ind(2)}trigger {_fmt_bool(bc.is_trigger)}')
            lines.append(f'{_ind(2)}collisionGroup {bc.collision_group}')
            lines.append(f'{_ind(2)}collisionMask {bc.collision_mask}')
            lines.append(f'{_ind(1)}}}')

        # circleCollider
        if obj.circle_collider is not None:
            cc = obj.circle_collider
            lines.append(f'{_ind(1)}circleCollider {{')
            lines.append(f'{_ind(2)}radius {_fmt_float(cc.radius)}')
            lines.append(f'{_ind(2)}trigger {_fmt_bool(cc.is_trigger)}')
            lines.append(f'{_ind(2)}collisionGroup {cc.collision_group}')
            lines.append(f'{_ind(2)}collisionMask {cc.collision_mask}')
            lines.append(f'{_ind(1)}}}')

        # sprite
        if obj.sprite is not None:
            sp = obj.sprite
            lines.append(f'{_ind(1)}sprite {{')
            if sp.path:
                lines.append(f'{_ind(2)}path {_q(sp.path)}')
            lines.append(f'{_ind(2)}flip {sp.flip}')
            lines.append(f'{_ind(2)}size {_fmt_float(sp.width)} {_fmt_float(sp.height)}')
            lines.append(f'{_ind(1)}}}')

        # audio
        if obj.audio is not None:
            au = obj.audio
            lines.append(f'{_ind(1)}audio {{')
            if au.path:
                lines.append(f'{_ind(2)}path {_q(au.path)}')
            lines.append(f'{_ind(2)}autoplay {_fmt_bool(au.autoplay)}')
            lines.append(f'{_ind(2)}loops {au.loops}')
            lines.append(f'{_ind(2)}gain {_fmt_float(au.gain)}')
            lines.append(f'{_ind(1)}}}')

        # animator
        if obj.animator is not None:
            an = obj.animator
            lines.append(f'{_ind(1)}animator {{')
            if an.play:
                lines.append(f'{_ind(2)}play {_q(an.play)}')
            lines.append(f'{_ind(2)}playbackSpeed {_fmt_float(an.playback_speed)}')
            for clip in an.clips:
                lines.append(f'{_ind(2)}clip {{')
                lines.append(f'{_ind(3)}name {_q(clip.name)}')
                lines.append(f'{_ind(3)}fps {_fmt_float(clip.fps)}')
                lines.append(f'{_ind(3)}loop {_fmt_bool(clip.loop)}')
                if clip.width > 0 or clip.height > 0:
                    lines.append(
                        f'{_ind(3)}size {_fmt_float(clip.width)} {_fmt_float(clip.height)}'
                    )
                for fr in clip.frames:
                    if fr.duration > 0 or fr.has_source_rect:
                        lines.append(f'{_ind(3)}frame {{')
                        lines.append(f'{_ind(4)}path {_q(fr.path)}')
                        if fr.duration > 0:
                            lines.append(f'{_ind(4)}duration {_fmt_float(fr.duration)}')
                        if fr.has_source_rect:
                            rx, ry, rw, rh = fr.source_rect
                            lines.append(
                                f'{_ind(4)}rect {_fmt_float(rx)} {_fmt_float(ry)} '
                                f'{_fmt_float(rw)} {_fmt_float(rh)}'
                            )
                        lines.append(f'{_ind(3)}}}')
                    else:
                        lines.append(f'{_ind(3)}frame {_q(fr.path)}')
                lines.append(f'{_ind(2)}}}')
            lines.append(f'{_ind(1)}}}')

        # scripts
        for script in obj.scripts:
            if not script.script_type:
                continue
            lines.append(f'{_ind(1)}script {{')
            lines.append(f'{_ind(2)}type {_q(script.script_type)}')
            for prop in script.properties:
                if prop.is_string:
                    lines.append(f'{_ind(2)}{prop.name} {_q(prop.value)}')
                else:
                    lines.append(f'{_ind(2)}{prop.name} {prop.value}')
            lines.append(f'{_ind(1)}}}')

        lines.append("}")
        parts.append("\n".join(lines) + "\n")

    return "".join(parts)


# ─────────────────────────────────────────────────────────────────────────────
# GUI helpers
# ─────────────────────────────────────────────────────────────────────────────


class ScrollableFrame(ttk.Frame):
    """A frame that can be scrolled vertically."""

    def __init__(self, parent, **kwargs):
        super().__init__(parent, **kwargs)
        self.canvas = tk.Canvas(self, borderwidth=0, highlightthickness=0)
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.inner = ttk.Frame(self.canvas)
        self.inner_id = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")

        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")

        self.inner.bind("<Configure>", self._on_frame_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)
        self.canvas.bind("<Enter>", self._bind_mousewheel)
        self.canvas.bind("<Leave>", self._unbind_mousewheel)

    def _on_frame_configure(self, _event=None):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas_configure(self, event):
        self.canvas.itemconfig(self.inner_id, width=event.width)

    def _bind_mousewheel(self, _event=None):
        self.canvas.bind_all("<MouseWheel>", self._on_mousewheel)
        self.canvas.bind_all("<Button-4>", self._on_mousewheel)
        self.canvas.bind_all("<Button-5>", self._on_mousewheel)

    def _unbind_mousewheel(self, _event=None):
        self.canvas.unbind_all("<MouseWheel>")
        self.canvas.unbind_all("<Button-4>")
        self.canvas.unbind_all("<Button-5>")

    def _on_mousewheel(self, event):
        if event.num == 4:
            self.canvas.yview_scroll(-1, "units")
        elif event.num == 5:
            self.canvas.yview_scroll(1, "units")
        else:
            self.canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")


def _make_label_entry(parent, label: str, row: int, var: tk.Variable, width: int = 14):
    ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=2)
    e = ttk.Entry(parent, textvariable=var, width=width)
    e.grid(row=row, column=1, sticky="ew", padx=4, pady=2)
    return e


def _make_label_check(parent, label: str, row: int, var: tk.BooleanVar):
    ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=2)
    cb = ttk.Checkbutton(parent, variable=var)
    cb.grid(row=row, column=1, sticky="w", padx=4, pady=2)
    return cb


def _make_label_combo(parent, label: str, row: int, var: tk.StringVar, values: list, width: int = 12):
    ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=2)
    cb = ttk.Combobox(parent, textvariable=var, values=values, width=width, state="readonly")
    cb.grid(row=row, column=1, sticky="ew", padx=4, pady=2)
    return cb


def _section_header(parent, text: str) -> ttk.Frame:
    """Create a styled section header."""
    frame = ttk.Frame(parent)
    frame.pack(fill="x", pady=(8, 2))
    ttk.Separator(frame, orient="horizontal").pack(fill="x", pady=2)
    ttk.Label(frame, text=text, font=("TkDefaultFont", 9, "bold")).pack(anchor="w", padx=4)
    return frame


# ─────────────────────────────────────────────────────────────────────────────
# Properties panel
# ─────────────────────────────────────────────────────────────────────────────


class PropertiesPanel(ttk.Frame):
    """Right-side panel showing editable properties of the selected object."""

    def __init__(self, parent, on_change_callback):
        super().__init__(parent, width=300)
        self.on_change = on_change_callback
        self._obj: Optional[SceneObject] = None
        self._vars: dict = {}
        self._suppress = False

        # Title label
        self._title_var = tk.StringVar(value="No selection")
        ttk.Label(self, textvariable=self._title_var, font=("TkDefaultFont", 10, "bold")).pack(
            fill="x", padx=6, pady=4
        )
        ttk.Separator(self, orient="horizontal").pack(fill="x")

        # Scrollable content area
        self._scroll = ScrollableFrame(self)
        self._scroll.pack(fill="both", expand=True)
        self._content = self._scroll.inner

        # Add-component area at bottom
        add_frame = ttk.Frame(self)
        add_frame.pack(fill="x", padx=4, pady=4)
        ttk.Label(add_frame, text="Add component:").pack(side="left")
        self._add_comp_var = tk.StringVar()
        comp_choices = [
            "rigidbody", "boxCollider", "circleCollider",
            "sprite", "camera", "audio", "animator", "script",
        ]
        cb = ttk.Combobox(
            add_frame, textvariable=self._add_comp_var,
            values=comp_choices, width=14, state="readonly"
        )
        cb.pack(side="left", padx=4)
        ttk.Button(add_frame, text="Add", command=self._add_component).pack(side="left")

    # ── public ────────────────────────────────────────────────────────────────

    def load_object(self, obj: Optional[SceneObject]):
        self._obj = obj
        self._rebuild()

    # ── internal ──────────────────────────────────────────────────────────────

    def _rebuild(self):
        for w in self._content.winfo_children():
            w.destroy()
        self._vars = {}

        if self._obj is None:
            self._title_var.set("No selection")
            return

        self._title_var.set(f"Object: {self._obj.name}")
        self._build_object_section()

        if self._obj.rigidbody is not None:
            self._build_rigidbody_section()
        if self._obj.box_collider is not None:
            self._build_box_collider_section()
        if self._obj.circle_collider is not None:
            self._build_circle_collider_section()
        if self._obj.sprite is not None:
            self._build_sprite_section()
        if self._obj.camera is not None:
            self._build_camera_section()
        if self._obj.audio is not None:
            self._build_audio_section()
        if self._obj.animator is not None:
            self._build_animator_section()
        for i, script in enumerate(self._obj.scripts):
            self._build_script_section(i, script)

    def _track(self, name: str, var: tk.Variable, writer):
        """Register a variable to call writer() and then on_change when it changes."""
        self._vars[name] = var

        def _cb(*_):
            if self._suppress or self._obj is None:
                return
            try:
                writer(var.get())
            except (ValueError, tk.TclError):
                pass
            self.on_change()

        var.trace_add("write", _cb)

    def _build_object_section(self):
        obj = self._obj
        _section_header(self._content, "Object")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        name_var = tk.StringVar(value=obj.name)
        self._track("name", name_var, lambda v: setattr(obj, "name", v))
        _make_label_entry(f, "Name", 0, name_var)

        tag_var = tk.StringVar(value=obj.tag)
        self._track("tag", tag_var, lambda v: setattr(obj, "tag", v))
        _make_label_entry(f, "Tag", 1, tag_var)

        active_var = tk.BooleanVar(value=obj.active)
        self._track("active", active_var, lambda v: setattr(obj, "active", v))
        _make_label_check(f, "Active", 2, active_var)

        px_var = tk.StringVar(value=str(obj.position[0]))
        self._track("pos_x", px_var, lambda v: setattr(obj, "position", (float(v), obj.position[1])))
        _make_label_entry(f, "Position X", 3, px_var)

        py_var = tk.StringVar(value=str(obj.position[1]))
        self._track("pos_y", py_var, lambda v: setattr(obj, "position", (obj.position[0], float(v))))
        _make_label_entry(f, "Position Y", 4, py_var)

        rot_var = tk.StringVar(value=str(math.degrees(obj.rotation)))
        self._track(
            "rotation", rot_var,
            lambda v: setattr(obj, "rotation", math.radians(float(v)))
        )
        _make_label_entry(f, "Rotation (°)", 5, rot_var)

        sx_var = tk.StringVar(value=str(obj.scale[0]))
        self._track("scale_x", sx_var, lambda v: setattr(obj, "scale", (float(v), obj.scale[1])))
        _make_label_entry(f, "Scale X", 6, sx_var)

        sy_var = tk.StringVar(value=str(obj.scale[1]))
        self._track("scale_y", sy_var, lambda v: setattr(obj, "scale", (obj.scale[0], float(v))))
        _make_label_entry(f, "Scale Y", 7, sy_var)

    def _remove_btn(self, parent, component_name: str, attr: str):
        def _remove():
            if messagebox.askyesno("Remove", f"Remove {component_name}?"):
                setattr(self._obj, attr, None)
                self._rebuild()
                self.on_change()
        ttk.Button(parent, text=f"✕ Remove {component_name}", command=_remove).pack(
            anchor="e", padx=4, pady=2
        )

    def _build_rigidbody_section(self):
        obj = self._obj
        r = obj.rigidbody
        _section_header(self._content, "Rigidbody")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        bt_var = tk.StringVar(value=r.body_type)
        self._track("rb_bodytype", bt_var, lambda v: setattr(r, "body_type", v))
        _make_label_combo(f, "Body Type", 0, bt_var, ["dynamic", "static", "kinematic"])

        grav_var = tk.BooleanVar(value=r.gravity)
        self._track("rb_gravity", grav_var, lambda v: setattr(r, "gravity", v))
        _make_label_check(f, "Gravity", 1, grav_var)

        mass_var = tk.StringVar(value=str(r.mass))
        self._track("rb_mass", mass_var, lambda v: setattr(r, "mass", float(v)))
        _make_label_entry(f, "Mass", 2, mass_var)

        fric_var = tk.StringVar(value=str(r.friction))
        self._track("rb_friction", fric_var, lambda v: setattr(r, "friction", float(v)))
        _make_label_entry(f, "Friction", 3, fric_var)

        rest_var = tk.StringVar(value=str(r.restitution))
        self._track("rb_restitution", rest_var, lambda v: setattr(r, "restitution", float(v)))
        _make_label_entry(f, "Restitution", 4, rest_var)

        vx_var = tk.StringVar(value=str(r.velocity[0]))
        self._track("rb_vx", vx_var, lambda v: setattr(r, "velocity", (float(v), r.velocity[1])))
        _make_label_entry(f, "Velocity X", 5, vx_var)

        vy_var = tk.StringVar(value=str(r.velocity[1]))
        self._track("rb_vy", vy_var, lambda v: setattr(r, "velocity", (r.velocity[0], float(v))))
        _make_label_entry(f, "Velocity Y", 6, vy_var)

        self._remove_btn(self._content, "Rigidbody", "rigidbody")

    def _build_box_collider_section(self):
        obj = self._obj
        bc = obj.box_collider
        _section_header(self._content, "Box Collider")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        w_var = tk.StringVar(value=str(bc.width))
        self._track("bc_w", w_var, lambda v: setattr(bc, "width", float(v)))
        _make_label_entry(f, "Width", 0, w_var)

        h_var = tk.StringVar(value=str(bc.height))
        self._track("bc_h", h_var, lambda v: setattr(bc, "height", float(v)))
        _make_label_entry(f, "Height", 1, h_var)

        trig_var = tk.BooleanVar(value=bc.is_trigger)
        self._track("bc_trig", trig_var, lambda v: setattr(bc, "is_trigger", v))
        _make_label_check(f, "Is Trigger", 2, trig_var)

        grp_var = tk.StringVar(value=str(bc.collision_group))
        self._track("bc_grp", grp_var, lambda v: setattr(bc, "collision_group", int(v)))
        _make_label_entry(f, "Col. Group", 3, grp_var)

        msk_var = tk.StringVar(value=str(bc.collision_mask))
        self._track("bc_msk", msk_var, lambda v: setattr(bc, "collision_mask", int(v)))
        _make_label_entry(f, "Col. Mask", 4, msk_var)

        self._remove_btn(self._content, "BoxCollider", "box_collider")

    def _build_circle_collider_section(self):
        obj = self._obj
        cc = obj.circle_collider
        _section_header(self._content, "Circle Collider")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        r_var = tk.StringVar(value=str(cc.radius))
        self._track("cc_r", r_var, lambda v: setattr(cc, "radius", float(v)))
        _make_label_entry(f, "Radius", 0, r_var)

        trig_var = tk.BooleanVar(value=cc.is_trigger)
        self._track("cc_trig", trig_var, lambda v: setattr(cc, "is_trigger", v))
        _make_label_check(f, "Is Trigger", 1, trig_var)

        grp_var = tk.StringVar(value=str(cc.collision_group))
        self._track("cc_grp", grp_var, lambda v: setattr(cc, "collision_group", int(v)))
        _make_label_entry(f, "Col. Group", 2, grp_var)

        msk_var = tk.StringVar(value=str(cc.collision_mask))
        self._track("cc_msk", msk_var, lambda v: setattr(cc, "collision_mask", int(v)))
        _make_label_entry(f, "Col. Mask", 3, msk_var)

        self._remove_btn(self._content, "CircleCollider", "circle_collider")

    def _build_sprite_section(self):
        obj = self._obj
        sp = obj.sprite
        _section_header(self._content, "Sprite")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        path_var = tk.StringVar(value=sp.path)
        self._track("sp_path", path_var, lambda v: setattr(sp, "path", v))
        ttk.Label(f, text="Path").grid(row=0, column=0, sticky="w", padx=4, pady=2)
        path_frame = ttk.Frame(f)
        path_frame.grid(row=0, column=1, sticky="ew", padx=4, pady=2)
        path_frame.columnconfigure(0, weight=1)
        ttk.Entry(path_frame, textvariable=path_var).grid(row=0, column=0, sticky="ew")

        def _browse():
            p = filedialog.askopenfilename(
                title="Select sprite image",
                filetypes=[("Image files", "*.png *.jpg *.jpeg *.bmp"), ("All", "*.*")]
            )
            if p:
                path_var.set(p)

        ttk.Button(path_frame, text="…", width=3, command=_browse).grid(row=0, column=1)

        flip_var = tk.StringVar(value=sp.flip)
        self._track("sp_flip", flip_var, lambda v: setattr(sp, "flip", v))
        _make_label_combo(f, "Flip", 1, flip_var, ["none", "horizontal", "vertical", "both"])

        sw_var = tk.StringVar(value=str(sp.width))
        self._track("sp_w", sw_var, lambda v: setattr(sp, "width", float(v)))
        _make_label_entry(f, "Width", 2, sw_var)

        sh_var = tk.StringVar(value=str(sp.height))
        self._track("sp_h", sh_var, lambda v: setattr(sp, "height", float(v)))
        _make_label_entry(f, "Height", 3, sh_var)

        self._remove_btn(self._content, "Sprite", "sprite")

    def _build_camera_section(self):
        obj = self._obj
        cam = obj.camera
        _section_header(self._content, "Camera")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        cx_var = tk.StringVar(value=str(cam.x))
        self._track("cam_x", cx_var, lambda v: setattr(cam, "x", float(v)))
        _make_label_entry(f, "Viewport X", 0, cx_var)

        cy_var = tk.StringVar(value=str(cam.y))
        self._track("cam_y", cy_var, lambda v: setattr(cam, "y", float(v)))
        _make_label_entry(f, "Viewport Y", 1, cy_var)

        cw_var = tk.StringVar(value=str(cam.width))
        self._track("cam_w", cw_var, lambda v: setattr(cam, "width", float(v)))
        _make_label_entry(f, "Width", 2, cw_var)

        ch_var = tk.StringVar(value=str(cam.height))
        self._track("cam_h", ch_var, lambda v: setattr(cam, "height", float(v)))
        _make_label_entry(f, "Height", 3, ch_var)

        self._remove_btn(self._content, "Camera", "camera")

    def _build_audio_section(self):
        obj = self._obj
        au = obj.audio
        _section_header(self._content, "Audio")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        path_var = tk.StringVar(value=au.path)
        self._track("au_path", path_var, lambda v: setattr(au, "path", v))
        ttk.Label(f, text="Path").grid(row=0, column=0, sticky="w", padx=4, pady=2)
        path_frame = ttk.Frame(f)
        path_frame.grid(row=0, column=1, sticky="ew", padx=4, pady=2)
        path_frame.columnconfigure(0, weight=1)
        ttk.Entry(path_frame, textvariable=path_var).grid(row=0, column=0, sticky="ew")

        def _browse():
            p = filedialog.askopenfilename(
                title="Select audio file",
                filetypes=[("Audio files", "*.wav *.ogg *.mp3"), ("All", "*.*")]
            )
            if p:
                path_var.set(p)

        ttk.Button(path_frame, text="…", width=3, command=_browse).grid(row=0, column=1)

        auto_var = tk.BooleanVar(value=au.autoplay)
        self._track("au_auto", auto_var, lambda v: setattr(au, "autoplay", v))
        _make_label_check(f, "Autoplay", 1, auto_var)

        loops_var = tk.StringVar(value=str(au.loops))
        self._track("au_loops", loops_var, lambda v: setattr(au, "loops", int(v)))
        _make_label_entry(f, "Loops", 2, loops_var)

        gain_var = tk.StringVar(value=str(au.gain))
        self._track("au_gain", gain_var, lambda v: setattr(au, "gain", float(v)))
        _make_label_entry(f, "Gain", 3, gain_var)

        self._remove_btn(self._content, "Audio", "audio")

    def _build_animator_section(self):
        obj = self._obj
        an = obj.animator
        _section_header(self._content, "Animator")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        play_var = tk.StringVar(value=an.play)
        self._track("an_play", play_var, lambda v: setattr(an, "play", v))
        _make_label_entry(f, "Play", 0, play_var)

        speed_var = tk.StringVar(value=str(an.playback_speed))
        self._track("an_speed", speed_var, lambda v: setattr(an, "playback_speed", float(v)))
        _make_label_entry(f, "Speed", 1, speed_var)

        # Clips summary (read-only list with add/remove)
        clips_frame = ttk.LabelFrame(self._content, text=f"Clips ({len(an.clips)})")
        clips_frame.pack(fill="x", padx=4, pady=4)
        for i, clip in enumerate(an.clips):
            cf = ttk.Frame(clips_frame)
            cf.pack(fill="x", padx=4, pady=1)
            ttk.Label(cf, text=f"[{i}] {clip.name or '(unnamed)'}  {len(clip.frames)} frame(s)").pack(side="left")

            def _remove_clip(idx=i):
                an.clips.pop(idx)
                self._rebuild()
                self.on_change()

            ttk.Button(cf, text="✕", width=2, command=_remove_clip).pack(side="right")

        def _add_clip():
            name = simpledialog.askstring("Clip name", "Enter clip name:", parent=self)
            if name:
                an.clips.append(AnimationClip(name=name))
                self._rebuild()
                self.on_change()

        ttk.Button(clips_frame, text="+ Add Clip", command=_add_clip).pack(anchor="w", padx=4, pady=2)
        self._remove_btn(self._content, "Animator", "animator")

    def _build_script_section(self, idx: int, script: ScriptDescriptor):
        _section_header(self._content, f"Script: {script.script_type}")
        f = ttk.Frame(self._content)
        f.pack(fill="x", padx=4)
        f.columnconfigure(1, weight=1)

        type_var = tk.StringVar(value=script.script_type)
        self._track(f"sc{idx}_type", type_var, lambda v: setattr(script, "script_type", v))
        _make_label_entry(f, "Type", 0, type_var)

        # Properties
        props_frame = ttk.LabelFrame(self._content, text="Properties")
        props_frame.pack(fill="x", padx=4, pady=2)

        for pi, prop in enumerate(script.properties):
            pf = ttk.Frame(props_frame)
            pf.pack(fill="x", padx=2, pady=1)
            pf.columnconfigure(1, weight=1)
            pf.columnconfigure(2, weight=1)

            name_v = tk.StringVar(value=prop.name)
            val_v = tk.StringVar(value=prop.value)
            str_v = tk.BooleanVar(value=prop.is_string)

            def _mk_name_writer(p=prop):
                return lambda v: setattr(p, "name", v)

            def _mk_val_writer(p=prop):
                return lambda v: setattr(p, "value", v)

            def _mk_str_writer(p=prop):
                return lambda v: setattr(p, "is_string", v)

            self._track(f"sc{idx}_p{pi}_n", name_v, _mk_name_writer())
            self._track(f"sc{idx}_p{pi}_v", val_v, _mk_val_writer())
            self._track(f"sc{idx}_p{pi}_s", str_v, _mk_str_writer())

            ttk.Entry(pf, textvariable=name_v, width=12).grid(row=0, column=0, sticky="ew", padx=2)
            ttk.Entry(pf, textvariable=val_v, width=12).grid(row=0, column=1, sticky="ew", padx=2)
            ttk.Checkbutton(pf, text="str", variable=str_v).grid(row=0, column=2, padx=2)

            def _remove_prop(p_idx=pi):
                script.properties.pop(p_idx)
                self._rebuild()
                self.on_change()

            ttk.Button(pf, text="✕", width=2, command=_remove_prop).grid(row=0, column=3)

        def _add_prop():
            script.properties.append(ScriptProperty(name="key", value="value"))
            self._rebuild()
            self.on_change()

        def _remove_script():
            self._obj.scripts.pop(idx)
            self._rebuild()
            self.on_change()

        ttk.Button(props_frame, text="+ Add Property", command=_add_prop).pack(
            anchor="w", padx=4, pady=2
        )
        ttk.Button(
            self._content, text=f"✕ Remove Script '{script.script_type}'",
            command=_remove_script
        ).pack(anchor="e", padx=4, pady=2)

    def _add_component(self):
        if self._obj is None:
            return
        comp = self._add_comp_var.get()
        if not comp:
            return
        if comp == "rigidbody":
            if self._obj.rigidbody is not None:
                messagebox.showinfo("Info", "Object already has a Rigidbody.")
                return
            self._obj.rigidbody = RigidbodyConfig()
        elif comp == "boxCollider":
            if self._obj.box_collider or self._obj.circle_collider:
                messagebox.showinfo("Info", "Object already has a collider.")
                return
            self._obj.box_collider = BoxColliderConfig()
        elif comp == "circleCollider":
            if self._obj.box_collider or self._obj.circle_collider:
                messagebox.showinfo("Info", "Object already has a collider.")
                return
            self._obj.circle_collider = CircleColliderConfig()
        elif comp == "sprite":
            if self._obj.sprite is not None:
                messagebox.showinfo("Info", "Object already has a Sprite.")
                return
            self._obj.sprite = SpriteConfig()
        elif comp == "camera":
            if self._obj.camera is not None:
                messagebox.showinfo("Info", "Object already has a Camera.")
                return
            self._obj.camera = CameraConfig()
        elif comp == "audio":
            if self._obj.audio is not None:
                messagebox.showinfo("Info", "Object already has an Audio component.")
                return
            self._obj.audio = AudioConfig()
        elif comp == "animator":
            if self._obj.animator is not None:
                messagebox.showinfo("Info", "Object already has an Animator.")
                return
            self._obj.animator = AnimatorConfig()
        elif comp == "script":
            st = simpledialog.askstring("Script type", "Enter script type name:", parent=self)
            if st:
                self._obj.scripts.append(ScriptDescriptor(script_type=st))
            else:
                return
        self._add_comp_var.set("")
        self._rebuild()
        self.on_change()

    def sync_position(self, obj: SceneObject):
        """Update position display vars without triggering a full rebuild."""
        if self._obj is not obj:
            return
        self._suppress = True
        try:
            if "pos_x" in self._vars:
                self._vars["pos_x"].set(str(obj.position[0]))
            if "pos_y" in self._vars:
                self._vars["pos_y"].set(str(obj.position[1]))
        finally:
            self._suppress = False


# ─────────────────────────────────────────────────────────────────────────────
# Editor canvas
# ─────────────────────────────────────────────────────────────────────────────


PALETTE = [
    "#2d5a8e", "#8e2d5a", "#5a8e2d", "#8e5a2d", "#2d8e5a",
    "#5a2d8e", "#8e8e2d", "#2d8e8e", "#8e2d8e", "#2d2d8e",
]


class EditorCanvas(ttk.Frame):
    """Main editing canvas: renders scene objects, supports drag, zoom, pan."""

    def __init__(self, parent, on_select, on_move):
        super().__init__(parent)
        self.on_select = on_select  # callback(obj or None)
        self.on_move = on_move  # callback(obj)

        # View state
        self._offset_x = 40.0
        self._offset_y = 40.0
        self._scale = 1.0
        self._show_grid = True
        self._snap_grid = True

        # Scene state
        self._objects: List[SceneObject] = []
        self._selected: Optional[SceneObject] = None
        self._color_map: dict = {}

        # Drag state
        self._drag_obj: Optional[SceneObject] = None
        self._drag_start_world: Optional[Tuple[float, float]] = None
        self._drag_start_pos: Optional[Tuple[float, float]] = None

        # Pan state
        self._pan_last: Optional[Tuple[int, int]] = None

        # Texture cache (Pillow, optional)
        self._texture_cache: dict = {}

        # Canvas widget
        self._canvas = tk.Canvas(self, bg=CANVAS_BG, cursor="crosshair")
        hbar = ttk.Scrollbar(self, orient="horizontal", command=self._canvas.xview)
        vbar = ttk.Scrollbar(self, orient="vertical", command=self._canvas.yview)
        self._canvas.configure(xscrollcommand=hbar.set, yscrollcommand=vbar.set)
        self._canvas.grid(row=0, column=0, sticky="nsew")
        hbar.grid(row=1, column=0, sticky="ew")
        vbar.grid(row=0, column=1, sticky="ns")
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)

        self._canvas.bind("<ButtonPress-1>", self._on_left_down)
        self._canvas.bind("<B1-Motion>", self._on_left_drag)
        self._canvas.bind("<ButtonRelease-1>", self._on_left_up)
        self._canvas.bind("<ButtonPress-2>", self._on_middle_down)
        self._canvas.bind("<ButtonPress-3>", self._on_middle_down)
        self._canvas.bind("<B2-Motion>", self._on_pan_drag)
        self._canvas.bind("<B3-Motion>", self._on_pan_drag)
        self._canvas.bind("<ButtonRelease-2>", self._on_pan_up)
        self._canvas.bind("<ButtonRelease-3>", self._on_pan_up)
        self._canvas.bind("<MouseWheel>", self._on_mousewheel)
        self._canvas.bind("<Button-4>", self._on_mousewheel)
        self._canvas.bind("<Button-5>", self._on_mousewheel)
        self._canvas.bind("<Configure>", self._on_resize)

        # Zoom info label
        self._zoom_var = tk.StringVar(value="Zoom: 100%")
        ttk.Label(self, textvariable=self._zoom_var, anchor="e").grid(
            row=2, column=0, columnspan=2, sticky="e", padx=4
        )

        # Coordinates status
        self._coord_var = tk.StringVar(value="")
        ttk.Label(self, textvariable=self._coord_var, anchor="w").grid(
            row=2, column=0, sticky="w", padx=4
        )
        self._canvas.bind("<Motion>", self._on_motion)

    # ── public ────────────────────────────────────────────────────────────────

    def set_objects(self, objects: List[SceneObject]):
        self._objects = objects
        self._texture_cache.clear()
        self.redraw()

    def set_selected(self, obj: Optional[SceneObject]):
        self._selected = obj
        self.redraw()

    @property
    def show_grid(self) -> bool:
        return self._show_grid

    @show_grid.setter
    def show_grid(self, v: bool):
        self._show_grid = v
        self.redraw()

    @property
    def snap_grid(self) -> bool:
        return self._snap_grid

    @snap_grid.setter
    def snap_grid(self, v: bool):
        self._snap_grid = v

    def fit_view(self):
        """Fit all objects (plus viewport) into the visible canvas area."""
        cw = self._canvas.winfo_width() or SCREEN_WIDTH
        ch = self._canvas.winfo_height() or SCREEN_HEIGHT

        # Collect bounding boxes
        all_pts: List[Tuple[float, float]] = [
            (0, 0), (SCREEN_WIDTH, SCREEN_HEIGHT)
        ]
        for obj in self._objects:
            x, y = obj.position
            w = obj.visual_width()
            h = obj.visual_height()
            all_pts.append((x - w / 2, y - h / 2))
            all_pts.append((x + w / 2, y + h / 2))

        if not all_pts:
            return
        min_x = min(p[0] for p in all_pts)
        max_x = max(p[0] for p in all_pts)
        min_y = min(p[1] for p in all_pts)
        max_y = max(p[1] for p in all_pts)

        world_w = max_x - min_x or 1
        world_h = max_y - min_y or 1

        scale = min((cw - 80) / world_w, (ch - 80) / world_h)
        scale = max(0.05, min(scale, 4.0))

        self._scale = scale
        self._offset_x = (cw - world_w * scale) / 2 - min_x * scale
        self._offset_y = (ch - world_h * scale) / 2 - min_y * scale
        self._zoom_var.set(f"Zoom: {int(self._scale * 100)}%")
        self.redraw()

    def redraw(self):
        self._canvas.delete("all")
        self._draw_grid()
        self._draw_viewport()
        for obj in self._objects:
            self._draw_object(obj)
        self._update_scroll_region()

    # ── coordinate helpers ───────────────────────────────────────────────────

    def _world_to_screen(self, wx: float, wy: float) -> Tuple[float, float]:
        return wx * self._scale + self._offset_x, wy * self._scale + self._offset_y

    def _screen_to_world(self, sx: float, sy: float) -> Tuple[float, float]:
        return (sx - self._offset_x) / self._scale, (sy - self._offset_y) / self._scale

    def _canvas_event_pos(self, event) -> Tuple[float, float]:
        return self._canvas.canvasx(event.x), self._canvas.canvasy(event.y)

    # ── drawing ───────────────────────────────────────────────────────────────

    def _draw_grid(self):
        if not self._show_grid:
            return
        cw = self._canvas.winfo_width()
        ch = self._canvas.winfo_height()

        # Choose grid spacing that looks good at current zoom
        raw = SNAP_GRID * self._scale
        if raw < 6:
            return
        if raw < 20:
            step = SNAP_GRID * 4
        else:
            step = SNAP_GRID

        # world coordinates visible
        wx0, wy0 = self._screen_to_world(0, 0)
        wx1, wy1 = self._screen_to_world(cw, ch)

        start_x = math.floor(wx0 / step) * step
        start_y = math.floor(wy0 / step) * step

        x = start_x
        while x <= wx1:
            sx, _ = self._world_to_screen(x, 0)
            self._canvas.create_line(sx, 0, sx, ch, fill=GRID_COLOR, tags="grid")
            x += step

        y = start_y
        while y <= wy1:
            _, sy = self._world_to_screen(0, y)
            self._canvas.create_line(0, sy, cw, sy, fill=GRID_COLOR, tags="grid")
            y += step

        # Draw axes
        sx0, _ = self._world_to_screen(0, 0)
        _, sy0 = self._world_to_screen(0, 0)
        self._canvas.create_line(sx0, 0, sx0, ch, fill="#444466", tags="grid")
        self._canvas.create_line(0, sy0, cw, sy0, fill="#444466", tags="grid")

    def _draw_viewport(self):
        x1, y1 = self._world_to_screen(0, 0)
        x2, y2 = self._world_to_screen(SCREEN_WIDTH, SCREEN_HEIGHT)
        self._canvas.create_rectangle(
            x1, y1, x2, y2,
            outline=VIEWPORT_COLOR, width=2, dash=(6, 4), tags="viewport"
        )
        self._canvas.create_text(
            x1 + 4, y1 + 4, anchor="nw",
            text=f"Viewport {SCREEN_WIDTH}×{SCREEN_HEIGHT}",
            fill=VIEWPORT_COLOR, font=("TkDefaultFont", 8)
        )

    def _get_object_color(self, obj: SceneObject) -> Tuple[str, str]:
        """Return (fill, outline) colors for an object."""
        idx = id(obj) % len(PALETTE)
        base = PALETTE[idx]
        if obj.box_collider and obj.box_collider.is_trigger:
            return TRIGGER_FILL, TRIGGER_OUTLINE
        if obj.circle_collider and obj.circle_collider.is_trigger:
            return TRIGGER_FILL, TRIGGER_OUTLINE
        if obj.camera:
            return "", CAMERA_OUTLINE
        return base + "88", base

    def _draw_object(self, obj: SceneObject):
        cx, cy = obj.position
        sx, sy = self._world_to_screen(cx, cy)
        selected = obj is self._selected

        fill, outline = self._get_object_color(obj)
        lw = 3 if selected else 1
        outline_col = SELECTION_COLOR if selected else outline

        # Try to draw sprite image
        sprite_drawn = False
        if PILLOW_AVAILABLE and obj.sprite and obj.sprite.path:
            img = self._load_texture(obj.sprite.path, obj.sprite.width, obj.sprite.height)
            if img is not None:
                half_w = obj.sprite.width * self._scale / 2
                half_h = obj.sprite.height * self._scale / 2
                self._canvas.create_image(sx, sy, image=img)
                sprite_drawn = True

        if obj.is_circle():
            r = obj.circle_collider.radius * self._scale
            self._canvas.create_oval(
                sx - r, sy - r, sx + r, sy + r,
                fill=fill, outline=outline_col, width=lw
            )
        else:
            w = obj.visual_width() * self._scale
            h = obj.visual_height() * self._scale
            if not sprite_drawn:
                self._canvas.create_rectangle(
                    sx - w / 2, sy - h / 2, sx + w / 2, sy + h / 2,
                    fill=fill, outline=outline_col, width=lw
                )
            elif selected:
                self._canvas.create_rectangle(
                    sx - w / 2, sy - h / 2, sx + w / 2, sy + h / 2,
                    fill="", outline=SELECTION_COLOR, width=lw
                )

        # Object label
        if self._scale >= 0.5:
            label = obj.name
            if obj.tag:
                label = f"[{obj.tag}] {label}"
            self._canvas.create_text(
                sx, sy,
                text=label,
                fill="white",
                font=("TkDefaultFont", max(7, int(9 * self._scale))),
                tags="label"
            )

        # Cross-hair for position center when selected
        if selected:
            half = 6
            self._canvas.create_line(sx - half, sy, sx + half, sy, fill=SELECTION_COLOR, width=1)
            self._canvas.create_line(sx, sy - half, sx, sy + half, fill=SELECTION_COLOR, width=1)

    def _load_texture(self, path: str, width: float, height: float):
        """Load and cache a scaled PIL image for the given path."""
        key = (path, int(width * self._scale), int(height * self._scale))
        if key in self._texture_cache:
            return self._texture_cache[key]
        if not os.path.isfile(path):
            self._texture_cache[key] = None
            return None
        try:
            img = Image.open(path).resize(
                (max(1, int(width * self._scale)), max(1, int(height * self._scale))),
                Image.NEAREST
            )
            photo = ImageTk.PhotoImage(img)
            self._texture_cache[key] = photo
            return photo
        except Exception:
            self._texture_cache[key] = None
            return None

    # ── interaction ───────────────────────────────────────────────────────────

    def _hit_test(self, wx: float, wy: float) -> Optional[SceneObject]:
        """Return the topmost object containing world point (wx, wy)."""
        # Reverse order so topmost drawn object gets priority
        for obj in reversed(self._objects):
            cx, cy = obj.position
            if obj.is_circle():
                r = obj.circle_collider.radius
                if (wx - cx) ** 2 + (wy - cy) ** 2 <= r * r:
                    return obj
            else:
                hw = obj.visual_width() / 2
                hh = obj.visual_height() / 2
                if abs(wx - cx) <= hw and abs(wy - cy) <= hh:
                    return obj
        return None

    def _snap(self, v: float) -> float:
        if self._snap_grid:
            return round(v / SNAP_GRID) * SNAP_GRID
        return v

    def _on_left_down(self, event):
        sx, sy = self._canvas_event_pos(event)
        wx, wy = self._screen_to_world(sx, sy)
        hit = self._hit_test(wx, wy)
        self._selected = hit
        self.on_select(hit)
        if hit is not None:
            self._drag_obj = hit
            self._drag_start_world = (wx, wy)
            self._drag_start_pos = hit.position
        else:
            self._drag_obj = None
        self.redraw()

    def _on_left_drag(self, event):
        if self._drag_obj is None or self._drag_start_world is None:
            return
        sx, sy = self._canvas_event_pos(event)
        wx, wy = self._screen_to_world(sx, sy)
        dx = wx - self._drag_start_world[0]
        dy = wy - self._drag_start_world[1]
        ox, oy = self._drag_start_pos
        new_x = self._snap(ox + dx)
        new_y = self._snap(oy + dy)
        self._drag_obj.position = (new_x, new_y)
        self.on_move(self._drag_obj)
        self.redraw()

    def _on_left_up(self, event):
        self._drag_obj = None
        self._drag_start_world = None
        self._drag_start_pos = None

    def _on_middle_down(self, event):
        self._pan_last = (event.x, event.y)

    def _on_pan_drag(self, event):
        if self._pan_last is None:
            return
        dx = event.x - self._pan_last[0]
        dy = event.y - self._pan_last[1]
        self._offset_x += dx
        self._offset_y += dy
        self._pan_last = (event.x, event.y)
        self.redraw()

    def _on_pan_up(self, event):
        self._pan_last = None

    def _on_mousewheel(self, event):
        sx, sy = self._canvas_event_pos(event)
        if event.num == 4:
            factor = 1.15
        elif event.num == 5:
            factor = 1 / 1.15
        else:
            factor = 1.15 if event.delta > 0 else 1 / 1.15

        new_scale = max(0.05, min(self._scale * factor, 8.0))
        real_factor = new_scale / self._scale
        self._offset_x = sx - (sx - self._offset_x) * real_factor
        self._offset_y = sy - (sy - self._offset_y) * real_factor
        self._scale = new_scale
        self._zoom_var.set(f"Zoom: {int(self._scale * 100)}%")
        self._texture_cache.clear()
        self.redraw()

    def _on_resize(self, _event=None):
        self.redraw()

    def _on_motion(self, event):
        sx, sy = self._canvas_event_pos(event)
        wx, wy = self._screen_to_world(sx, sy)
        self._coord_var.set(f"World: ({wx:.1f}, {wy:.1f})")

    def _update_scroll_region(self):
        all_items = self._canvas.bbox("all")
        if all_items:
            self._canvas.configure(scrollregion=all_items)


# ─────────────────────────────────────────────────────────────────────────────
# Object list panel
# ─────────────────────────────────────────────────────────────────────────────


class ObjectListPanel(ttk.Frame):
    """Left-side panel with the scene object list."""

    def __init__(self, parent, on_select, on_add, on_delete, on_duplicate):
        super().__init__(parent, width=200)
        self.on_select = on_select
        self.on_add = on_add
        self.on_delete = on_delete
        self.on_duplicate = on_duplicate

        ttk.Label(self, text="Scene Objects", font=("TkDefaultFont", 9, "bold")).pack(
            fill="x", padx=4, pady=4
        )

        # Search
        search_frame = ttk.Frame(self)
        search_frame.pack(fill="x", padx=4, pady=2)
        ttk.Label(search_frame, text="🔍").pack(side="left")
        self._search_var = tk.StringVar()
        self._search_var.trace_add("write", lambda *_: self._filter())
        ttk.Entry(search_frame, textvariable=self._search_var).pack(side="left", fill="x", expand=True)

        # Listbox
        list_frame = ttk.Frame(self)
        list_frame.pack(fill="both", expand=True, padx=4, pady=2)
        self._listbox = tk.Listbox(
            list_frame, selectmode="single",
            bg="#1e1e2e", fg="white", selectbackground="#4466aa",
            font=("TkDefaultFont", 9), activestyle="none"
        )
        sb = ttk.Scrollbar(list_frame, orient="vertical", command=self._listbox.yview)
        self._listbox.configure(yscrollcommand=sb.set)
        self._listbox.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self._listbox.bind("<<ListboxSelect>>", self._on_listbox_select)

        # Buttons
        btn_frame = ttk.Frame(self)
        btn_frame.pack(fill="x", padx=4, pady=4)
        ttk.Button(btn_frame, text="＋", width=3, command=self.on_add).pack(side="left", padx=2)
        ttk.Button(btn_frame, text="⎘", width=3, command=self.on_duplicate).pack(side="left", padx=2)
        ttk.Button(btn_frame, text="✕", width=3, command=self.on_delete).pack(side="left", padx=2)

        self._objects: List[SceneObject] = []
        self._filtered: List[SceneObject] = []

    def refresh(self, objects: List[SceneObject], selected: Optional[SceneObject] = None):
        self._objects = objects
        self._filter(selected)

    def _filter(self, selected: Optional[SceneObject] = None):
        query = self._search_var.get().lower()
        self._filtered = [
            o for o in self._objects
            if not query or query in o.name.lower() or query in o.tag.lower()
        ]
        self._listbox.delete(0, "end")
        sel_idx = None
        for i, obj in enumerate(self._filtered):
            tag = f" [{obj.tag}]" if obj.tag else ""
            inactive = " (inactive)" if not obj.active else ""
            self._listbox.insert("end", f"{obj.name}{tag}{inactive}")
            if selected is not None and obj is selected:
                sel_idx = i
        if sel_idx is not None:
            self._listbox.selection_set(sel_idx)
            self._listbox.see(sel_idx)

    def _on_listbox_select(self, _event=None):
        sel = self._listbox.curselection()
        if sel:
            self.on_select(self._filtered[sel[0]])

    def get_selected(self) -> Optional[SceneObject]:
        sel = self._listbox.curselection()
        if sel:
            return self._filtered[sel[0]]
        return None


# ─────────────────────────────────────────────────────────────────────────────
# Main application window
# ─────────────────────────────────────────────────────────────────────────────


class SceneEditorApp(tk.Tk):
    def __init__(self, initial_file: Optional[str] = None):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1280x720")
        self.minsize(800, 500)

        self._objects: List[SceneObject] = []
        self._selected: Optional[SceneObject] = None
        self._file_path: Optional[str] = None
        self._dirty = False

        # Undo stack (list of deep-copied states)
        self._undo_stack: List[List[SceneObject]] = []
        self._redo_stack: List[List[SceneObject]] = []

        self._build_menu()
        self._build_toolbar()
        self._build_main_area()
        self._build_statusbar()

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<Control-z>", lambda _: self._undo())
        self.bind("<Control-y>", lambda _: self._redo())
        self.bind("<Control-Z>", lambda _: self._undo())
        self.bind("<Delete>", lambda _: self._delete_selected())
        self.bind("<Control-d>", lambda _: self._duplicate_selected())
        self.bind("<Control-s>", lambda _: self._save())
        self.bind("<Control-S>", lambda _: self._save_as())

        if initial_file:
            self.after(100, lambda: self._open_file(initial_file))
        else:
            self._new_scene()

    # ── menu ──────────────────────────────────────────────────────────────────

    def _build_menu(self):
        mb = tk.Menu(self)
        self.configure(menu=mb)

        file_menu = tk.Menu(mb, tearoff=False)
        mb.add_cascade(label="File", menu=file_menu)
        file_menu.add_command(label="New Scene", accelerator="Ctrl+N", command=self._new_scene)
        file_menu.add_command(label="Open…", accelerator="Ctrl+O", command=self._open)
        file_menu.add_separator()
        file_menu.add_command(label="Save", accelerator="Ctrl+S", command=self._save)
        file_menu.add_command(label="Save As…", accelerator="Ctrl+Shift+S", command=self._save_as)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self._on_close)
        self.bind("<Control-n>", lambda _: self._new_scene())
        self.bind("<Control-o>", lambda _: self._open())

        edit_menu = tk.Menu(mb, tearoff=False)
        mb.add_cascade(label="Edit", menu=edit_menu)
        edit_menu.add_command(label="Add Object", accelerator="Ctrl+Shift+A", command=self._add_object)
        edit_menu.add_command(label="Duplicate", accelerator="Ctrl+D", command=self._duplicate_selected)
        edit_menu.add_command(label="Delete", accelerator="Del", command=self._delete_selected)
        edit_menu.add_separator()
        edit_menu.add_command(label="Undo", accelerator="Ctrl+Z", command=self._undo)
        edit_menu.add_command(label="Redo", accelerator="Ctrl+Y", command=self._redo)
        self.bind("<Control-A>", lambda _: self._add_object())

        view_menu = tk.Menu(mb, tearoff=False)
        mb.add_cascade(label="View", menu=view_menu)
        self._show_grid_var = tk.BooleanVar(value=True)
        self._snap_grid_var = tk.BooleanVar(value=True)
        view_menu.add_checkbutton(
            label="Show Grid", variable=self._show_grid_var,
            command=self._toggle_grid
        )
        view_menu.add_checkbutton(
            label="Snap to Grid", variable=self._snap_grid_var,
            command=self._toggle_snap
        )
        view_menu.add_separator()
        view_menu.add_command(label="Fit View", accelerator="F", command=self._fit_view)
        self.bind("f", lambda _: self._fit_view())
        self.bind("F", lambda _: self._fit_view())

        help_menu = tk.Menu(mb, tearoff=False)
        mb.add_cascade(label="Help", menu=help_menu)
        help_menu.add_command(label="Controls", command=self._show_help)
        help_menu.add_command(label="About", command=self._show_about)

    def _build_toolbar(self):
        tb = ttk.Frame(self)
        tb.pack(fill="x", padx=4, pady=2)

        buttons = [
            ("📄 New", self._new_scene),
            ("📂 Open", self._open),
            ("💾 Save", self._save),
            ("|", None),
            ("＋ Object", self._add_object),
            ("⎘ Dupe", self._duplicate_selected),
            ("✕ Delete", self._delete_selected),
            ("|", None),
            ("↩ Undo", self._undo),
            ("↪ Redo", self._redo),
            ("|", None),
            ("⊞ Fit View", self._fit_view),
        ]
        for label, cmd in buttons:
            if label == "|":
                ttk.Separator(tb, orient="vertical").pack(side="left", padx=6, pady=2, fill="y")
            else:
                ttk.Button(tb, text=label, command=cmd).pack(side="left", padx=2)

    def _build_main_area(self):
        paned = ttk.PanedWindow(self, orient="horizontal")
        paned.pack(fill="both", expand=True)

        # Left: object list
        self._obj_list = ObjectListPanel(
            paned,
            on_select=self._on_list_select,
            on_add=self._add_object,
            on_delete=self._delete_selected,
            on_duplicate=self._duplicate_selected,
        )
        paned.add(self._obj_list, weight=0)

        # Center: canvas
        self._editor = EditorCanvas(
            paned,
            on_select=self._on_canvas_select,
            on_move=self._on_object_moved,
        )
        paned.add(self._editor, weight=3)

        # Right: properties
        self._props = PropertiesPanel(paned, on_change_callback=self._on_props_changed)
        paned.add(self._props, weight=0)

    def _build_statusbar(self):
        sb = ttk.Frame(self)
        sb.pack(fill="x")
        self._status_var = tk.StringVar(value="Ready")
        ttk.Label(sb, textvariable=self._status_var, anchor="w").pack(
            side="left", padx=8
        )
        ttk.Label(sb, text=f"{'Pillow: ✓' if PILLOW_AVAILABLE else 'Pillow: ✗ (no sprite previews)'}",
                  anchor="e").pack(side="right", padx=8)

    # ── scene operations ──────────────────────────────────────────────────────

    def _new_scene(self):
        if not self._confirm_discard():
            return
        self._objects = [
            SceneObject(
                name="Main Camera",
                camera=CameraConfig(0.0, 0.0, float(SCREEN_WIDTH), float(SCREEN_HEIGHT)),
            )
        ]
        self._file_path = None
        self._selected = None
        self._dirty = False
        self._undo_stack.clear()
        self._redo_stack.clear()
        self._refresh_all()
        self._update_title()
        self._status_var.set("New scene created.")

    def _open(self):
        if not self._confirm_discard():
            return
        path = filedialog.askopenfilename(
            title="Open Scene",
            filetypes=[("Scene files", "*.scene"), ("All files", "*.*")]
        )
        if path:
            self._open_file(path)

    def _open_file(self, path: str):
        try:
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
            self._objects = parse_scene(text)
            self._file_path = path
            self._selected = None
            self._dirty = False
            self._undo_stack.clear()
            self._redo_stack.clear()
            self._refresh_all()
            self._update_title()
            self.after(200, self._fit_view)
            self._status_var.set(f"Opened: {os.path.basename(path)}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to open scene:\n{e}")

    def _save(self):
        if self._file_path:
            self._write_file(self._file_path)
        else:
            self._save_as()

    def _save_as(self):
        path = filedialog.asksaveasfilename(
            title="Save Scene As",
            defaultextension=".scene",
            filetypes=[("Scene files", "*.scene"), ("All files", "*.*")]
        )
        if path:
            self._write_file(path)

    def _write_file(self, path: str):
        try:
            text = write_scene(self._objects)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            self._file_path = path
            self._dirty = False
            self._update_title()
            self._status_var.set(f"Saved: {os.path.basename(path)}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save scene:\n{e}")

    # ── object operations ─────────────────────────────────────────────────────

    def _add_object(self):
        self._push_undo()
        names = {o.name for o in self._objects}
        base = "New Object"
        name = base
        i = 1
        while name in names:
            name = f"{base} {i}"
            i += 1
        # Place at center of current viewport
        new_obj = SceneObject(name=name, position=(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2))
        self._objects.append(new_obj)
        self._mark_dirty()
        self._selected = new_obj
        self._refresh_all()

    def _delete_selected(self):
        if self._selected is None:
            return
        if messagebox.askyesno("Delete", f"Delete '{self._selected.name}'?"):
            self._push_undo()
            self._objects.remove(self._selected)
            self._selected = None
            self._mark_dirty()
            self._refresh_all()

    def _duplicate_selected(self):
        if self._selected is None:
            return
        self._push_undo()
        clone = deepcopy(self._selected)
        x, y = clone.position
        clone.position = (x + SNAP_GRID * 2, y + SNAP_GRID * 2)
        # Ensure unique name
        names = {o.name for o in self._objects}
        base = clone.name
        i = 1
        candidate = f"{base} (copy)"
        while candidate in names:
            candidate = f"{base} (copy {i})"
            i += 1
        clone.name = candidate
        idx = self._objects.index(self._selected) + 1
        self._objects.insert(idx, clone)
        self._selected = clone
        self._mark_dirty()
        self._refresh_all()

    # ── undo/redo ─────────────────────────────────────────────────────────────

    def _push_undo(self):
        self._undo_stack.append(deepcopy(self._objects))
        self._redo_stack.clear()
        # Limit stack depth
        if len(self._undo_stack) > 50:
            self._undo_stack.pop(0)

    def _undo(self):
        if not self._undo_stack:
            self._status_var.set("Nothing to undo.")
            return
        self._redo_stack.append(deepcopy(self._objects))
        self._objects = self._undo_stack.pop()
        self._selected = None
        self._mark_dirty()
        self._refresh_all()
        self._status_var.set("Undo.")

    def _redo(self):
        if not self._redo_stack:
            self._status_var.set("Nothing to redo.")
            return
        self._undo_stack.append(deepcopy(self._objects))
        self._objects = self._redo_stack.pop()
        self._selected = None
        self._mark_dirty()
        self._refresh_all()
        self._status_var.set("Redo.")

    # ── callbacks ─────────────────────────────────────────────────────────────

    def _on_list_select(self, obj: Optional[SceneObject]):
        self._selected = obj
        self._editor.set_selected(obj)
        self._props.load_object(obj)

    def _on_canvas_select(self, obj: Optional[SceneObject]):
        self._selected = obj
        self._obj_list.refresh(self._objects, obj)
        self._props.load_object(obj)

    def _on_object_moved(self, obj: SceneObject):
        self._mark_dirty()
        self._props.sync_position(obj)
        self._update_status_coords(obj)

    def _on_props_changed(self):
        self._mark_dirty()
        if self._selected is not None:
            self._obj_list.refresh(self._objects, self._selected)
        self._editor.redraw()

    def _toggle_grid(self):
        self._editor.show_grid = self._show_grid_var.get()

    def _toggle_snap(self):
        self._editor.snap_grid = self._snap_grid_var.get()

    def _fit_view(self):
        self._editor.fit_view()

    # ── helpers ───────────────────────────────────────────────────────────────

    def _refresh_all(self):
        self._editor.set_objects(self._objects)
        self._editor.set_selected(self._selected)
        self._obj_list.refresh(self._objects, self._selected)
        self._props.load_object(self._selected)
        n = len(self._objects)
        self._status_var.set(f"{n} object{'s' if n != 1 else ''} in scene.")

    def _mark_dirty(self):
        self._dirty = True
        self._update_title()

    def _update_title(self):
        filename = os.path.basename(self._file_path) if self._file_path else "Untitled"
        dirty = " •" if self._dirty else ""
        self.title(f"{APP_TITLE} — {filename}{dirty}")

    def _update_status_coords(self, obj: SceneObject):
        x, y = obj.position
        self._status_var.set(f"{obj.name}  pos=({x:.1f}, {y:.1f})")

    def _confirm_discard(self) -> bool:
        if self._dirty:
            result = messagebox.askyesnocancel(
                "Unsaved Changes",
                "You have unsaved changes. Save before continuing?"
            )
            if result is None:
                return False
            if result:
                self._save()
        return True

    def _on_close(self):
        if self._confirm_discard():
            self.destroy()

    def _show_help(self):
        help_text = (
            "Platformator Scene Editor — Controls\n"
            "─────────────────────────────────────\n"
            "Left-click          Select object\n"
            "Left-drag           Move selected object\n"
            "Right/Middle-drag   Pan the canvas\n"
            "Mouse wheel         Zoom in/out\n"
            "F                   Fit view to scene\n"
            "Delete              Delete selected object\n"
            "Ctrl+D              Duplicate selected object\n"
            "Ctrl+Z / Ctrl+Y     Undo / Redo\n"
            "Ctrl+S              Save\n"
            "Ctrl+Shift+S        Save As\n"
            "Ctrl+N              New Scene\n"
            "Ctrl+O              Open Scene\n"
            "\n"
            "Grid snap size: 16 px (world units)\n"
            "Toggle grid/snap from the View menu.\n"
            "\n"
            "Sprite previews require Pillow:\n"
            "  pip install Pillow"
        )
        messagebox.showinfo("Controls", help_text)

    def _show_about(self):
        messagebox.showinfo(
            "About",
            f"{APP_TITLE}\n\n"
            "Visual level design tool for the Platformator game engine.\n\n"
            "Scene format: .scene (Platformator custom text format)\n"
            f"Python {sys.version.split()[0]}\n"
            f"Pillow: {'available' if PILLOW_AVAILABLE else 'not installed'}"
        )


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────


def main():
    initial = sys.argv[1] if len(sys.argv) > 1 else None
    app = SceneEditorApp(initial_file=initial)
    app.mainloop()


if __name__ == "__main__":
    main()
