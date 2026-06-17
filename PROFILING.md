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
| `analyze_trace(trace_path, top, sections)` | `.utrace` | **full TraceServices coverage**: frame stats (game/render, percentiles, fps); top CPU & GPU timers; **counters** (NumDraws/NumPrimitives/STAT_* min/max/avg/last); **memory_llm** (LLM tracker tags by peak MB — needs `-llm`); **regions** & **bookmarks** (named timeline markers, e.g. PIE/GC); **loadtime_events** (async package-load aggregation). `sections` selects which to compute: `"all"` (default) or a comma list of `counters,memory,regions,bookmarks,loadtime` (frames + timers are always included). Uses TraceServices in the editor. |

`sections` lets an agent skip the heavier passes — e.g. `sections="frames"` for a quick frame check, or `sections="memory,counters"` for a memory sweep.

CLI (no editor): `python Python/tools/profiling_tools.py memreport <f> [--top N]` ·
`memreport-diff <a> <b>` · `csv <f> [--hitch-ms 33.3]`.

## Capture (then analyze)

| MCP tool | Effect |
|---|---|
| `capture_memreport(full)` | runs the `memreport` console command and returns the new `.memreport` path. The command writes *deferred* (a later editor tick), so the tool triggers it then polls the filesystem for the file. → `analyze_memreport`. |
| `start_trace(channels, path)` | begins a `.utrace` capture (`FTraceAuxiliary`). Default channels `cpu,gpu,frame,counters,stats`. |
| `stop_trace()` | finalizes the `.utrace`, returns its path. → `analyze_trace`. |

Typical loop: `start_trace` → let the editor run/render a while → `stop_trace` → `analyze_trace(path)`.

**To populate the heavier sections** (a runtime `start_trace` is not enough for these):
- **`memory_llm`** needs the LLM tracker active — launch the editor with **`-llm`** (LLM tag samples are only emitted when LLM is on). With it, you get a Default-tracker breakdown (Total / WorkingSetSize / AssetRegistry / Textures / Untracked / FMallocUnused … by peak MB) — ideal for memory-regression hunting.
- **`loadtime_events`** needs the **`loadtime`** trace channel *and* async package loads to occur inside the capture window. Easiest is a **startup trace**: launch with
  `-trace=cpu,gpu,frame,counters,stats,memtag,loadtime,assetloadtime -tracefile=<path>` (add `-llm` for memory too), let it load, then `stop_trace`. (Note: editor traces using the Zen/EDL loader may still emit no LoadTimeProfiler events — the section then returns an empty array, not an error.)
- **`counters` / `regions` / `bookmarks`** populate from a normal runtime `start_trace` (regions/bookmarks need PIE or code that emits `TRACE_BOOKMARK`/region markers).

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
- Providers with their own read lock (Regions, Memory) are accessed inside a
  `FProviderReadScopeLock` / `BeginRead`-`EndRead` scope — reading them under only the
  session read scope asserts (`Trying to READ from provider outside of a READ scope`).
- Still a future extension: `AllocationsProvider` (per-callstack alloc churn / leak hunting)
  and per-call butterfly aggregation. The frame/timer/counter/memory/region/bookmark/loadtime
  providers are all wired now.
