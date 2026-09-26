#!/usr/bin/env python3
"""Emit content/models/human.gltf and skeleton.gltf (skinned, named clips)."""
from __future__ import annotations

import base64
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "content" / "models"

JOINTS = [
    ("Hips", -1, (0.0, 0.0, 0.0)),
    ("Spine", 0, (0.0, 0.18, 0.0)),
    ("Chest", 1, (0.0, 0.18, 0.0)),
    ("Neck", 2, (0.0, 0.16, 0.0)),
    ("Head", 3, (0.0, 0.14, 0.0)),
    ("L_UpperArm", 2, (0.16, 0.14, 0.0)),
    ("L_LowerArm", 5, (0.26, 0.0, 0.0)),
    ("L_Hand", 6, (0.22, 0.0, 0.0)),
    ("R_UpperArm", 2, (-0.16, 0.14, 0.0)),
    ("R_LowerArm", 8, (-0.26, 0.0, 0.0)),
    ("R_Hand", 9, (-0.22, 0.0, 0.0)),
    ("L_UpperLeg", 0, (0.09, -0.02, 0.0)),
    ("L_LowerLeg", 11, (0.0, -0.24, 0.0)),
    ("L_Foot", 12, (0.0, -0.22, 0.04)),
    ("R_UpperLeg", 0, (-0.09, -0.02, 0.0)),
    ("R_LowerLeg", 14, (0.0, -0.24, 0.0)),
    ("R_Foot", 15, (0.0, -0.22, 0.04)),
]

# Box in model space: (cx, cy, cz, hx, hy, hz, joint)
PARTS = [
    (0.0, 0.06, 0.0, 0.12, 0.10, 0.08, 0),  # hips
    (0.0, 0.18, 0.0, 0.11, 0.10, 0.08, 1),  # spine
    (0.0, 0.16, 0.02, 0.16, 0.12, 0.09, 2),  # chest
    (0.0, 0.08, 0.0, 0.05, 0.08, 0.05, 3),  # neck
    (0.0, 0.12, 0.02, 0.10, 0.11, 0.10, 4),  # head
    (0.14, 0.0, 0.0, 0.14, 0.05, 0.05, 5),
    (0.14, 0.0, 0.0, 0.13, 0.04, 0.04, 6),
    (0.08, 0.0, 0.02, 0.08, 0.03, 0.05, 7),
    (-0.14, 0.0, 0.0, 0.14, 0.05, 0.05, 8),
    (-0.14, 0.0, 0.0, 0.13, 0.04, 0.04, 9),
    (-0.08, 0.0, 0.02, 0.08, 0.03, 0.05, 10),
    (0.0, -0.13, 0.0, 0.06, 0.13, 0.06, 11),
    (0.0, -0.13, 0.0, 0.05, 0.13, 0.05, 12),
    (0.0, -0.04, 0.07, 0.05, 0.04, 0.10, 13),
    (0.0, -0.13, 0.0, 0.06, 0.13, 0.06, 14),
    (0.0, -0.13, 0.0, 0.05, 0.13, 0.05, 15),
    (0.0, -0.04, 0.07, 0.05, 0.04, 0.10, 16),
]


def q_axis(ax: float, ay: float, az: float, rad: float) -> tuple[float, float, float, float]:
    n = math.sqrt(ax * ax + ay * ay + az * az) or 1.0
    s = math.sin(rad * 0.5)
    return (ax / n * s, ay / n * s, az / n * s, math.cos(rad * 0.5))


def q_ident() -> tuple[float, float, float, float]:
    return (0.0, 0.0, 0.0, 1.0)


def joint_world() -> list[tuple[float, float, float]]:
    worlds = [(0.0, 0.0, 0.0)] * len(JOINTS)
    for i, (_n, parent, t) in enumerate(JOINTS):
        if parent < 0:
            worlds[i] = t
        else:
            px, py, pz = worlds[parent]
            worlds[i] = (px + t[0], py + t[1], pz + t[2])
    return worlds


