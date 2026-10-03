"""한글 모아모아 배치 헬퍼 (터미널).

매 턴 게임에 나온 블록 3개의 번호(1~19, 기존 헬퍼 화면 순서)를 입력하면
놓을 순서·모양(회전/반전 반영)·위치를 판 위에 A→B→C로 표시하고, 판 상태를 자동으로 갱신한다.
"""
import ctypes, json, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
W, H, FULL, NP = 10, 16, 0x3FF, 19
SAVE = os.path.join(HERE, "board_state.json")
# {B1, B2, K, K2, S, iB1, iB2, iK}: 빔 폭 / 최종 후보 / lookahead 후보·샘플 수 / 내부 빔
CFG = [64, 64, 256, 64, 128, 8, 8, 32]
DEATH = -2000.0

L = ctypes.CDLL(os.path.join(HERE, os.environ.get("MOAMOA_LIB", "libengine.so")))
if hasattr(L, "mm_set_threads"):  # lookahead 병렬 계산 (환경변수 MOAMOA_THREADS로 조절)
    L.mm_set_threads(int(os.environ.get("MOAMOA_THREADS", os.cpu_count() or 1)))
_w = json.load(open(os.path.join(HERE, "weights.json")))["w"]
WEIGHTS = np.zeros(13); WEIGHTS[:len(_w)] = _w  # 엔진 특징 13개(예전 파일은 0으로 채움)
if os.path.exists(os.path.join(HERE, "dist.json")) and hasattr(L, "mm_set_dist"):  # 단계별 블록 분포(추정치)
    _P = np.ascontiguousarray(json.load(open(os.path.join(HERE, "dist.json"))), dtype=np.float64)
    L.mm_set_dist(_P.ctypes.data_as(ctypes.c_void_p))
COLORS = ["\033[95m", "\033[96m", "\033[93m"]  # A, B, C
RST, DIM = "\033[0m", "\033[90m"


def orient(p, q):
    m = (ctypes.c_uint16 * 5)()
    h = L.mm_orient(p, q, m)
    return [m[i] for i in range(h)]


def shape_lines(p, q=0):
    rows = orient(p, q)
    w = max(r.bit_length() for r in rows)
    return ["".join("■" if r >> j & 1 else " " for j in range(w)) for r in rows]


def show_catalog():
    blocks = [(i + 1, L.mm_cells(i), shape_lines(i)) for i in range(NP)]
    for start in range(0, NP, 7):
        group = blocks[start:start + 7]
        hgt = max(len(s) for _, _, s in group)
        print("   ".join(f"{n:>2}({c:>2}칸)".ljust(8) for n, c, _ in group))
        for i in range(hgt):
            print("   ".join((s[i] if i < len(s) else "").ljust(8) for _, _, s in group))
        print()


def draw(board, marks=None):
    marks = marks or {}
    print("    " + " ".join(str(c) for c in range(W)))
    for r in range(H):
        cells = []
        for c in range(W):
            if (r, c) in marks:
                k = marks[(r, c)]
                cells.append(f"{COLORS[k]}{'ABC'[k]}{RST}")
            elif board[r] >> c & 1:
                cells.append("■")
            else:
                cells.append(f"{DIM}·{RST}")
        print(f"{r:>3} " + " ".join(cells))


def apply(board, p, q, r, c):
    board = list(board)
    lines = 0
    for i, m in enumerate(orient(p, q)):
        board[r + i] |= m << c
        if board[r + i] == FULL:
            board[r + i] = 0
            lines += 1
    return board, lines


def solve(board, pieces, base_lines=0):
    arr = (ctypes.c_uint16 * H)(*board)
    mv = (ctypes.c_int * 12)()
    sc = ctypes.c_double()
    d = L.mm_search(arr, (ctypes.c_int * 3)(*pieces), len(pieces), WEIGHTS.ctypes.data_as(ctypes.c_void_p),
                    (ctypes.c_int * 8)(*CFG), ctypes.c_double(DEATH), ctypes.c_uint64(12345), ctypes.c_int(base_lines), mv, ctypes.byref(sc))
    return d, [tuple(mv[i * 4:i * 4 + 4]) for i in range(d)]


