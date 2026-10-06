#!/usr/bin/env python3
"""
make_demo.py — synthesize a ZDoom IFF/ZDEM demo: a deterministic camera path.

Why synthesize instead of record:
  A ZDoom .lmp demo is just a versioned IFF container whose body is a stream
  of player inputs (delta-packed usercmds) and cheat markers. No world state
  is stored: the map name, an RNG seed (which reseeds all engine RNGs, so
  replay is deterministic), player info, and the input stream. The camera
  path is re-derived by the deterministic simulation at load time, so a demo
  generated here replays identically on any engine build whose DEMOGAMEVERSION
  satisfies the version handshake — no per-compile rebuild. Rebuild only if
  the engine rejects the demo ("Demo requires a newer version...") or the
  replay gate (phase 3/4: single L-line + frame count + per-frame geometry
  counters) fails.

Path: spiral fan-out from a computed map center (covers the whole map):
  P1    player start, parsed from the map (THINGS type 1 / UDMF thing type=1)
  C     median of linedef midpoints (robust map center)
  R     max distance from C to a midpoint (map extent)
  phase 1 (travel, T1 tics):  P1 -> C straight, capped at 300 tics (fits in
                             the 350-tic warmup)
  phase 2 (spiral, T-T1 tics): Archimedean:
                             r(tau) = R * tau / T2
                             th(tau) = th0 + 2*pi*K * tau / T2
                             th0 = direction of travel (tangent-continuous)
                             K = clamp(round(V_END*T2/(2*pi*R)), 2, 8)
  The camera velocity profile is V(t) = P(t) - P(t-1); per-tic input commands
  are solved from the engine's exact movement model (below) so the actual
  per-tic displacement matches the planned one (within fixed-point rounding).

Engine movement model (VERIFIED from this build's source — all exact binary
fractions, so the generator simulates it exactly):
  per tic (player.zs MovePlayer -> ForwardThrust/Thrust -> P_SlideMove):
    Angle += yaw_fp * 360/65536                (65536 = full circle, int16)
    vel   += (fwd, side) / 8192  in the view basis
                = (1/256) * ORIG_FRICTION_FACTOR (2048/65536), Speed 1
    vel   *= 29/32                           ORIG_FRICTION (0xE800/65536)
    pos   += vel
  CF_NOCLIP2: P_GetFriction() returns the defaults (sector friction ignored),
  onground is forced true, P_CheckMove ignores walls.
  Side thrust direction (actor.h Thrust, a = Angle - 90): (sin A, -cos A).
  Steady state: |v| = (29/3) * |a|, i.e. cmd ~= 847 per (u/tic) of speed.

  Known hazard: a full-map spiral may cross walk-through exit doors (e.g.
  Heretic episode exits) and trigger a map change mid-window — detectable as
  a second L line inside the measurement window; flag such data.

Demo format (verified from src: g_game.cpp G_ProcessIFFDemo/G_ReadDemoTiccmd,
d_protocol.cpp PackUserCmd — ALL multi-byte ints big-endian, strings
NUL-terminated):

  "FORM" u32be(totalLen) "ZDEM"
    "ZDHD" u32be(len):
      u16be  demover        (DEMOGAMEVERSION at generation time)
      u16be  minver         (0x0203 = 515; must be <= reader's DEMOGAMEVERSION)
      str    mapname        (NUL-terminated)
      i32be  rngseed        (seeds all RNGs via FRandom::StaticClearRandom)
      u8     consoleplayer  (0)
    "UINF" u32be(len):
      u8     player         (0)
      str    userinfo       (NUL-terminated; minimal = "\\" only)
    "BODY" u32be(len):      (len exact — no padding for BODY)
      [u8 10, u8 50]         DEM_GENERICCHEAT + CHT_NOCLIP2 (fly through walls)
      [u8 1, packed cmd]     DEM_USERCMD + each per-tic command (delta-packed
                             against the previous command; first basis = zeros)
      [u7] ...               (more packed commands)
      [u8 7]                 DEM_STOP

Packed usercmd (d_protocol.cpp PackUserCmd): flags byte first, then for each
changed field in order: BUTTONS (1-4 bytes, 7-bit groups, MoreButtons=0x80),
PITCH, YAW, FORWARDMOVE, SIDEMOVE, UPMOVE, ROLL — each i16be.

Usage:
  make_demo.py --map MAP01 --out DOOM2_MAP01.lmp
  make_demo.py --map 20PAM --iwad ../build/wads/myhouse.pk3 --tics 2600
"""

