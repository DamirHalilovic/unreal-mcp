import socket, json, time

HOST, PORT = "127.0.0.1", 55557

def send(command, params, timeout=120):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((HOST, PORT))
    s.sendall(json.dumps({"type": command, "params": params or {}}).encode("utf-8"))
    chunks = []
    while True:
        chunk = s.recv(65536)
        if not chunk: break
        chunks.append(chunk)
        try:
            return json.loads(b"".join(chunks).decode("utf-8"))
        except json.JSONDecodeError:
            continue
    return json.loads(b"".join(chunks).decode("utf-8"))

path = r"D:/Inferno/Project/Saved/Profiling/mcp_memload.utrace"
chans = "cpu,gpu,frame,counters,stats,memtag,loadtimetrace,assetloadtime,object,assetregistry"
print("start:", json.dumps(send("start_trace", {"channels": chans, "path": path})))
# Let the editor tick & emit memory/load samples
time.sleep(20)
# A memreport forces a GC + memory snapshot which populates memory tags
print("memreport:", json.dumps(send("capture_memreport", {"full": True}, timeout=120)).get if False else json.dumps(send("capture_memreport", {"full": True}, timeout=120)))
time.sleep(8)
print("stop:", json.dumps(send("stop_trace", {})))
time.sleep(2)
res = send("analyze_trace", {"trace_path": path, "sections": "memory,loadtime,counters", "top": 15}, timeout=300)
with open(r"D:/UnrealMCP/Python/scripts/_memload_result.json", "w") as f:
    json.dump(res, f, indent=2)
data = res.get("result", {}).get("data", res)
mem = data.get("memory_llm")
print("memory_llm:", json.dumps(mem)[:400] if mem else mem)
lt = data.get("loadtime_events")
print("loadtime_events count:", len(lt) if isinstance(lt, list) else lt)
if isinstance(lt, list):
    for e in lt[:5]: print("   ", json.dumps(e))
