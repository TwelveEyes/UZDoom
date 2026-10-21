"""gamescope_wrap.py — run every engine launch of the A/B kit headless.

All in-game runs (abtest.py captures, lm_obj_compare.py, contact_sheet.py,
run_matrix.sh) go through a headless gamescope session so they neither need
nor disturb the desktop. The nested display is exactly the requested size,
so the engine's -width/-height window is the whole output.
"""
import os


def wrap(cmd, w, h):
    """cmd prefixed with a headless gamescope session of w x h."""
    return ["gamescope", "--backend", "headless", "-W", str(w), "-H", str(h),
            "-w", str(w), "-h", str(h), "--", *cmd]


def env():
    """Environment for the wrapped engine: SDL on gamescope's Xwayland."""
    e = dict(os.environ)
    e["SDL_VIDEODRIVER"] = "x11"
    return e