def edit_board():
    print(f"판을 위에서부터 {H}줄 입력하세요 (# = 찬 칸, . = 빈 칸, 각 {W}글자). 빈 줄 입력 시 취소.")
    rows = []
    while len(rows) < H:
        s = input(f"{len(rows):>2}> ").strip().replace(" ", "")
        if not s:
            return None
        if len(s) != W or set(s) - set("#.10"):
            print(f"  {W}글자의 #/. 로 입력하세요")
            continue
        rows.append(sum(1 << j for j, ch in enumerate(s) if ch in "#1"))
    return rows


def main():
    board = json.load(open(SAVE))["board"] if os.path.exists(SAVE) else [0] * H
    history, total_lines, turn = [], 0, 0
    help_msg = ("블록 번호 1~3개 입력(예: 3 12 19) · 1: 1칸 아이템 추천 · t 행 열 [행 열 ...]: 칸 채우기/비우기\n"
                "p: 블록 번호표 · u: 되돌리기 · e: 판 직접 입력 · r: 판 초기화 · q: 종료")
    show_catalog()
    print(help_msg)
    while True:
        print()
        draw(board)
        try:
            s = input(f"\n[턴 {turn + 1} · 지운 줄 {total_lines}] > ").strip().lower()
        except EOFError:
            break
        if s in ("q", "quit", "exit"):
            break
        if s == "p":
            show_catalog(); continue
        if s == "u":
            if history:
                board, total_lines, turn = history.pop()
            else:
                print("되돌릴 수 없습니다")
            continue
        if s == "r":
            history.append((board, total_lines, turn)); board, total_lines, turn = [0] * H, 0, 0; continue
        if s.startswith("t ") or s == "t":
            try:
                v = [int(x) for x in s[1:].replace(",", " ").split()]
                assert v and len(v) % 2 == 0
                cells = list(zip(v[::2], v[1::2]))
                assert all(0 <= r < H and 0 <= c < W for r, c in cells)
            except (ValueError, AssertionError):
                print(f"사용법: t 행 열 [행 열 ...]  (행 0~{H - 1}, 열 0~{W - 1}). 예: t 3 5 3 6"); continue
            history.append((board, total_lines, turn))
            board = list(board)
            for r, c in cells:
                board[r] ^= 1 << c
            json.dump({"board": board}, open(SAVE, "w"))
            continue
        if s == "1":
            s = "item"
        if s == "e":
            nb = edit_board()
            if nb:
                history.append((board, total_lines, turn)); board = nb
            continue
        if s == "item":
            pieces = [0]  # 1칸 아이템 = 1칸 블록과 동일
        else:
            try:
                nums = [int(x) for x in s.replace(",", " ").split()]
                assert 1 <= len(nums) <= 3 and all(1 <= n <= NP for n in nums)
            except (ValueError, AssertionError):
                print(help_msg); continue
            pieces = [n - 1 for n in nums]
        d, moves = solve(board, pieces, total_lines)
        if d < len(pieces):
            print(f"\n⚠ {len(pieces)}개를 모두 놓을 방법이 없습니다 (최대 {d}개). 게임 오버 상황입니다.")
            if d == 0:
                continue
        # 판에 A/B/C 표시 (중간에 지워지는 줄도 있으므로 놓기 전 상태 기준으로 표시)
        marks, b, gained = {}, board, 0
        print()
        for k, (slot, q, r, c) in enumerate(moves):
            p = pieces[slot]
            for i, m in enumerate(orient(p, q)):
                for j in range(W):
                    if (m << c) >> j & 1:
                        marks[(r + i, j)] = k
            b, ln = apply(b, p, q, r, c)
            gained += ln
            print(f"  {COLORS[k]}{'ABC'[k]}{RST}: {p + 1}번 블록 ({L.mm_cells(p)}칸)"
                  f"{'  → ' + str(ln) + '줄 삭제' if ln else ''}")
            for line in shape_lines(p, q):
                print(f"       {COLORS[k]}{line}{RST}")
        print()
        draw(board, marks)
        print(f"\n  놓는 순서: {' → '.join('ABC'[:len(moves)])}   ({gained}줄 삭제)")
        history.append((board, total_lines, turn))
        board, total_lines, turn = b, total_lines + gained, turn + (len(pieces) == 3)
        json.dump({"board": board}, open(SAVE, "w"))
    json.dump({"board": board}, open(SAVE, "w"))


if __name__ == "__main__":
    main()
