"""
Profiling-data analysis tools for Unreal MCP.

Parsers + MCP tools for the profiling artifacts Unreal emits:
  - .memreport  : CPU/GPU *memory* dump (the `memreport` console command).
  - CSV profiler: per-frame CPU/GPU timing (FCsvProfiler `.csv`).
  - .utrace      : Unreal Insights timeline (forwarded to the C++ `analyze_trace`
                   command, which uses TraceServices in the editor).

The memreport/CSV parsers are pure Python (no editor needed) and double as a CLI:
    python profiling_tools.py memreport <file> [--top N] [--json]
    python profiling_tools.py memreport-diff <a> <b>
    python profiling_tools.py csv <file> [--hitch-ms 33.3]
"""
import argparse
import json
import logging
import math
import re
import sys

logger = logging.getLogger("UnrealMCP")


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def _percentiles(values, ps=(50, 90, 95, 99, 99.9)):
    if not values:
        return {}
    s = sorted(values)
    out = {}
    for p in ps:
        k = (len(s) - 1) * (p / 100.0)
        lo, hi = math.floor(k), math.ceil(k)
        out[f"p{p:g}"] = round(s[lo] + (s[hi] - s[lo]) * (k - lo), 4)
    return out


def _stats(values):
    if not values:
        return {}
    n = len(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / n if n > 1 else 0.0
    out = {"count": n, "min": round(min(values), 4), "max": round(max(values), 4),
           "avg": round(mean, 4), "stddev": round(math.sqrt(var), 4)}
    out.update(_percentiles(values))
    return out


# --------------------------------------------------------------------------- #
# .memreport (memory)
# --------------------------------------------------------------------------- #
def read_memreport_sections(path):
    header, sections, cur, buf = [], {}, None, []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            b = re.match(r'MemReport: Begin command "(.*)"', line)
            if b:
                (sections.setdefault(cur, []).extend(buf) if cur is not None else header.extend(buf))
                cur, buf = b.group(1), []
                continue
            if re.match(r"MemReport: End command", line):
                if cur is not None:
                    sections.setdefault(cur, []).extend(buf)
                cur, buf = None, []
                continue
            buf.append(line)
    (sections.setdefault(cur, []).extend(buf) if cur is not None else header.extend(buf))
    return header, sections


def _find(lines, pattern):
    rx = re.compile(pattern)
    for ln in lines:
        m = rx.search(ln)
        if m:
            return m
    return None


def _parse_class_table(lines, top):
    rows, started = [], False
    for ln in lines:
        if "Class" in ln and "ResExcKB" in ln:
            started = True
            continue
        if not started:
            continue
        t = ln.split()
        if len(t) < 8:
            continue
        try:
            rows.append({"class": t[0], "count": int(t[1]),
                         "res_total_mb": round(float(t[4]) / 1024, 2),
                         "res_sys_mb": round(float(t[5]) / 1024, 2),
                         "res_gpu_mb": round(float(t[6]) / 1024, 2)})
        except ValueError:
            continue
    rows.sort(key=lambda r: r["res_total_mb"], reverse=True)
    return rows[:top]


def analyze_memreport_file(path, top=15):
    header, sections = read_memreport_sections(path)
    mem = sections.get("Mem FromReport", []) + header
    out = {"file": path, "header": {}, "platform_memory": {},
           "gpu_rhi_categories": [], "gpu_rhi_total_mb": None,
           "top_classes_by_resource_mem": _parse_class_table(sections.get("obj list -resourcesizesort", []), top),
           "top_static_meshes": _parse_class_table(sections.get("obj list class=StaticMesh -resourcesizesort", []), top),
           "top_skeletal_meshes": _parse_class_table(sections.get("obj list class=SkeletalMesh -resourcesizesort", []), top),
           "textures": {}, "allocator": {}, "sections_present": sorted(sections)}

    for key, field in [("Device Name", "device"), ("Config", "config"),
                       ("Changelist", "changelist"), ("Time Since Boot", "uptime")]:
        m = _find(header, rf"{key}:\s*(.+)")
        if m:
            out["header"][field] = m.group(1).strip()

    pp = _find(mem, r"Process Physical Memory:\s*([\d.]+) MB used,\s*([\d.]+) MB peak")
    phys = _find(mem, r"Physical Memory:\s*([\d.]+) MB used,\s*([\d.]+) MB free,\s*([\d.]+) MB total")
    if pp:
        out["platform_memory"]["process_used_mb"] = float(pp.group(1))
        out["platform_memory"]["process_peak_mb"] = float(pp.group(2))
    if phys:
        out["platform_memory"].update(used_mb=float(phys.group(1)), free_mb=float(phys.group(2)), total_mb=float(phys.group(3)))

    rx = re.compile(r"Shown \d+ entries with name (\S+)\. Size:\s*([\d.]+)/([\d.]+) MB \(([\d.]+)%")
    for cmd, lines in sections.items():
        if cmd.startswith("rhi.dumpresourcememory summary name="):
            for ln in lines:
                m = rx.search(ln)
                if m:
                    out["gpu_rhi_categories"].append({"category": m.group(1), "mb": float(m.group(2)),
                                                      "pct_of_total": float(m.group(4))})
                    out["gpu_rhi_total_mb"] = float(m.group(3))
    out["gpu_rhi_categories"].sort(key=lambda c: c["mb"], reverse=True)
    out["gpu_rhi_categories"] = [c for c in out["gpu_rhi_categories"] if c["mb"] > 0][:top]

    rtot = _find(sections.get("rhi.DumpResourceMemory", []), r"Total tracked resource size:\s*([\d.]+) MB")
    if rtot:
        out["gpu_rhi_total_tracked_mb"] = float(rtot.group(1))

    tlines = sections.get("listtextures nonvt", [])
    tt = _find(tlines, r"Total size:\s*InMem=\s*([\d.]+) MB\s*OnDisk=\s*([\d.]+) MB\s*Count=(\d+)")
    if tt:
        out["textures"] = {"total_inmem_mb": float(tt.group(1)), "total_ondisk_mb": float(tt.group(2)),
                           "count": int(tt.group(3)), "largest": []}
        tex = []
        for ln in tlines:
            m = re.match(r"\s*\d+x\d+ \((\d+) KB.*?\),.*?,\s*(PF_\w+),\s*(\w+),\s*(\S+),", ln)
            if m:
                tex.append({"mb": round(int(m.group(1)) / 1024, 2), "format": m.group(2),
                            "group": m.group(3), "path": m.group(4)})
        tex.sort(key=lambda t: t["mb"], reverse=True)
        out["textures"]["largest"] = tex[:top]

    am = _find(mem, r"Small Pool Allocations:\s*([\d.]+)mb")
    ao = _find(mem, r"Small Pool OS Allocated:\s*([\d.]+)mb")
    if am:
        out["allocator"]["small_pool_alloc_mb"] = float(am.group(1))
    if ao:
        out["allocator"]["small_pool_os_mb"] = float(ao.group(1))
    return out


def diff_memreports(path_a, path_b, top=15):
    a, b = analyze_memreport_file(path_a, 9999), analyze_memreport_file(path_b, 9999)
    def cat_map(rep):
        return {c["category"]: c["mb"] for c in rep["gpu_rhi_categories"]}
    def cls_map(rep):
        return {c["class"]: c["res_total_mb"] for c in rep["top_classes_by_resource_mem"]}
    out = {"base": path_a, "current": path_b,
           "platform_memory_delta_mb": {}, "gpu_category_deltas_mb": [], "class_deltas_mb": []}
    for k in set(a["platform_memory"]) | set(b["platform_memory"]):
        out["platform_memory_delta_mb"][k] = round(b["platform_memory"].get(k, 0) - a["platform_memory"].get(k, 0), 2)
    ca, cb = cat_map(a), cat_map(b)
    for k in set(ca) | set(cb):
        d = round(cb.get(k, 0) - ca.get(k, 0), 2)
        if abs(d) >= 0.1:
            out["gpu_category_deltas_mb"].append({"category": k, "base": ca.get(k, 0), "current": cb.get(k, 0), "delta": d})
    out["gpu_category_deltas_mb"].sort(key=lambda x: abs(x["delta"]), reverse=True)
    la, lb = cls_map(a), cls_map(b)
    for k in set(la) | set(lb):
        d = round(lb.get(k, 0) - la.get(k, 0), 2)
        if abs(d) >= 0.5:
            out["class_deltas_mb"].append({"class": k, "base": la.get(k, 0), "current": lb.get(k, 0), "delta": d})
    out["class_deltas_mb"].sort(key=lambda x: abs(x["delta"]), reverse=True)
    out["gpu_category_deltas_mb"] = out["gpu_category_deltas_mb"][:top]
    out["class_deltas_mb"] = out["class_deltas_mb"][:top]
    return out


# --------------------------------------------------------------------------- #
# CSV profiler (per-frame timing)
# --------------------------------------------------------------------------- #
def analyze_csv_profile_file(path, hitch_ms=33.3, top=20):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = [ln.rstrip("\n") for ln in f]
    # data ends where the metadata section begins (a line starting with '[' )
    headers, rows, meta = None, [], {}
    for ln in lines:
        if headers is None:
            headers = [h.strip() for h in ln.split(",")]
            continue
        if ln.startswith("[") or not ln.strip():
            m = re.match(r"\[(\w+)\],?(.*)", ln)
            if m:
                meta[m.group(1)] = m.group(2).strip().strip(",")
            continue
        rows.append(ln.split(","))
    if not headers:
        return {"file": path, "error": "no header row"}

    cols = {h: [] for h in headers}
    for r in rows:
        for i, h in enumerate(headers):
            if i < len(r):
                try:
                    cols[h].append(float(r[i]))
                except ValueError:
                    pass

    def pick(*names):
        for n in names:
            for h in headers:
                if h.lower() == n.lower():
                    return cols[h]
        return []

    frame = pick("FrameTime", "frametime")
    out = {"file": path, "frames": len(rows), "metadata": meta, "frame_time_ms": {}, "fps": {},
           "hitches": {}, "threads_ms": {}, "top_stats_by_avg": []}
    if frame:
        out["frame_time_ms"] = _stats(frame)
        fps = [1000.0 / v for v in frame if v > 0]
        out["fps"] = _stats(fps)
        hitches = [i for i, v in enumerate(frame) if v >= hitch_ms]
        out["hitches"] = {"threshold_ms": hitch_ms, "count": len(hitches),
                          "pct_of_frames": round(100.0 * len(hitches) / len(frame), 2) if frame else 0,
                          "worst_ms": round(max(frame), 2) if frame else 0}
    for label, names in [("game", ("GameThreadTime",)), ("render", ("RenderThreadTime",)),
                         ("rhi", ("RHIThreadTime",)), ("gpu", ("GPU", "GPUTime"))]:
        v = pick(*names)
        if v:
            out["threads_ms"][label] = _stats(v)

    ignore = {"frametime", "gamethreadtime", "renderthreadtime", "rhithreadtime", "gpu", "gputime", "framenumber"}
    ranked = []
    for h in headers:
        if h.lower() in ignore or not cols[h]:
            continue
        avg = sum(cols[h]) / len(cols[h])
        if avg != 0:
            ranked.append({"stat": h, "avg": round(avg, 4), "max": round(max(cols[h]), 4)})
    ranked.sort(key=lambda x: x["avg"], reverse=True)
    out["top_stats_by_avg"] = ranked[:top]
    return out


# --------------------------------------------------------------------------- #
# MCP tools
# --------------------------------------------------------------------------- #
def register_profiling_tools(mcp):
    @mcp.tool()
    def analyze_memreport(ctx, file_path: str, top: int = 15):
        """Summarize an Unreal .memreport (CPU+GPU memory dump). Returns platform memory,
        GPU/RHI categories (Nanite/Lumen/Shadow/VirtualTexture…), top classes by resource
        memory (with dedicated-video = GPU), texture totals + largest textures, and largest
        meshes. Pure file parse — the editor does NOT need to be running."""
        try:
            return analyze_memreport_file(file_path, top)
        except Exception as e:
            return {"success": False, "message": f"Error analyzing memreport: {e}"}

    @mcp.tool()
    def compare_memreports(ctx, base_path: str, current_path: str, top: int = 15):
        """Diff two .memreport captures — platform-memory deltas, GPU category deltas, and
        per-class resource-memory deltas (sorted by magnitude). For catching memory
        regressions between two builds/states."""
        try:
            return diff_memreports(base_path, current_path, top)
        except Exception as e:
            return {"success": False, "message": f"Error comparing memreports: {e}"}

    @mcp.tool()
    def analyze_csv_profile(ctx, file_path: str, hitch_ms: float = 33.3, top: int = 20):
        """Summarize an FCsvProfiler .csv (per-frame CPU/GPU timing): frame-time and FPS
        stats with percentiles, hitch count above hitch_ms, per-thread times (game/render/
        rhi/gpu), and the most expensive stats by average. Pure file parse."""
        try:
            return analyze_csv_profile_file(file_path, hitch_ms, top)
        except Exception as e:
            return {"success": False, "message": f"Error analyzing csv profile: {e}"}

    @mcp.tool()
    def analyze_trace(ctx, trace_path: str, top: int = 25):
        """Summarize an Unreal Insights .utrace via TraceServices in the editor: frame stats
        (game/render FPS + percentiles + hitches) and the top CPU and GPU timers by total
        time. Requires the editor running (this forwards to the C++ analyze_trace command)."""
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("analyze_trace", {"trace_path": trace_path, "top": top})
            return response or {"success": False, "message": "No response from Unreal Engine"}
        except Exception as e:
            return {"success": False, "message": f"Error analyzing trace: {e}"}

    def _bridge(command, params):
        from unreal_mcp_server import get_unreal_connection
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        return unreal.send_command(command, params) or {"success": False, "message": "No response from Unreal Engine"}

    @mcp.tool()
    def capture_memreport(ctx, full: bool = True, timeout_s: float = 30.0):
        """Capture a fresh .memreport in the running editor and return the generated file's
        path — feed it straight to analyze_memreport. The `memreport` command writes
        deferred (a later editor tick), so this triggers it then polls the filesystem for
        the new file. Requires the editor running."""
        import os
        import time
        try:
            t0 = time.time() - 2.0
            r = _bridge("capture_memreport", {"full": full})
            d = (r.get("result") or {}).get("data") or r
            dpath = d.get("dir")
            if not dpath:
                return r

            def scan():
                res = []
                for root, _, files in os.walk(dpath):
                    for f in files:
                        if f.lower().endswith(".memreport"):
                            p = os.path.join(root, f)
                            try:
                                res.append((p, os.path.getmtime(p)))
                            except OSError:
                                pass
                return res

            deadline = time.time() + timeout_s
            while time.time() < deadline:
                time.sleep(1.0)
                fresh = [(p, m) for p, m in scan() if m >= t0]
                if fresh:
                    cand = max(fresh, key=lambda x: x[1])[0]
                    sz = os.path.getsize(cand)
                    time.sleep(0.8)  # let the deferred write finish
                    if os.path.getsize(cand) == sz:
                        return {"success": True, "captured": True, "path": cand, "dir": dpath}
            return {"success": False, "captured": False, "dir": dpath,
                    "message": f"memreport triggered but no new file within {timeout_s}s"}
        except Exception as e:
            return {"success": False, "message": f"Error capturing memreport: {e}"}

    @mcp.tool()
    def start_trace(ctx, channels: str = "cpu,gpu,frame,counters,stats", path: str = ""):
        """Start an Unreal Insights .utrace capture in the running editor (FTraceAuxiliary).
        Let the editor run/render for a while, then stop_trace, then analyze_trace. Returns
        the trace file path. Requires the editor running."""
        try:
            return _bridge("start_trace", {"channels": channels, "path": path})
        except Exception as e:
            return {"success": False, "message": f"Error starting trace: {e}"}

    @mcp.tool()
    def stop_trace(ctx):
        """Stop the in-progress .utrace capture and finalize the file. Returns its path —
        then call analyze_trace on it. Requires the editor running."""
        try:
            return _bridge("stop_trace", {})
        except Exception as e:
            return {"success": False, "message": f"Error stopping trace: {e}"}

    logger.info("Profiling tools registered successfully")


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def _main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("memreport"); m.add_argument("file"); m.add_argument("--top", type=int, default=15); m.add_argument("--json", action="store_true")
    d = sub.add_parser("memreport-diff"); d.add_argument("base"); d.add_argument("current"); d.add_argument("--top", type=int, default=15)
    c = sub.add_parser("csv"); c.add_argument("file"); c.add_argument("--hitch-ms", type=float, default=33.3); c.add_argument("--top", type=int, default=20)
    a = ap.parse_args()
    if a.cmd == "memreport":
        print(json.dumps(analyze_memreport_file(a.file, a.top), indent=2))
    elif a.cmd == "memreport-diff":
        print(json.dumps(diff_memreports(a.base, a.current, a.top), indent=2))
    elif a.cmd == "csv":
        print(json.dumps(analyze_csv_profile_file(a.file, a.hitch_ms, a.top), indent=2))


if __name__ == "__main__":
    _main()
