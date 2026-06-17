import socket, json, sys, time

HOST, PORT = "127.0.0.1", 55557

def send(command, params, timeout=180):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((HOST, PORT))
    s.sendall(json.dumps({"type": command, "params": params or {}}).encode("utf-8"))
    chunks = []
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        chunks.append(chunk)
        try:
            return json.loads(b"".join(chunks).decode("utf-8"))
        except json.JSONDecodeError:
            continue
    return json.loads(b"".join(chunks).decode("utf-8"))

if __name__ == "__main__":
    trace = sys.argv[1] if len(sys.argv) > 1 else r"D:/Inferno/Project/Saved/Profiling/20251023_104742.utrace"
    sections = sys.argv[2] if len(sys.argv) > 2 else "all"
    t0 = time.time()
    resp = send("analyze_trace", {"trace_path": trace, "sections": sections, "top": 15}, timeout=300)
    dt = time.time() - t0
    print(f"=== analyze_trace (sections={sections}) in {dt:.1f}s ===")
    # Summarize keys present and counts, then dump
    data = resp.get("result") or resp.get("data") or resp
    if isinstance(data, dict):
        for k, v in data.items():
            if isinstance(v, list):
                print(f"  {k}: list[{len(v)}]")
            elif isinstance(v, dict):
                print(f"  {k}: object({len(v)} keys)")
            else:
                print(f"  {k}: {v}")
    out = r"D:/UnrealMCP/Python/scripts/_trace_result.json"
    with open(out, "w") as f:
        json.dump(resp, f, indent=2)
    print(f"full JSON -> {out}")