import argparse
import contextlib
import importlib.util
import math
import os
import re
import statistics
import struct
import sys

# scan_maps.py lives next to this tool; load it by path (no sys.path change)
_spec = importlib.util.spec_from_file_location(
    "scan_maps", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "scan_maps.py"))
if _spec is None or _spec.loader is None:  # cannot happen for an existing file
    raise SystemExit(f"cannot load {os.path.abspath('scan_maps.py')}")
_sm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_sm)
open_archive = _sm.open_archive
map_blocks = _sm.map_blocks
lump_bytes = _sm.lump_bytes

# --- protocol constants (src/d_protocol.h, src/version.h) --------------------
DEMOGAMEVERSION = 0x221  # version.h at generation time (2026-10-05)
MINVER = 0x0203          # 515, as written by G_BeginRecording

DEM_USERCMD = 1
DEM_STOP = 7
DEM_GENERICCHEAT = 10

UCMDF_BUTTONS = 0x01
UCMDF_PITCH = 0x02
UCMDF_YAW = 0x04
UCMDF_FORWARDMOVE = 0x08
UCMDF_SIDEMOVE = 0x10
UCMDF_UPMOVE = 0x20
UCMDF_ROLL = 0x40

MORE_BUTTONS = 0x80

CHEATS = {
    # ECheatCommand enum indices (d_protocol.h, zero-based) — NOT the legacy
    # bitmask values (0x20/0x40): DEM_GENERICCHEAT is followed by the enum index.
    "noclip2": 50,  # CHT_NOCLIP2 (fly through walls)
    "noclip": 1,    # CHT_NOCLIP
}

# --- engine movement model (see docstring; exact binary fractions) -----------
THRUST_PER_CMD = 1.0 / 8192.0   # (u/tic) added per unit of cmd: (1/256)*0.03125
FRICTION_KEEP = 0xE800 / 65536.  # = 29/32: velocity retained per tic
ANGLE_FPP = 65536.0             # fixed angle units per full circle
CMD_MAX = 32767

INT16_RE = re.compile(r"^-?(0[xX][0-9a-fA-F]+|\d+)$")


def chunk(cid: bytes, payload: bytes, pad: bool = True) -> bytes:
    if pad and len(payload) % 2:
        payload += b"\x00"
    return cid + struct.pack(">I", len(payload)) + payload


# --- map geometry -------------------------------------------------------------

def _udmf_val(s: str):
    s = s.strip().rstrip(";").strip()
    if not s:
        return None
    if s.lower() in ("true", "false"):
        return s.lower() == "true"
    if s.startswith('"'):
        return s
    parts = s.split()
    nums = []
    for p in parts:
        try:
            v = int(p, 0)
        except ValueError:
            try:
                v = float(p)
            except ValueError:
                return s
        nums.append(v)
    if len(nums) != len(parts):
        return s
    return nums[0] if len(nums) == 1 else nums


