# Unreal MCP — Profiling analysis & capture

Tools for the profiling data Unreal emits: **memory** (`.memreport`), **per-frame CPU/GPU
timing** (FCsvProfiler `.csv`), and **Insights timeline sessions** (`.utrace`). Exposed as
MCP tools (and a Python CLI). The memreport/CSV analyzers are pure file parsers and work
with the editor closed; trace analysis and all capture need the editor running.

## Analyze (read)

| MCP tool | Input | Output |
|---|---|---|
| `analyze_memreport(file_path)` | `.memreport` | platform memory; GPU/RHI categories (Nanite/Lumen/Shadow/VirtualTexture…); top classes by resource memory (CPU vs dedicated-video GPU); texture totals + largest textures; largest meshes |
| `compare_memreports(base, current)` | two `.memreport` | platform / GPU-category / per-class memory deltas (regression hunting) |
| `analyze_csv_profile(file_path, hitch_ms)` | FCsvProfiler `.csv` | frame-time & FPS percentiles; hitch count; per-thread (game/render/rhi/gpu); most expensive stats |
| `analyze_trace(trace_path, top)` | `.utrace` | frame stats (game/render, percentiles, fps); top CPU & GPU timers by total time. Uses TraceServices in the editor. |

CLI (no editor): `python Python/tools/profiling_tools.py memreport <f> [--top N]` ·
`memreport-diff <a> <b>` · `csv <f> [--hitch-ms 33.3]`.

## Capture (then analyze)

| MCP tool | Effect |
|---|---|
| `capture_memreport(full)` | runs the `memreport` console command and returns the new `.memreport` path. The command writes *deferred* (a later editor tick), so the tool triggers it then polls the filesystem for the file. → `analyze_memreport`. |
| `start_trace(channels, path)` | begins a `.utrace` capture (`FTraceAuxiliary`). Default channels `cpu,gpu,frame,counters,stats`. |
| `stop_trace()` | finalizes the `.utrace`, returns its path. → `analyze_trace`. |

Typical loop: `start_trace` → let the editor run/render a while → `stop_trace` → `analyze_trace(path)`.

## Implementation

- `Python/tools/profiling_tools.py` — memreport + CSV parsers (pure Python), MCP tool
  registration, and the capture-poll orchestration. Registered in `unreal_mcp_server.py`.
- `analyze_trace` / `capture_memreport` / `start_trace` / `stop_trace` are C++ bridge
  commands in the plugin (`UnrealMCPBlueprintCommands`), using `TraceServices`
  (`IAnalysisService::Analyze` + frame/timing providers) and `FTraceAuxiliary`. The plugin
  links `TraceServices` + `TraceLog`.

## Notes

- A large `.utrace` takes a while to analyze (`Analyze` is synchronous on the game thread;
  ~14 s for a 131 MB trace) — clients should use a generous timeout.
- `analyze_trace` filters the trailing incomplete frame (open at trace end → non-finite
  duration) and guards non-finite timer values so the JSON stays valid.
- `.utrace` *deep* inspection (per-call butterfly, memory/allocations tags, regions) is a
  future extension — the same TraceServices providers (`AllocationsProvider`, `Counters`,
  `Regions`) are available.
