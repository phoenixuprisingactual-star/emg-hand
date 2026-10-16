#!/usr/bin/env python3
"""
Tripod claw/gripper STL generator - proof of concept.
Generates a simple 3-pronged Doc Ock style claw:
- Central cylindrical hub with mounting hole
- Three tapered triangular claw fingers at 120 degree intervals
- Each finger has two segments with a slight inward curve

Pure Python, no dependencies. Outputs ASCII STL.
"""

import math

def vec_sub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

def vec_cross(a, b):
    return (a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0])

def vec_norm(v):
    l = math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
    if l < 1e-9:
        return (0, 0, 1)
    return (v[0]/l, v[1]/l, v[2]/l)

class STLBuilder:
    def __init__(self, name="model"):
        self.name = name
        self.facets = []

    def add_triangle(self, v1, v2, v3):
        """Add a triangle, auto-compute normal."""
        e1 = vec_sub(v2, v1)
        e2 = vec_sub(v3, v1)
        n = vec_norm(vec_cross(e1, e2))
        self.facets.append((n, v1, v2, v3))

    def add_quad(self, v1, v2, v3, v4):
        """Add a quad as two triangles."""
        self.add_triangle(v1, v2, v3)
        self.add_triangle(v1, v3, v4)

    def add_box(self, cx, cy, cz, sx, sy, sz):
        """Axis-aligned box centered at (cx,cy,cz) with size (sx,sy,sz)."""
        x0, x1 = cx - sx/2, cx + sx/2
        y0, y1 = cy - sy/2, cy + sy/2
        z0, z1 = cz - sz/2, cz + sz/2
        # 8 corners
        c = [(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),
             (x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
        # faces (outward winding)
        self.add_quad(c[0], c[3], c[2], c[1])  # bottom
        self.add_quad(c[4], c[5], c[6], c[7])  # top
        self.add_quad(c[0], c[1], c[5], c[4])  # front
        self.add_quad(c[2], c[3], c[7], c[6])  # back
        self.add_quad(c[1], c[2], c[6], c[5])  # right
        self.add_quad(c[3], c[0], c[4], c[7])  # left

    def add_cylinder(self, cx, cy, cz, r, h, segments=24):
        """Vertical cylinder centered at (cx,cy,cz)."""
        z0, z1 = cz - h/2, cz + h/2
        # side wall
        for i in range(segments):
            a0 = 2 * math.pi * i / segments
            a1 = 2 * math.pi * (i+1) / segments
            p0 = (cx + r*math.cos(a0), cy + r*math.sin(a0), z0)
            p1 = (cx + r*math.cos(a1), cy + r*math.sin(a1), z0)
            p2 = (cx + r*math.cos(a1), cy + r*math.sin(a1), z1)
            p3 = (cx + r*math.cos(a0), cy + r*math.sin(a0), z1)
            self.add_quad(p0, p1, p2, p3)
        # caps (as triangle fans)
        cb = (cx, cy, z0)
        ct = (cx, cy, z1)
        for i in range(segments):
            a0 = 2 * math.pi * i / segments
            a1 = 2 * math.pi * (i+1) / segments
            # bottom cap (normal -z)
            self.add_triangle(
                cb,
                (cx + r*math.cos(a1), cy + r*math.sin(a1), z0),
                (cx + r*math.cos(a0), cy + r*math.sin(a0), z0))
            # top cap (normal +z)
            self.add_triangle(
                ct,
                (cx + r*math.cos(a0), cy + r*math.sin(a0), z1),
                (cx + r*math.cos(a1), cy + r*math.sin(a1), z1))

    def add_tapered_prism(self, base_center, base_w, base_d,
                          tip_center, tip_w, tip_d):
        """
        A tapered box (frustum) from base to tip.
        base_center/tip_center are (x,y,z) tuples.
        base_w/d and tip_w/d are widths (x) and depths (y).
        """
        bx, by, bz = base_center
        tx, ty, tz = tip_center
        # 8 corners
        b = [(bx-base_w/2, by-base_d/2, bz),
             (bx+base_w/2, by-base_d/2, bz),
             (bx+base_w/2, by+base_d/2, bz),
             (bx-base_w/2, by+base_d/2, bz)]
        t = [(tx-tip_w/2, ty-tip_d/2, tz),
             (tx+tip_w/2, ty-tip_d/2, tz),
             (tx+tip_w/2, ty+tip_d/2, tz),
             (tx-tip_w/2, ty+tip_d/2, tz)]
        # sides
        for i in range(4):
            j = (i+1) % 4
            self.add_quad(b[i], b[j], t[j], t[i])
        # base cap
        self.add_quad(b[0], b[3], b[2], b[1])
        # tip cap
        self.add_quad(t[0], t[1], t[2], t[3])

    def write(self, path):
        with open(path, 'w') as f:
            f.write(f"solid {self.name}\n")
            for n, v1, v2, v3 in self.facets:
                f.write(f"  facet normal {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n")
                f.write("    outer loop\n")
                f.write(f"      vertex {v1[0]:.6f} {v1[1]:.6f} {v1[2]:.6f}\n")
                f.write(f"      vertex {v2[0]:.6f} {v2[1]:.6f} {v2[2]:.6f}\n")
                f.write(f"      vertex {v3[0]:.6f} {v3[1]:.6f} {v3[2]:.6f}\n")
                f.write("    endloop\n")
                f.write("  endfacet\n")
            f.write(f"endsolid {self.name}\n")
        print(f"Wrote {len(self.facets)} facets to {path}")


def build_tripod_claw():
    """
    Tripod claw dimensions (mm):
    - Hub: 30mm diameter, 12mm tall, centered at origin
    - Mounting hole: represented as a recessed cylinder marker
      (actual boolean subtraction not implemented; hub is solid)
    - 3 fingers at 120 deg, each ~45mm long, tapered, curving inward
    """
    s = STLBuilder("tripod_claw")

    # --- Central hub ---
    s.add_cylinder(0, 0, 0, r=15, h=12, segments=32)

    # --- Top mounting boss (smaller cylinder on top) ---
    s.add_cylinder(0, 0, 8, r=8, h=8, segments=24)

    # --- Three claw fingers ---
    # Each finger: base segment going outward+down, tip segment curving inward
    hub_r = 13          # finger base radius from center
    seg1_len = 25       # first segment length
    seg2_len = 20       # second segment length
    base_w = 10         # finger base width
    base_d = 8          # finger base depth
    mid_w = 7           # mid joint width
    mid_d = 6           # mid joint depth
    tip_w = 2           # tip width (sharp point)
    tip_d = 2           # tip depth

    for k in range(3):
        angle = 2 * math.pi * k / 3  # 0, 120, 240 degrees
        dx = math.cos(angle)
        dy = math.sin(angle)

        # Base of finger (at hub edge, z = -2 slightly below hub center)
        bx = dx * hub_r
        by = dy * hub_r
        bz = -2

        # Mid joint: outward and downward
        mx = dx * (hub_r + seg1_len * 0.9)
        my = dy * (hub_r + seg1_len * 0.9)
        mz = bz - seg1_len * 0.45  # droop downward

        # Tip: continue outward slightly, then curve inward and down to a point
        # Inward curve: pull back toward center axis
        tx = dx * (hub_r + seg1_len * 0.9 + seg2_len * 0.3)
        ty = dy * (hub_r + seg1_len * 0.9 + seg2_len * 0.3)
        tz = mz - seg2_len * 0.9  # steep downward

        # Segment 1: base -> mid (tapered)
        s.add_tapered_prism(
            (bx, by, bz), base_w, base_d,
            (mx, my, mz), mid_w, mid_d)

        # Segment 2: mid -> tip (tapered to point)
        s.add_tapered_prism(
            (mx, my, mz), mid_w, mid_d,
            (tx, ty, tz), tip_w, tip_d)

    return s


if __name__ == "__main__":
    import os
    claw = build_tripod_claw()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tripod_claw_v1.stl")
    claw.write(out)