def udmf_geom(data: bytes):
    """Targeted UDMF geometry parse: vertex x/y, linedef v1/v2, thing type 1.
    Handles both header styles (bare `type` line + separate `{`, and
    `type {` / `type // id` on one line); values may be int, hex, float,
    or vectors; a thing position may be `position = x, y[, z]` or separate
    x/y properties."""
    txt = data.decode("latin-1", "replace")
    verts, lines, p1 = [], [], None
    cur, props = None, {}

    def commit(ctype, pr):
        nonlocal p1
        if ctype == "vertex" and "x" in pr and "y" in pr:
            with contextlib.suppress(TypeError, ValueError):
                verts.append((float(pr["x"]), float(pr["y"])))
        elif ctype == "linedef" and "v1" in pr and "v2" in pr:
            with contextlib.suppress(TypeError, ValueError):
                lines.append((int(pr["v1"]), int(pr["v2"])))
        elif ctype == "thing" and pr.get("type") == 1:
            pos = pr.get("position")
            if isinstance(pos, (list, tuple)) and len(pos) >= 2:
                p1 = (float(pos[0]), float(pos[1]))
            elif "x" in pr and "y" in pr:
                with contextlib.suppress(TypeError, ValueError):
                    p1 = (float(pr["x"]), float(pr["y"]))

    for raw in txt.splitlines():
        ln = raw.strip()
        if not ln or ln.startswith("//"):
            continue
        if ln == "}":
            if cur is not None:
                commit(cur, props)
            cur, props = None, {}
            continue
        if ln == "{":  # body start on its own line (header already seen)
            continue
        if ln.endswith("{"):
            ln = ln[:-1].strip()
            if not ln:
                continue
        if cur is None:
            m = re.match(r"^(\w+)\s*(//.*)?$", ln)
            if m:
                cur = m.group(1)
                props = {}
            continue
        if "=" in ln:
            k, _, v = ln.partition("=")
            props[k.strip()] = _udmf_val(v)
    mids = []
    nv = len(verts)
    for v1, v2 in lines:
        if 0 <= v1 < nv and 0 <= v2 < nv:
            mids.append(((verts[v1][0] + verts[v2][0]) / 2.0,
                         (verts[v1][1] + verts[v2][1]) / 2.0))
    return mids, p1


