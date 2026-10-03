"""index.html(서버용 화면) + engine.js + local.js → 서버 없이 열리는 단일 파일 moamoa.html."""
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def main():
    html = open(os.path.join(ROOT, "index.html"), encoding="utf-8").read()
    engine = open(os.path.join(HERE, "engine.js"), encoding="utf-8").read()
    local = open(os.path.join(HERE, "local.js"), encoding="utf-8").read()
    w = json.load(open(os.path.join(ROOT, "weights.json")))["w"]
    w = (w + [0] * 13)[:13]
    dist = json.load(open(os.path.join(ROOT, "dist_emp.json")))
    consts = f"const MOA_W = {json.dumps(w)};\nconst MOA_DIST = {json.dumps([[round(x, 5) for x in p] for p in dist])};\n"

    rep = [
        ("PIECES = await (await fetch('/api/pieces')).json();",
         "PIECES = localPieces();\n  moaStartWorkers().then(n => { $('cores').textContent = n ? `${n}코어 병렬 계산` : '단일 스레드 계산'; });"),
        ("""    const res = await fetch('/api/solve', { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ board, pieces, total_lines: lines, game, turn }) });
    const data = await res.json();
    if (id !== reqId) return;
    if (!res.ok) throw new Error(data.error);""",
         """    const data = await localSolve(board.slice(), pieces, lines);
    if (id !== reqId) return;"""),
        ("<script>\nconst W = 10, H = 16, FULL = 0x3FF;",
         f'<script id="engine-src">\n{engine}</script>\n<script>\n{consts}{local}</script>\n<script>\nconst W = 10, H = 16, FULL = 0x3FF;'),
        ("""<h2>판 <small>""", """<h2>판 <small><span id="cores" style="margin-right:8px"></span>"""),
    ]
    for a, b in rep:
        assert a in html, a[:60]
        html = html.replace(a, b)
    out = os.path.join(ROOT, "moamoa.html")
    open(out, "w", encoding="utf-8").write(html)
    print("wrote", out, f"{len(html) / 1024:.0f} KB")


if __name__ == "__main__":
    main()
