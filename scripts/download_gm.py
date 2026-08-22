#!/usr/bin/env python3
"""
Download GM games: Lichess Elite DB + fresh 2700+ GM games via Lichess API
Usage:
  python scripts/download_gm.py --source elite --elite-path /path/to/lichess_elite_db.pgn.zst --out data/raw/
  python scripts/download_gm.py --source lichess --min-elo 2700 --max-games 50000 --out data/raw/
"""
import argparse, os, sys, pathlib, requests, time, zstandard, io

def download_elite(elite_path, out_dir):
    out_dir = pathlib.Path(out_dir); out_dir.mkdir(parents=True, exist_ok=True)
    if elite_path.endswith(".zst"):
        print(f"decompressing {elite_path}...")
        with open(elite_path, "rb") as fh:
            dctx = zstandard.ZstdDecompressor()
            stream = dctx.stream_reader(fh)
            out = out_dir / "elite.pgn"
            with open(out, "wb") as out_fh:
                while True:
                    chunk = stream.read(16384)
                    if not chunk: break
                    out_fh.write(chunk)
            print(f"wrote {out}")
    else:
        import shutil
        shutil.copy(elite_path, out_dir / "elite.pgn")
        print(f"copied elite.pgn")

def download_lichess(min_elo, max_games, out_dir, token=None):
    out_dir = pathlib.Path(out_dir); out_dir.mkdir(parents=True, exist_ok=True)
    # Use lichess API: /api/games/user/{username} or /api/tournament etc.
    # Simpler: use lichess database export via https://database.lichess.org/ and filter
    # For fresh GM games, we use https://lichess.org/api/games/user/{username} for top players
    # First fetch top 50 players via https://lichess.org/api/player
    headers = {}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    # Get leaderboard
    print("fetching top players...")
    r = requests.get("https://lichess.org/api/player", headers=headers)
    r.raise_for_status()
    data = r.json()
    # data contains top 10 lists
    usernames = []
    for cat in ["bullet","blitz","rapid","classical"]:
        if cat in data:
            for e in data[cat][:10]:
                usernames.append(e["username"])
    usernames = list(set(usernames))[:20]
    print(f"top usernames: {usernames}")
    out_path = out_dir / f"gm_{min_elo}.pgn"
    with open(out_path, "w") as out:
        for u in usernames:
            print(f"fetching {u}...")
            params = {"max": max_games//len(usernames), "rated": "true", "perfType": "classical,rapid", "moves": "true", "tags": "true", "clocks": "false"}
            # Lichess API for games: GET /api/games/user/{username}
            url = f"https://lichess.org/api/games/user/{u}"
            try:
                with requests.get(url, headers=headers, params=params, stream=True, timeout=30) as resp:
                    if resp.status_code != 200:
                        print(f"fail {u} {resp.status_code}")
                        continue
                    for line in resp.iter_lines():
                        if line:
                            out.write(line.decode("utf-8") + "\n")
                time.sleep(1)
            except Exception as e:
                print(f"error {u}: {e}")
    print(f"wrote {out_path}")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--source", choices=["elite","lichess"], required=True)
    p.add_argument("--elite-path", default=None)
    p.add_argument("--min-elo", type=int, default=2700)
    p.add_argument("--max-games", type=int, default=50000)
    p.add_argument("--out", default="data/raw")
    p.add_argument("--token", default=None, help="lichess API token for higher rate limit")
    args = p.parse_args()
    if args.source == "elite":
        if not args.elite_path:
            print("need --elite-path for elite source"); sys.exit(1)
        download_elite(args.elite_path, args.out)
    else:
        download_lichess(args.min_elo, args.max_games, args.out, args.token)