def classic_geom(file, blk):
    """Classic/BEHA binary map: line midpoints from VERTEXES+LINEDEFS
    (14/16B auto-detected), player start from THINGS (type 1). THINGS
    layout branches exactly like the engine (maploader.cpp:3014-3018):
    BEHAVIOR lump present -> mapthinghexen_t 20 B (x@2, y@4, type@10);
    else mapthing_t 10 B (x@0, y@2, type@6)."""
    vdata = lump_bytes(file, blk["VERTEXES"])
    nverts = len(vdata) // 4
    v = struct.unpack(f"<{2 * nverts}h", vdata)  # x0,y0,x1,y1,...
    nside = (blk["SIDEDEFS"][1] // 30) if blk.get("SIDEDEFS") else (1 << 30)
    ldata = lump_bytes(file, blk["LINEDEFS"])
    stride, n = None, 0
    for st in (14, 16):
        if len(ldata) % st:
            continue
        nn = len(ldata) // st
        o1, o2 = (10, 12) if st == 14 else (12, 14)
        ok = True
        for i in range(nn):
            v1, v2 = struct.unpack_from("<HH", ldata, i * st)
            if not (v1 < nverts and v2 < nverts):
                ok = False
                break
            s1 = struct.unpack_from("<H", ldata, i * st + o1)[0]
            s2 = struct.unpack_from("<H", ldata, i * st + o2)[0]
            if not ((s1 < nside or s1 == 0xFFFF)
                    and (s2 < nside or s2 == 0xFFFF)):
                ok = False
                break
        if ok:
            stride, n = st, nn
            break
    mids = []
    if stride:
        for i in range(n):
            v1, v2 = struct.unpack_from("<HH", ldata, i * stride)
            mids.append(((v[2 * v1] + v[2 * v2]) / 2.0,
                         (v[2 * v1 + 1] + v[2 * v2 + 1]) / 2.0))
    p1 = None
    if blk.get("THINGS") is not None:
        tdata = lump_bytes(file, blk["THINGS"])
        if "BEHAVIOR" in blk:  # Hexen-style 20 B records (engine HasBehavior)
            for i in range(len(tdata) // 20):
                x, y, _z, _ang, typ = struct.unpack_from("<hhhhh",
                                                         tdata, i * 20 + 2)
                if typ == 1:
                    p1 = (float(x), float(y))
                    break
        else:  # classic 10 B records
            for i in range(len(tdata) // 10):
                x, y, _ang, typ = struct.unpack_from("<hhHh", tdata, i * 10)
                if typ == 1:
                    p1 = (float(x), float(y))
                    break
    return mids, p1


def load_geometry(iwad: str, mapname: str):
    """Return (mids, p1) for the map, or (None, None) if not found."""
    files = open_archive(iwad)
    for file in files:
        blocks, _phantoms = map_blocks(file[3])
        for mname, blk in blocks:
            if mname != mapname:
                continue
            if blk.get("VERTEXES") is not None and blk.get("LINEDEFS") is not None:
                return classic_geom(file, blk)
            if blk.get("TEXTMAP") is not None:
                return udmf_geom(lump_bytes(file, blk["TEXTMAP"]))
    return None, None


# --- path building ------------------------------------------------------------

def _clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, v))


def build_path(p1, center, R, T, v_end_target, view_turns, turns):
    """Plan the position profile and solve per-tic commands against the
    engine's exact movement model. Returns (cmds, stats)."""
    px, py = p1
    cx, cy = center
    d = math.hypot(cx - px, cy - py)

    # phase 1: travel P1 -> C (fits in warmup)
    T1 = _clamp(round(d / v_end_target), 50, 300) if d > 1.0 else 50
    T2 = T - T1
    if R < 1.0:
        R = 600.0  # degenerate fallback

    if turns:
        K = turns
    else:
        K = _clamp(round(v_end_target * T2 / (2.0 * math.pi * R)), 2, 8)
    th0 = math.atan2(cy - py, cx - px) if d > 1.0 else 0.0

    # view: A0 along the travel direction, then a constant slow yaw
    yaw_fp = int(round(view_turns * ANGLE_FPP / T))
    yaw_rad = yaw_fp * 2.0 * math.pi / ANGLE_FPP
    A0 = th0

    def planned(t: int):
        if t <= 0:
            return px, py
        if t <= T1:
            f = t / T1
            return px + (cx - px) * f, py + (cy - py) * f
        tau = t - T1
        r = R * tau / T2
        th = th0 + 2.0 * math.pi * K * tau / T2
        return cx + r * math.cos(th), cy + r * math.sin(th)

    v = (0.0, 0.0)
    pos = (px, py)
    cmds = []
    maxv, total, maxc = 0.0, 0.0, 0
    xs, ys = [px], [py]
    for t in range(1, T + 1):
        pt = planned(t)
        vt = (pt[0] - planned(t - 1)[0], pt[1] - planned(t - 1)[1])
        A = A0 + t * yaw_rad
        fx, fy = math.cos(A), math.sin(A)
        sx, sy = math.sin(A), -math.cos(A)  # engine side basis (a = A - 90)
        # solve (v + a) * FRICTION_KEEP = vt  ->  a = vt / FRICTION_KEEP - v
        ax = vt[0] / FRICTION_KEEP - v[0]
        ay = vt[1] / FRICTION_KEEP - v[1]
        fwd = _clamp(int(round((ax * fx + ay * fy) / THRUST_PER_CMD)),
                     -CMD_MAX, CMD_MAX)
        side = _clamp(int(round((ax * sx + ay * sy) / THRUST_PER_CMD)),
                      -CMD_MAX, CMD_MAX)
        # re-simulate with the clamped commands (honest prediction)
        a = (THRUST_PER_CMD * (fwd * fx + side * sx),
             THRUST_PER_CMD * (fwd * fy + side * sy))
        v = ((v[0] + a[0]) * FRICTION_KEEP, (v[1] + a[1]) * FRICTION_KEEP)
        pos = (pos[0] + v[0], pos[1] + v[1])
        cmds.append((0, 0, yaw_fp, fwd, side, 0))  # b, p, y, f, s, u
        maxv = max(maxv, math.hypot(v[0], v[1]))
        total += math.hypot(v[0], v[1])
        maxc = max(maxc, abs(fwd), abs(side))
        xs.append(pos[0])
        ys.append(pos[1])

    stats = {
        "p1": (px, py), "center": (cx, cy), "R": R, "travel_tics": T1,
        "spiral_tics": T2, "turns": K, "yaw_fp": yaw_fp,
        "view_turns": view_turns * ANGLE_FPP / T / ANGLE_FPP,
        "max_speed": maxv, "total_dist": total, "max_cmd": maxc,
        "view_turns_actual": yaw_fp * T / ANGLE_FPP,
        "end": pos, "path_bbox": (min(xs), min(ys), max(xs), max(ys)),
    }
    return cmds, stats


# --- packing ------------------------------------------------------------------

def pack_cmd(c, basis) -> bytes:
    """c = (buttons, pitch, yaw, fwd, side, up); basis = previous cmd or None."""
    b = basis or (0, 0, 0, 0, 0, 0)
    flags = 0
    out = bytearray()
    btn = c[0] ^ b[0]
    if btn:
        flags |= UCMDF_BUTTONS
        groups = [btn & 0x7F, (btn >> 7) & 0x7F, (btn >> 14) & 0x7F,
                  (btn >> 21) & 0xFF]
        for i, g in enumerate(groups):
            more = 0
            if i < 3 and btn & (0x7F << (7 * (i + 1))):
                more = MORE_BUTTONS
            out.append(g | more)
            if not more:
                break
    for val, bval, f in ((c[1], b[1], UCMDF_PITCH), (c[2], b[2], UCMDF_YAW),
                         (c[3], b[3], UCMDF_FORWARDMOVE),
                         (c[4], b[4], UCMDF_SIDEMOVE),
                         (c[5], b[5], UCMDF_UPMOVE)):
        if val != bval:
            flags |= f
            out += struct.pack(">h", val)
    return bytes([flags]) + bytes(out)


def build(mapname: str, cmds, seed: int, cheats: list) -> bytes:
    # ZDHD
    zdh = (struct.pack(">HH", DEMOGAMEVERSION, MINVER)
           + mapname.encode("ascii") + b"\x00"
           + struct.pack(">I", seed & 0xFFFFFFFF)
           + b"\x00")  # consoleplayer = 0
    # UINF: player 0, minimal userinfo (leading backslash, no fields)
    uinf = b"\x00" + b"\\" + b"\x00"
    # BODY: cheat marker(s), then one packed cmd per tic, then STOP
    body = bytearray()
    for c in cheats:
        body += bytes([DEM_GENERICCHEAT, CHEATS[c]])
    basis = None
    for cmd in cmds:
        body += bytes([DEM_USERCMD]) + pack_cmd(cmd, basis)
        basis = cmd
    body += bytes([DEM_STOP])

    chunks_ = (chunk(b"ZDHD", zdh) + chunk(b"UINF", uinf)
               + chunk(b"BODY", bytes(body), pad=False))
    total = len(b"ZDEM") + len(chunks_)
    return b"FORM" + struct.pack(">I", total) + b"ZDEM" + chunks_


def main() -> int:
    p = argparse.ArgumentParser(
        description="synthesize a deterministic ZDoom IFF/ZDEM demo "
                    "(spiral fan-out from the computed map center)")
    p.add_argument("--map", required=True, help="map name, e.g. MAP01, E1M2, 20PAM")
    p.add_argument("--iwad", required=True,
                   help="WAD/PK3 containing the map (geometry is read from it)")
    p.add_argument("--out", required=True, help="output .lmp path")
    p.add_argument("--tics", type=int, default=2600,
                   help="demo length in tics (default 2600 = ~74s; protocol "
                        "window = tics 350..2450)")
    p.add_argument("--v-end", type=float, default=8.0,
                   help="target end speed u/tic for spiral sizing "
                        "(default 8.0 = 280 u/s)")
    p.add_argument("--view-turns", type=float, default=2.0,
                   help="total view rotations over the demo (default 2)")
    p.add_argument("--turns", type=int, default=0,
                   help="force spiral turn count (default 0 = auto, 2..8)")
    p.add_argument("--center", default="",
                   help="override center 'x,y' (default: median of midpoints)")
    p.add_argument("--radius", type=float, default=0.0,
                   help="override max spiral radius in units (default: map extent)")
    p.add_argument("--seed", type=int, default=0xA1B2C3D4,
                   help="RNG seed baked into the demo (default 0xA1B2C3D4)")
    p.add_argument("--cheat", action="append", choices=sorted(CHEATS),
                   default=["noclip2"],
                   help="cheat to apply at tic 0 (default: noclip2)")
    a = p.parse_args()

    if a.tics < 500:
        p.error("--tics must be >= 500 (window 350..2450 needs 2600 for the "
                "production set; smaller values are for probing only)")

    mids, p1 = load_geometry(a.iwad, a.map)
    if not mids:
        print(f"warning: no line geometry found for {a.map} in {a.iwad}; "
              f"falling back to center=(0,0) R=600", file=sys.stderr)
        center, R = (0.0, 0.0), 600.0
        if p1 is None:
            p1 = (0.0, 0.0)
    else:
        center = (statistics.median(m[0] for m in mids),
                  statistics.median(m[1] for m in mids))
        R = max(math.hypot(m[0] - center[0], m[1] - center[1]) for m in mids)
        if p1 is None:
            print(f"warning: no player start (thing type 1) in {a.map}; "
                  f"starting at the map center", file=sys.stderr)
            p1 = center
    if a.center:
        try:
            cx, cy = (float(x) for x in a.center.split(","))
            center = (cx, cy)
        except ValueError:
            p.error("--center must be 'x,y'")
    if a.radius > 0:
        R = a.radius

    cmds, st = build_path(p1, center, R, a.tics, a.v_end, a.view_turns,
                          a.turns)
    data = build(a.map, cmds, a.seed, a.cheat)
    try:
        with open(a.out, "wb") as f:
            f.write(data)
    except OSError as e:
        print(f"error: cannot write {a.out}: {e}", file=sys.stderr)
        return 1

    bx = st["path_bbox"]
    mx0 = min(m[0] for m in mids) if mids else float("nan")
    my0 = min(m[1] for m in mids) if mids else float("nan")
    mx1 = max(m[0] for m in mids) if mids else float("nan")
    my1 = max(m[1] for m in mids) if mids else float("nan")
    print(f"wrote {a.out} ({len(data)} bytes): map={a.map} tics={a.tics} "
          f"seed={a.seed:#x} cheats={a.cheat}")
    print(f"  geometry: {len(mids) if mids else 0} lines, P1=({p1[0]:.0f},{p1[1]:.0f}) "
          f"center=({center[0]:.0f},{center[1]:.0f}) extent R={R:.0f} u")
    print(f"  path: travel {st['travel_tics']} tics -> "
          f"spiral {st['spiral_tics']} tics, {st['turns']} turns, "
          f"view {st['view_turns_actual']:.2f} turns ({st['yaw_fp']} fp/tic)")
    print(f"  motion: max speed {st['max_speed']:.2f} u/tic "
          f"({st['max_speed'] * 35:.0f} u/s), distance {st['total_dist']:.0f} u, "
          f"max cmd {st['max_cmd']}")
    print(f"  path bbox  x[{bx[0]:.0f},{bx[2]:.0f}] y[{bx[1]:.0f},{bx[3]:.0f}]")
    print(f"  map bbox   x[{mx0:.0f},{mx1:.0f}] y[{my0:.0f},{my1:.0f}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