def add_box(cx, cy, cz, hx, hy, hz, joint, pos, nrm, jnt, wgt, idx):
    faces = [
        ((1, 0, 0), [(hx, -hy, -hz), (hx, -hy, hz), (hx, hy, hz), (hx, hy, -hz)]),
        ((-1, 0, 0), [(-hx, -hy, hz), (-hx, -hy, -hz), (-hx, hy, -hz), (-hx, hy, hz)]),
        ((0, 1, 0), [(-hx, hy, -hz), (hx, hy, -hz), (hx, hy, hz), (-hx, hy, hz)]),
        ((0, -1, 0), [(-hx, -hy, hz), (hx, -hy, hz), (hx, -hy, -hz), (-hx, -hy, -hz)]),
        ((0, 0, 1), [(-hx, -hy, hz), (-hx, hy, hz), (hx, hy, hz), (hx, -hy, hz)]),
        ((0, 0, -1), [(hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz), (-hx, -hy, -hz)]),
    ]
    for n, corners in faces:
        base = len(pos) // 3
        for ox, oy, oz in corners:
            pos.extend((cx + ox, cy + oy, cz + oz))
            nrm.extend(n)
            jnt.extend((joint, 0, 0, 0))
            wgt.extend((1.0, 0.0, 0.0, 0.0))
        idx.extend((base, base + 1, base + 2, base, base + 2, base + 3))


def pad4(buf: bytearray) -> None:
    while len(buf) % 4:
        buf.append(0)


def pack_f32(buf: bytearray, values) -> None:
    buf.extend(struct.pack("<" + "f" * len(values), *values))


def pack_u16(buf: bytearray, values) -> None:
    buf.extend(struct.pack("<" + "H" * len(values), *values))


def pack_u8(buf: bytearray, values) -> None:
    buf.extend(bytes(values))


def pack_mat4_t(buf: bytearray, x, y, z) -> None:
    # Row-major engine / documented memcpy layout (translation in last row).
    pack_f32(buf, (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1))


def view(offset, length, target=None):
    v = {"buffer": 0, "byteOffset": offset, "byteLength": length}
    if target is not None:
        v["target"] = target
    return v


def acc(view_i, ctype, count, typ, extra=None):
    a = {"bufferView": view_i, "componentType": ctype, "count": count, "type": typ}
    if extra:
        a.update(extra)
    return a


def clip_channels(name: str, times: list[float], joint_rots: dict[int, list[tuple[float, float, float, float]]]):
    """joint_rots: joint -> list of xyzw quats, one per time."""
    return name, times, joint_rots


