#!/usr/bin/env python3
"""Emit content/models/wolf.gltf (skinned quadruped, named clips)."""
from __future__ import annotations

import base64
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "content" / "models"

# Y-up, +Z forward. Root at mid-body; legs hang to about y = -0.50 so a
# pawn spawned at terrain+0.5 (then AI-snapped to +1) matches the hunter.
JOINTS = [
    ("Hips", -1, (0.0, 0.0, 0.0)),
    ("Spine", 0, (0.0, 0.02, 0.16)),
    ("Chest", 1, (0.0, 0.00, 0.20)),
    ("Neck", 2, (0.0, 0.06, 0.14)),
    ("Head", 3, (0.0, 0.03, 0.14)),
    ("Jaw", 4, (0.0, -0.05, 0.08)),
    ("Tail0", 0, (0.0, 0.04, -0.16)),
    ("Tail1", 6, (0.0, 0.01, -0.14)),
    ("Tail2", 7, (0.0, -0.02, -0.12)),
    ("L_F_Upper", 2, (0.10, -0.04, 0.04)),
    ("L_F_Lower", 9, (0.0, -0.22, 0.01)),
    ("L_F_Foot", 10, (0.0, -0.22, 0.04)),
    ("R_F_Upper", 2, (-0.10, -0.04, 0.04)),
    ("R_F_Lower", 12, (0.0, -0.22, 0.01)),
    ("R_F_Foot", 13, (0.0, -0.22, 0.04)),
    ("L_R_Upper", 0, (0.10, -0.04, -0.08)),
    ("L_R_Lower", 15, (0.0, -0.22, 0.00)),
    ("L_R_Foot", 16, (0.0, -0.22, 0.04)),
    ("R_R_Upper", 0, (-0.10, -0.04, -0.08)),
    ("R_R_Lower", 18, (0.0, -0.22, 0.00)),
    ("R_R_Foot", 19, (0.0, -0.22, 0.04)),
    ("L_Ear", 4, (0.05, 0.10, -0.02)),
    ("R_Ear", 4, (-0.05, 0.10, -0.02)),
]

