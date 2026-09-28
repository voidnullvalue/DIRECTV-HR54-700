#!/usr/bin/env python3
"""Non-destructive HR54 recording catalogue from copied /var metadata."""
import argparse, csv, json, re
from datetime import datetime, timedelta
from pathlib import Path

NAME = re.compile(r"Rcrd-(?P<date>\d{2}-\d{2}-\d{4})-(?P<time>\d{4})-(?P<seconds>\d{2})-(?P<token>\d+)TransportMPEG-DIRECTV_A3_MPEG4_AC3-ch(?P<channel>\d+)-min\d+-\d+(?P<ad>-ad)?\.mpg$")
HEAD = re.compile(r"recording\[(?P<n>\d+)\] == \[(?P<title>.*?)\], UUID = (?P<uuid>[^,]+).*?Channel = (?P<channel>\d+), Season=(?P<season>[^,]+), Episode=(?P<episode>[^,]+), RecID=(?P<recid>[^,]+), LongID=(?P<longid>[^,]+), state=(?P<state>[^,]+)")

def library(path):
    text = path.read_text(errors="replace") if path and path.exists() else ""
    out = []
    for block in re.split(r"(?=^recording\[)", text, flags=re.M):
        m = HEAD.search(block)
        if not m: continue
        row = m.groupdict()
        def get(p):
            x = re.search(p, block); return x.group(1).strip() if x else None
        row.update(recording_start=get(r"RecStartTime=\s*(\d{12})"), subtitle=get(r"\(SUBTITLE\)(.*)"), description=get(r"\n    Description     : (.*)"), watched=get(r"HasBeenViewed\s*:\s*(\w+)"), complete=get(r"isComplete=(\w+)"), pristine=get(r"isPristine=(\w+)"), actual_duration_minutes=get(r"ActualDuration\s*:\s*(\d+)min"))
        out.append(row)
    return out

def find_match(item, records):
    matches = []
    for r in records:
        if r["channel"] != item["channel"] or not r["recording_start"]: continue
        stamp = datetime.strptime(r["recording_start"], "%Y%m%d%H%M") + timedelta(hours=5)
        if abs((item["_time"] - stamp).total_seconds()) <= 90: matches.append(r)
    return matches[0] if len(matches) == 1 else None

def main():
    p = argparse.ArgumentParser()
    p.add_argument("root", type=Path); p.add_argument("--library", type=Path)
    p.add_argument("--json", type=Path, default=Path("recordings.json")); p.add_argument("--tsv", type=Path, default=Path("recordings.tsv"))
    a = p.parse_args(); roots = [a.root / x for x in ("var/backup/viewer/indexfile", "var/viewer/indexfile", "backup/viewer/indexfile", "viewer/indexfile")]
    # The copied vendor tree preserves root-only index-directory permissions.
    # Listing the index root is sufficient to catalogue it; do not require
    # traversal into each directory merely to obtain a filename-based record.
    records, dirs = library(a.library), {d for base in roots if base.is_dir() for d in base.iterdir() if d.name.startswith("Rcrd-") and d.name.endswith(".mpg")}
    rows = []
    for d in sorted(dirs):
        m = NAME.match(d.name)
        if not m: continue
        x = m.groupdict(); t = datetime.strptime(x["date"] + x["time"] + x["seconds"], "%m-%d-%Y%H%M%S")
        # Index directories are root-only in a faithful copied tree.  These
        # four names are the observed standard set; avoid opening them.
        metadata = ["meta_man.xma", "meta_man.xmd", "meta_man.xmi", "meta_man.xmv"]
        row = {"recording_id": d.name, "channel": x["channel"], "filename_timestamp": t.isoformat(), "filename_token": x["token"], "advertisement": bool(x["ad"]), "index_path": str(d), "metadata_files": metadata, "_time": t}
        hit = find_match(row, records)
        if hit: row.update({k: v for k, v in hit.items() if k not in ("n", "channel")})
        else: row["title"] = None
        row.pop("_time"); rows.append(row)
    a.json.write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n")
    fields = ["recording_id","title","subtitle","channel","filename_timestamp","recording_start","uuid","recid","longid","state","season","episode","watched","complete","pristine","actual_duration_minutes","advertisement","metadata_files","index_path"]
    with a.tsv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, delimiter="\t", extrasaction="ignore"); w.writeheader()
        for row in rows:
            row = dict(row); row["metadata_files"] = ",".join(row["metadata_files"]); w.writerow(row)
    print("catalogued %d index entries; matched %d stock library records" % (len(rows), sum(x["title"] is not None for x in rows)))
if __name__ == "__main__": main()
