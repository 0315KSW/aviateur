#!/usr/bin/env python3
"""Analyse the per-second CSV written by the lowlat receiver patch
(logs/link_<time>.csv).

1. Splits the file into transmission runs (consecutive seconds with
   video_frames > 0).
2. Prints totals per run.
3. Optionally sums a fixed wall-clock window per run, e.g. the 60 s windows
   used in the 10/28 report:
     --window 16:32:58-16:33:58 --window 16:34:22-16:35:22 ...

Usage: python3 analyze_link_csv.py <link.csv> [--window HH:MM:SS-HH:MM:SS ...]
"""
import argparse
import csv

COUNTERS = ["wifi_frames", "video_frames", "wfb_session", "wfb_dec_err",
            "fec_recovered", "lost_after_fec", "wfb_out_pkts", "rtp_gap_pkts",
            "frames_decoded", "frames_rendered"]


def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def split_runs(rows):
    runs, cur = [], []
    for r in rows:
        if int(r["video_frames"]) > 0:
            cur.append(r)
        elif cur:
            runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)
    return runs


def summarise(rows):
    s = {k: sum(int(r[k]) for r in rows) for k in COUNTERS}
    snr = [float(r["snr0_avg"]) for r in rows if int(r["video_frames"]) > 0]
    s["snr0_mean"] = round(sum(snr) / len(snr), 1) if snr else 0.0
    return s


def hms(r):
    return r["time"][11:19]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--window", action="append", default=[])
    a = ap.parse_args()
    rows = load(a.csv)
    print(f"rows: {len(rows)}")
    for i, run in enumerate(split_runs(rows), 1):
        s = summarise(run)
        print(f"run {i}: {hms(run[0])}-{hms(run[-1])} ({len(run)} s) "
              f"video={s['video_frames']} fec_rec={s['fec_recovered']} "
              f"lost_after_fec={s['lost_after_fec']} rendered={s['frames_rendered']} "
              f"snr0={s['snr0_mean']}")
    for w in a.window:
        start, end = w.split("-")
        sel = [r for r in rows if start <= hms(r) < end]
        s = summarise(sel)
        print(f"window {w} ({len(sel)} s): video={s['video_frames']} "
              f"lost_after_fec={s['lost_after_fec']} rendered={s['frames_rendered']} "
              f"snr0={s['snr0_mean']}")


if __name__ == "__main__":
    main()
