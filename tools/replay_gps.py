#!/usr/bin/env python3
"""Drive the phone along a route without moving it.

Interpolates between waypoints and pushes each point to the device with
idevicesetlocation, so Waze believes it is driving. Requires Xcode's
DeveloperDiskImage to be mounted first (see ios/sim-drive.sh).

    ./tools/replay_gps.py --speed 40 --hz 1
"""
import argparse, json, math, pathlib, subprocess, sys, time

# Straight-line fallback only. Interpolating between hand-picked waypoints puts
# the car off the road network: Waze then reports "head to the indicated road",
# `st` is empty and no speed limit is known. Prefer a cached road-following
# route from tools/routes/ (see fetch_route.py).
FALLBACK_ROUTE = [
    (21.00210, 105.82130),   # Nga Tu So
    (20.98700, 105.80350),
]

def load_route(name):
    path = pathlib.Path(__file__).parent / "routes" / f"{name}.json"
    if not path.exists():
        print(f"  no cached route {name!r}; falling back to a straight line "
              f"(run tools/fetch_route.py to snap one to real roads)",
              file=sys.stderr)
        return FALLBACK_ROUTE
    data = json.loads(path.read_text())
    return [tuple(c) for c in data["coords"]]

def haversine(a, b):
    R = 6371000.0
    lat1, lon1, lat2, lon2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = (math.sin((lat2 - lat1) / 2) ** 2
         + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2)
    return 2 * R * math.asin(math.sqrt(h))

def interpolate(route, step_m):
    """Yield points spaced step_m apart along the polyline."""
    for a, b in zip(route, route[1:]):
        seg = haversine(a, b)
        n = max(1, int(seg / step_m))
        for i in range(n):
            t = i / n
            yield (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    yield route[-1]

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--speed", type=float, default=40.0, help="km/h")
    p.add_argument("--hz", type=float, default=1.0, help="position updates per second")
    p.add_argument("--loop", action="store_true", help="repeat forever")
    p.add_argument("--route", default="nguyen-trai",
                   help="cached route name in tools/routes/")
    args = p.parse_args()

    route = load_route(args.route)

    step_m = (args.speed * 1000 / 3600) / args.hz
    pts = list(interpolate(route, step_m))
    total = sum(haversine(a, b) for a, b in zip(route, route[1:]))
    print(f"{len(pts)} points, {total/1000:.2f} km at {args.speed:.0f} km/h "
          f"({args.hz:.0f} Hz, {step_m:.1f} m apart) ~= {total/(args.speed*1000/3600):.0f}s")

    while True:
        for i, (lat, lon) in enumerate(pts):
            t0 = time.time()
            r = subprocess.run(["idevicesetlocation", f"{lat:.6f}", f"{lon:.6f}"],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print(f"  idevicesetlocation failed: {r.stderr.strip()}", file=sys.stderr)
                return 1
            if i % 10 == 0 or i == len(pts) - 1:
                print(f"  {i+1}/{len(pts)}  {lat:.5f},{lon:.5f}", flush=True)
            # idevicesetlocation itself takes a moment; only sleep the remainder.
            time.sleep(max(0.0, 1.0 / args.hz - (time.time() - t0)))
        print()
        if not args.loop:
            return 0

if __name__ == "__main__":
    sys.exit(main())
