#!/usr/bin/env python3
"""Fetch a road-following route once and cache it in tools/routes/.

Why this exists: replaying a straight line between two guessed waypoints puts
the simulated car off the road network, so Waze reports "head to the indicated
road", `st` comes back empty and no speed limit is known. A polyline snapped to
real roads fixes all three at once.

Network is used only here. The cached file is what the replay reads, so a
simulated drive needs no connectivity.

    ./tools/fetch_route.py nguyen-trai 21.00210,105.82130 20.98700,105.80350
"""
import json, sys, urllib.request
from pathlib import Path

OSRM = "https://router.project-osrm.org/route/v1/driving"

def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    name, start, end = sys.argv[1], sys.argv[2], sys.argv[3]
    (slat, slon) = [float(x) for x in start.split(",")]
    (elat, elon) = [float(x) for x in end.split(",")]

    # OSRM takes lon,lat - the opposite order to everything else here.
    url = f"{OSRM}/{slon},{slat};{elon},{elat}?overview=full&geometries=geojson"
    with urllib.request.urlopen(url, timeout=30) as r:
        data = json.load(r)
    if data.get("code") != "Ok":
        print("routing failed:", data.get("code"), file=sys.stderr)
        return 1

    route = data["routes"][0]
    coords = [[lat, lon] for lon, lat in route["geometry"]["coordinates"]]

    out = Path(__file__).parent / "routes" / f"{name}.json"
    out.write_text(json.dumps({
        "name": name,
        "distance_m": round(route["distance"]),
        "duration_s": round(route["duration"]),
        "coords": coords,
    }, ensure_ascii=False, indent=1))
    print(f"{out.name}: {len(coords)} points, {route['distance']/1000:.2f} km")
    return 0

if __name__ == "__main__":
    sys.exit(main())
