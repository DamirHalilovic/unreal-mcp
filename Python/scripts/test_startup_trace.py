import socket, json, time

HOST, PORT = "127.0.0.1", 55557

def send(command, params, timeout=300):
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

path = r"D:/Inferno/Project/Saved/Profiling/startup_full.utrace"
# A full memreport forces DumpLLM -> emits LLM tag snapshot into the trace
print("memreport:", json.dumps(send("capture_memreport", {"full": True}, timeout=180)))
time.sleep(8)
print("stop:", json.dumps(send("stop_trace", {})))
time.sleep(3)
res = send("analyze_trace", {"trace_path": path, "sections": "all", "top": 20}, timeout=400)
with open(r"D:/UnrealMCP/Python/scripts/_startup_result.json", "w") as f:
    json.dump(res, f, indent=2)
data = res.get("result", {}).get("data", res)
print("KEYS:", list(data.keys()) if isinstance(data, dict) else data)
m = data.get("memory_llm")
print("memory_llm:", json.dumps(m)[:700] if m else m)
lt = data.get("loadtime_events")
print("loadtime_events:", len(lt) if isinstance(lt, list) else lt)
for e in (lt or [])[:8]:
    print("   ", json.dumps(e))
