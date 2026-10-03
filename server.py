"""한글 모아모아 헬퍼 웹 서버 (표준 라이브러리만 사용).

    python3 server.py [--port 8765] [--host 127.0.0.1]

원격 서버라면 VSCode 포트 포워딩 또는 `ssh -L 8765:localhost:8765 서버`로 접속.
"""
import argparse, json, os, time, threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

from helper import HERE, H, W, NP, L, orient, apply, solve


def piece_info():
    out = []
    for p in range(NP):
        rows = orient(p, 0)
        w = max(r.bit_length() for r in rows)
        out.append({"id": p, "cells": L.mm_cells(p), "shape": [[r >> j & 1 for j in range(w)] for r in rows]})
    return out


def do_solve(board, pieces, base_lines=0):
    d, moves = solve(board, pieces, base_lines)
    steps, b, total = [], board, 0
    for slot, q, r, c in moves:
        p = pieces[slot]
        rows = orient(p, q)
        w = max(m.bit_length() for m in rows)
        cells = [[r + i, j] for i, m in enumerate(rows) for j in range(W) if (m << c) >> j & 1]
        b, ln = apply(b, p, q, r, c)
        total += ln
        steps.append({"piece": p, "cells": cells, "lines": ln,
                      "shape": [[m >> j & 1 for j in range(w)] for m in rows]})
    return {"placed": d, "need": len(pieces), "steps": steps, "board_after": b, "lines": total}


PIECES = piece_info()
INDEX = os.path.join(HERE, "index.html")
LOG = os.path.join(HERE, "pieces_log.jsonl")  # 실제 블록 분포 추정용 기록
_log_lock = threading.Lock()


def log_pieces(req, pieces, base_lines):
    if len(pieces) != 3 or not req.get("game"):
        return
    rec = {"t": round(time.time()), "game": str(req["game"])[:32], "turn": int(req.get("turn", -1)),
           "lines": base_lines, "pieces": [p + 1 for p in pieces]}
    with _log_lock, open(LOG, "a") as f:
        f.write(json.dumps(rec) + "\n")


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._send(200, open(INDEX, "rb").read(), "text/html; charset=utf-8")
        elif self.path == "/api/pieces":
            self._send(200, PIECES)
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/api/solve":
            return self._send(404, {"error": "not found"})
        try:
            req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
            board = [int(x) & 0x3FF for x in req["board"]]
            pieces = [int(x) for x in req["pieces"]]
            base_lines = max(0, int(req.get("total_lines", 0)))
            assert len(board) == H and 1 <= len(pieces) <= 3 and all(0 <= p < NP for p in pieces)
        except Exception as e:
            return self._send(400, {"error": f"잘못된 요청: {e}"})
        log_pieces(req, pieces, base_lines)
        self._send(200, do_solve(board, pieces, base_lines))

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    a = ap.parse_args()
    print(f"헬퍼 실행 중: http://localhost:{a.port}  (종료: Ctrl+C)", flush=True)
    ThreadingHTTPServer((a.host, a.port), Handler).serve_forever()