# Box in model space: (cx, cy, cz, hx, hy, hz, joint)
PARTS = [
    (0.0, 0.02, -0.04, 0.12, 0.11, 0.16, 0),  # hips / belly
    (0.0, 0.03, 0.02, 0.11, 0.10, 0.12, 1),  # spine
    (0.0, 0.03, 0.02, 0.14, 0.12, 0.15, 2),  # chest / shoulders
    (0.0, 0.02, 0.02, 0.06, 0.07, 0.10, 3),  # neck / ruff
    (0.0, 0.02, 0.03, 0.08, 0.07, 0.10, 4),  # skull
    (0.0, -0.01, 0.12, 0.05, 0.04, 0.10, 4),  # snout
    (0.0, -0.02, 0.06, 0.045, 0.03, 0.08, 5),  # jaw
    (0.0, 0.00, -0.02, 0.04, 0.04, 0.10, 6),
    (0.0, 0.00, -0.02, 0.03, 0.03, 0.09, 7),
    (0.0, 0.00, -0.02, 0.025, 0.025, 0.08, 8),
    (0.0, -0.10, 0.00, 0.045, 0.12, 0.045, 9),
    (0.0, -0.11, 0.00, 0.035, 0.12, 0.035, 10),
    (0.0, -0.04, 0.05, 0.04, 0.035, 0.08, 11),
    (0.0, -0.10, 0.00, 0.045, 0.12, 0.045, 12),
    (0.0, -0.11, 0.00, 0.035, 0.12, 0.035, 13),
    (0.0, -0.04, 0.05, 0.04, 0.035, 0.08, 14),
    (0.0, -0.10, 0.00, 0.05, 0.12, 0.05, 15),
    (0.0, -0.11, 0.00, 0.04, 0.12, 0.04, 16),
    (0.0, -0.04, 0.05, 0.045, 0.035, 0.08, 17),
    (0.0, -0.10, 0.00, 0.05, 0.12, 0.05, 18),
    (0.0, -0.11, 0.00, 0.04, 0.12, 0.04, 19),
    (0.0, -0.04, 0.05, 0.045, 0.035, 0.08, 20),
    (0.0, 0.05, 0.00, 0.025, 0.07, 0.018, 21),
    (0.0, 0.05, 0.00, 0.025, 0.07, 0.018, 22),
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
    return name, times, joint_rots


def gait_keys(amp: float):
    """Trot: LF+RR together, RF+LR opposite. Negative X swings the foot toward +Z."""

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

    knee_fwd = q_axis(1, 0, 0, amp * 0.55)
    knee_mid = q_ident()
    knee_back = q_axis(1, 0, 0, amp * 0.12)
    return {
        9: swing(amp),
        12: swing_opp(amp),
        15: swing_opp(amp),
        18: swing(amp),
        10: [knee_fwd, knee_mid, knee_back, knee_mid, knee_fwd],
        13: [knee_back, knee_mid, knee_fwd, knee_mid, knee_back],
        16: [knee_back, knee_mid, knee_fwd, knee_mid, knee_back],
        19: [knee_fwd, knee_mid, knee_back, knee_mid, knee_fwd],
    }


def make_clips():
    clips = []

    t_idle = [0.0, 1.0, 2.0]
    idle = {
        1: [q_ident(), q_axis(1, 0, 0, 0.05), q_ident()],
        2: [q_ident(), q_axis(1, 0, 0, 0.03), q_ident()],
        4: [q_ident(), q_axis(0, 1, 0, 0.08), q_ident()],
        6: [q_axis(0, 1, 0, 0.18), q_axis(0, 1, 0, -0.18), q_axis(0, 1, 0, 0.18)],
        7: [q_axis(0, 1, 0, 0.22), q_axis(0, 1, 0, -0.22), q_axis(0, 1, 0, 0.22)],
        8: [q_axis(0, 1, 0, 0.28), q_axis(0, 1, 0, -0.28), q_axis(0, 1, 0, 0.28)],
        21: [q_ident(), q_axis(0, 0, 1, 0.12), q_ident()],
        22: [q_ident(), q_axis(0, 0, 1, -0.12), q_ident()],
    }
    clips.append(clip_channels("Idle", t_idle, idle))

    wt = [0.0, 0.2, 0.4, 0.6, 0.8]
    walk = gait_keys(0.50)
    walk[1] = [q_ident(), q_axis(0, 1, 0, 0.06), q_ident(), q_axis(0, 1, 0, -0.06), q_ident()]
    walk[6] = [q_axis(0, 1, 0, 0.25), q_ident(), q_axis(0, 1, 0, -0.25), q_ident(), q_axis(0, 1, 0, 0.25)]
    walk[7] = [q_axis(0, 1, 0, 0.30), q_ident(), q_axis(0, 1, 0, -0.30), q_ident(), q_axis(0, 1, 0, 0.30)]
    clips.append(clip_channels("Walk", wt, walk))

    rt = [0.0, 0.11, 0.22, 0.33, 0.44]
    run = gait_keys(0.85)
    bounce = [
        q_axis(1, 0, 0, -0.12),
        q_axis(1, 0, 0, 0.10),
        q_axis(1, 0, 0, -0.12),
        q_axis(1, 0, 0, 0.10),
        q_axis(1, 0, 0, -0.12),
    ]
    run[0] = bounce
    run[1] = [q_axis(1, 0, 0, 0.10)] * 5
    run[2] = [q_axis(1, 0, 0, 0.08)] * 5
    run[3] = [q_axis(1, 0, 0, -0.10)] * 5
    run[6] = [q_axis(1, 0, 0, 0.20), q_axis(0, 1, 0, 0.35), q_axis(1, 0, 0, 0.20), q_axis(0, 1, 0, -0.35), q_axis(1, 0, 0, 0.20)]
    clips.append(clip_channels("Run", rt, run))

    bt = [0.0, 0.10, 0.22, 0.38, 0.55]
    jaw_open = q_axis(1, 0, 0, 0.70)
    lunge = q_axis(1, 0, 0, -0.45)
    bite = {
        3: [q_ident(), q_axis(1, 0, 0, 0.18), lunge, lunge, q_ident()],
        4: [q_ident(), q_axis(1, 0, 0, 0.12), q_axis(1, 0, 0, -0.15), q_axis(1, 0, 0, -0.08), q_ident()],
        5: [q_ident(), jaw_open, q_axis(1, 0, 0, 0.12), q_axis(1, 0, 0, 0.20), q_ident()],
        1: [q_ident(), q_ident(), q_axis(1, 0, 0, -0.12), q_ident(), q_ident()],
        2: [q_ident(), q_ident(), q_axis(1, 0, 0, -0.10), q_ident(), q_ident()],
        9: [q_ident(), q_axis(1, 0, 0, -0.20), q_axis(1, 0, 0, -0.35), q_axis(1, 0, 0, -0.15), q_ident()],
        12: [q_ident(), q_axis(1, 0, 0, -0.20), q_axis(1, 0, 0, -0.35), q_axis(1, 0, 0, -0.15), q_ident()],
    }
    clips.append(clip_channels("Bite", bt, bite))

    jt = [0.0, 0.14, 0.32, 0.52, 0.72, 0.95]
    crouch_u = q_axis(1, 0, 0, 0.55)
    crouch_k = q_axis(1, 0, 0, 0.85)
    launch_u = q_axis(1, 0, 0, -0.55)
    launch_k = q_axis(1, 0, 0, -0.15)
    tuck_u = q_axis(1, 0, 0, 0.70)
    tuck_k = q_axis(1, 0, 0, 1.05)
    stretch_u = q_axis(1, 0, 0, -0.25)
    land_u = q_axis(1, 0, 0, 0.45)
    land_k = q_axis(1, 0, 0, 0.70)
    jump = {
        0: [q_axis(1, 0, 0, 0.18), q_axis(1, 0, 0, -0.22), q_axis(1, 0, 0, -0.08), q_axis(1, 0, 0, 0.12), q_axis(1, 0, 0, 0.20), q_ident()],
        1: [q_axis(1, 0, 0, 0.12), q_axis(1, 0, 0, -0.15), q_ident(), q_axis(1, 0, 0, 0.10), q_axis(1, 0, 0, 0.14), q_ident()],
        3: [q_axis(1, 0, 0, 0.10), q_axis(1, 0, 0, -0.25), q_axis(1, 0, 0, -0.10), q_axis(1, 0, 0, 0.15), q_ident(), q_ident()],
        9: [crouch_u, launch_u, tuck_u, stretch_u, land_u, q_ident()],
        12: [crouch_u, launch_u, tuck_u, stretch_u, land_u, q_ident()],
        15: [crouch_u, q_axis(1, 0, 0, -0.70), tuck_u, stretch_u, land_u, q_ident()],
        18: [crouch_u, q_axis(1, 0, 0, -0.70), tuck_u, stretch_u, land_u, q_ident()],
        10: [crouch_k, launch_k, tuck_k, q_ident(), land_k, q_ident()],
        13: [crouch_k, launch_k, tuck_k, q_ident(), land_k, q_ident()],
        16: [crouch_k, launch_k, tuck_k, q_ident(), land_k, q_ident()],
        19: [crouch_k, launch_k, tuck_k, q_ident(), land_k, q_ident()],
        6: [q_axis(1, 0, 0, 0.15), q_axis(1, 0, 0, -0.35), q_axis(1, 0, 0, -0.20), q_ident(), q_axis(1, 0, 0, 0.20), q_ident()],
    }
    clips.append(clip_channels("Jump", jt, jump))

    dtm = [0.0, 0.35, 0.70, 1.15]
    die = {
        0: [q_ident(), q_axis(0, 0, 1, 0.55), q_axis(0, 0, 1, 1.15), q_axis(0, 0, 1, 1.40)],
        1: [q_ident(), q_axis(1, 0, 0, 0.20), q_axis(1, 0, 0, 0.35), q_axis(1, 0, 0, 0.40)],
        3: [q_ident(), q_axis(1, 0, 0, 0.25), q_axis(1, 0, 0, 0.45), q_axis(1, 0, 0, 0.50)],
        9: [q_ident(), q_axis(1, 0, 0, 0.40), q_axis(1, 0, 0, 0.70), q_axis(1, 0, 0, 0.80)],
        12: [q_ident(), q_axis(1, 0, 0, 0.35), q_axis(1, 0, 0, 0.65), q_axis(1, 0, 0, 0.75)],
        15: [q_ident(), q_axis(1, 0, 0, 0.50), q_axis(1, 0, 0, 0.90), q_axis(1, 0, 0, 1.05)],
        18: [q_ident(), q_axis(1, 0, 0, 0.45), q_axis(1, 0, 0, 0.85), q_axis(1, 0, 0, 1.00)],
        6: [q_ident(), q_axis(0, 0, 1, -0.30), q_axis(0, 0, 1, -0.55), q_axis(0, 0, 1, -0.60)],
    }
    clips.append(clip_channels("Die", dtm, die))
    return clips


def build_gltf() -> dict:
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

    ibm_accessor = len(accessors)
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
    for name, times, joint_rots in make_clips():
        samplers = []
        channels = []
        raw_t = bytearray()
        pack_f32(raw_t, times)
        i_t = push_view(raw_t)
        a_t = len(accessors)
        accessors.append(acc(i_t, 5126, len(times), "SCALAR", {"min": [times[0]], "max": [times[-1]]}))
        for joint, rots in joint_rots.items():
            if len(rots) != len(times):
                raise RuntimeError(f"{name} joint {joint} has {len(rots)} keys, expected {len(times)}")
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
        "asset": {"version": "2.0", "generator": "DarkEngine6 wolf"},
        "scene": 0,
        "scenes": [{"nodes": [0, mesh_node]}],
        "nodes": nodes,
        "skins": [{"joints": list(range(len(JOINTS))), "inverseBindMatrices": ibm_accessor, "skeleton": 0}],
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
                "name": "WolfFur",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.45, 0.40, 0.36, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.72,
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
    wolf = build_gltf()
    (OUT_DIR / "wolf.gltf").write_text(json.dumps(wolf, indent=2), encoding="utf-8")
    print("wrote", OUT_DIR / "wolf.gltf")


if __name__ == "__main__":
    main()