def make_clips(include_die: bool):
    clips = []

    # Idle: 2s breathe
    t = [0.0, 1.0, 2.0]
    idle = {1: [q_ident(), q_axis(1, 0, 0, 0.04), q_ident()]}
    idle[5] = [q_axis(0, 0, 1, 0.12), q_axis(0, 0, 1, 0.18), q_axis(0, 0, 1, 0.12)]
    idle[8] = [q_axis(0, 0, 1, -0.12), q_axis(0, 0, 1, -0.18), q_axis(0, 0, 1, -0.12)]
    clips.append(clip_channels("Idle", t, idle))

    # Walk: 0.8s cycle, opposite limbs
    wt = [0.0, 0.2, 0.4, 0.6, 0.8]
    def swing(a):
        return [
            q_axis(1, 0, 0, -a),
            q_axis(1, 0, 0, 0.0),
            q_axis(1, 0, 0, a),
            q_axis(1, 0, 0, 0.0),
            q_axis(1, 0, 0, -a),
        ]

    def swing_opp(a):
        return [
            q_axis(1, 0, 0, a),
            q_axis(1, 0, 0, 0.0),
            q_axis(1, 0, 0, -a),
            q_axis(1, 0, 0, 0.0),
            q_axis(1, 0, 0, a),
        ]

    walk = {
        11: swing(0.55),
        14: swing_opp(0.55),
        12: [q_axis(1, 0, 0, 0.35), q_ident(), q_axis(1, 0, 0, 0.05), q_ident(), q_axis(1, 0, 0, 0.35)],
        15: [q_axis(1, 0, 0, 0.05), q_ident(), q_axis(1, 0, 0, 0.35), q_ident(), q_axis(1, 0, 0, 0.05)],
        5: swing_opp(0.40),
        8: swing(0.40),
        1: [q_ident(), q_axis(0, 1, 0, 0.08), q_ident(), q_axis(0, 1, 0, -0.08), q_ident()],
    }
    def negate_pitch(pose):
        # Reverse the forward/back swings so the cycle steps along -Z.
        return {joint: [(-q[0], q[1], q[2], q[3]) for q in rots] for joint, rots in pose.items()}

    clips.append(clip_channels("Walk", wt, walk))
    clips.append(clip_channels("WalkBack", wt, negate_pitch(walk)))

    # Run: faster, bigger
    rt = [0.0, 0.125, 0.25, 0.375, 0.5]
    run = {
        11: swing(0.85),
        14: swing_opp(0.85),
        12: [q_axis(1, 0, 0, 0.55), q_ident(), q_axis(1, 0, 0, 0.1), q_ident(), q_axis(1, 0, 0, 0.55)],
        15: [q_axis(1, 0, 0, 0.1), q_ident(), q_axis(1, 0, 0, 0.55), q_ident(), q_axis(1, 0, 0, 0.1)],
        5: swing_opp(0.70),
        8: swing(0.70),
        1: [q_axis(1, 0, 0, 0.18)] * 5,
        2: [q_axis(1, 0, 0, 0.08)] * 5,
    }
    clips.append(clip_channels("Run", rt, run))
    clips.append(clip_channels("RunBack", rt, negate_pitch(run)))

    # Side-step. Model +X is gameplay right (FromLookRotation maps local +X to the character's right)
    # even though the joints named L_* sit on +X. Positive Z on a downward leg swings the foot toward +X.
    # StrafeRight steps toward +X; StrafeLeft is that pose mirrored across the YZ plane.
    def z_keys(scale, a, b, c, d):
        return [q_axis(0, 0, 1, scale * v) for v in (a, b, c, d, a)]

    def x_keys(scale, a, b, c, d):
        return [q_axis(1, 0, 0, scale * v) for v in (a, b, c, d, a)]

    def strafe_right(step, knee, arm, lean, chest_pitch):
        pose = {
            0: z_keys(lean, 0.35, -0.25, -0.55, -0.15),
            1: z_keys(lean, -1.0, -1.15, -1.0, -0.85),
            11: z_keys(step, 0.08, 1.0, 0.28, -0.42),
            12: x_keys(knee, 0.18, 1.0, 0.32, 0.16),
            13: x_keys(knee, 0.12, 0.48, 0.16, 0.34),
            14: z_keys(step, -0.85, -0.18, 0.58, 0.12),
            15: x_keys(knee, 0.95, 0.22, 0.88, 0.28),
            16: x_keys(knee, 0.55, 0.14, 0.50, 0.18),
            5: z_keys(arm, -0.25, 0.85, 0.12, -0.55),
            8: z_keys(arm, 0.75, -0.08, -0.95, -0.12),
            6: z_keys(1.0, 0.28, 0.42, 0.24, 0.18),
            9: z_keys(1.0, -0.32, -0.18, -0.48, -0.22),
        }
        if chest_pitch != 0.0:
            pose[2] = [q_axis(1, 0, 0, chest_pitch)] * 5
        return pose

    pairs = ((5, 8), (6, 9), (7, 10), (11, 14), (12, 15), (13, 16))

    def mirror_strafe(pose):
        swap = {}
        for a, b in pairs:
            swap[a] = b
            swap[b] = a
        out = {}
        for joint, rots in pose.items():
            dest = swap.get(joint, joint)
            out[dest] = [(q[0], -q[1], -q[2], q[3]) for q in rots]
        return out

    walk_right = strafe_right(0.62, 0.55, 0.42, 0.20, 0.0)
    run_right = strafe_right(0.90, 0.78, 0.58, 0.30, 0.10)
    clips.append(clip_channels("StrafeLeft", wt, mirror_strafe(walk_right)))
    clips.append(clip_channels("StrafeRight", wt, walk_right))
    clips.append(clip_channels("StrafeRunLeft", rt, mirror_strafe(run_right)))
    clips.append(clip_channels("StrafeRunRight", rt, run_right))

    # Shoot: raise both arms, hold, slight recoil. 0.55s
    st = [0.0, 0.12, 0.28, 0.55]
    aim_l = q_axis(0, 0, 1, 1.15)
    aim_r = q_axis(0, 0, 1, -1.15)
    aim_lf = q_axis(1, 0, 0, -0.35)
    aim_rf = q_axis(1, 0, 0, -0.35)
    shoot = {
        5: [q_ident(), aim_l, aim_l, aim_l],
        8: [q_ident(), aim_r, aim_r, aim_r],
        6: [q_ident(), aim_lf, q_axis(1, 0, 0, -0.20), aim_lf],
        9: [q_ident(), aim_rf, q_axis(1, 0, 0, -0.20), aim_rf],
        2: [q_ident(), q_ident(), q_axis(1, 0, 0, -0.06), q_ident()],
    }
    clips.append(clip_channels("Shoot", st, shoot))

    # SwingSword: right arm arc. 0.5s
    swt = [0.0, 0.12, 0.28, 0.5]
    swingc = {
        8: [
            q_ident(),
            q_axis(0, 0, 1, -2.2),
            q_axis(1, 0, 0, -1.6),
            q_ident(),
        ],
        9: [
            q_ident(),
            q_axis(1, 0, 0, -0.4),
            q_axis(1, 0, 0, 0.8),
            q_ident(),
        ],
        2: [q_ident(), q_axis(0, 1, 0, -0.25), q_axis(0, 1, 0, 0.35), q_ident()],
    }
    clips.append(clip_channels("SwingSword", swt, swingc))

    if include_die:
        dtm = [0.0, 0.35, 0.7, 1.2]
        die = {
            0: [q_ident(), q_axis(1, 0, 0, 0.4), q_axis(1, 0, 0, 1.1), q_axis(1, 0, 0, 1.35)],
            1: [q_ident(), q_axis(1, 0, 0, 0.3), q_axis(1, 0, 0, 0.5), q_axis(1, 0, 0, 0.55)],
            11: [q_ident(), q_axis(1, 0, 0, 0.6), q_axis(1, 0, 0, 1.1), q_axis(1, 0, 0, 1.2)],
            14: [q_ident(), q_axis(1, 0, 0, 0.5), q_axis(1, 0, 0, 1.0), q_axis(1, 0, 0, 1.15)],
            5: [q_ident(), q_axis(0, 0, 1, 0.4), q_axis(0, 0, 1, 0.8), q_axis(0, 0, 1, 0.9)],
            8: [q_ident(), q_axis(0, 0, 1, -0.4), q_axis(0, 0, 1, -0.8), q_axis(0, 0, 1, -0.9)],
        }
        clips.append(clip_channels("Die", dtm, die))

    return clips


def build_gltf(color, include_die: bool, generator: str) -> dict:
    worlds = joint_world()
    pos: list[float] = []
    nrm: list[float] = []
    jnt: list[int] = []
    wgt: list[float] = []
    idx: list[int] = []
    for cx, cy, cz, hx, hy, hz, joint in PARTS:
        wx, wy, wz = worlds[joint]
        add_box(wx + cx, wy + cy, wz + cz, hx, hy, hz, joint, pos, nrm, jnt, wgt, idx)

    buf = bytearray()
    views = []
    accessors = []

    def push_view(data: bytearray, target=None):
        pad4(buf)
        off = len(buf)
        buf.extend(data)
        views.append(view(off, len(data), target))
        return len(views) - 1

    raw = bytearray()
    pack_f32(raw, pos)
    i_pos = push_view(raw, 34962)
    xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
    accessors.append(acc(i_pos, 5126, len(pos) // 3, "VEC3", {"min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]}))

    raw = bytearray()
    pack_f32(raw, nrm)
    i_nrm = push_view(raw, 34962)
    accessors.append(acc(i_nrm, 5126, len(nrm) // 3, "VEC3"))

    raw = bytearray()
    pack_u8(raw, jnt)
    pad4(raw)
    i_jnt = push_view(raw, 34962)
    accessors.append(acc(i_jnt, 5121, len(jnt) // 4, "VEC4"))

    raw = bytearray()
    pack_f32(raw, wgt)
    i_wgt = push_view(raw, 34962)
    accessors.append(acc(i_wgt, 5126, len(wgt) // 4, "VEC4"))

    raw = bytearray()
    pack_u16(raw, idx)
    i_idx = push_view(raw, 34963)
    accessors.append(acc(i_idx, 5123, len(idx), "SCALAR"))

    raw = bytearray()
    for wx, wy, wz in worlds:
        pack_mat4_t(raw, -wx, -wy, -wz)
    i_ibm = push_view(raw)
    accessors.append(acc(i_ibm, 5126, len(JOINTS), "MAT4"))

    children = [[] for _ in JOINTS]
    for i, (_n, parent, _t) in enumerate(JOINTS):
        if parent >= 0:
            children[parent].append(i)

    nodes = []
    for i, (name, _p, t) in enumerate(JOINTS):
        node = {"name": name, "translation": [t[0], t[1], t[2]]}
        if children[i]:
            node["children"] = children[i]
        nodes.append(node)
    mesh_node = len(nodes)
    nodes.append({"name": "Mesh", "mesh": 0, "skin": 0})

    animations = []
    for name, times, joint_rots in make_clips(include_die):
        samplers = []
        channels = []
        raw_t = bytearray()
        pack_f32(raw_t, times)
        i_t = push_view(raw_t)
        a_t = len(accessors)
        accessors.append(acc(i_t, 5126, len(times), "SCALAR", {"min": [times[0]], "max": [times[-1]]}))
        for joint, rots in joint_rots.items():
            flat = []
            for q in rots:
                flat.extend(q)
            raw_q = bytearray()
            pack_f32(raw_q, flat)
            i_q = push_view(raw_q)
            a_q = len(accessors)
            accessors.append(acc(i_q, 5126, len(rots), "VEC4"))
            si = len(samplers)
            samplers.append({"input": a_t, "output": a_q, "interpolation": "LINEAR"})
            channels.append({"sampler": si, "target": {"node": joint, "path": "rotation"}})
        animations.append({"name": name, "samplers": samplers, "channels": channels})

    uri = "data:application/octet-stream;base64," + base64.b64encode(buf).decode("ascii")
    return {
        "asset": {"version": "2.0", "generator": generator},
        "scene": 0,
        "scenes": [{"nodes": [0, mesh_node]}],
        "nodes": nodes,
        "skins": [{"joints": list(range(len(JOINTS))), "inverseBindMatrices": 5, "skeleton": 0}],
        "meshes": [
            {
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
                        "indices": 4,
                        "material": 0,
                    }
                ]
            }
        ],
        "materials": [
            {
                "pbrMetallicRoughness": {
                    "baseColorFactor": [color[0], color[1], color[2], 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.55,
                }
            }
        ],
        "animations": animations,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buf), "uri": uri}],
    }


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    human = build_gltf((0.72, 0.52, 0.38), False, "DarkEngine6 human")
    skel = build_gltf((0.92, 0.90, 0.82), True, "DarkEngine6 skeleton")
    (OUT_DIR / "human.gltf").write_text(json.dumps(human, indent=2), encoding="utf-8")
    (OUT_DIR / "skeleton.gltf").write_text(json.dumps(skel, indent=2), encoding="utf-8")
    print("wrote", OUT_DIR / "human.gltf")
    print("wrote", OUT_DIR / "skeleton.gltf")


if __name__ == "__main__":
    main()
