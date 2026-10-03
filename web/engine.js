// 한글 모아모아 엔진 (engine.c의 JavaScript 이식판). 브라우저 메인 스레드·Web Worker·node 공용.
// 10x16 판, 가로줄만 삭제(중력 없음), 회전/반전 허용. 점수: 블록 하나로 n줄 동시 삭제 = 300*n^2.
const MoaEngine = (() => {
  const W = 10, H = 16, FULL = 0x3FF, NP = 19, NF = 13;
  const SHAPES = [
    "#", "#.|.#|#.", "#|#|#", "#.|##", "#..|###", "#.|##|#.", ".#.|#.#|.#.", "###|#.#", "#####",
    "#.#|##.|#.#", ".#|##|.#|##", "#####|.#.#.", "#.#|.#.|###|.#.", "###|#.#|###", "###.#|#.###",
    "#.#.#|#####", "..#..|#####|.#.#.|..#..", "####|#.#.|####", "####|.##.|####",
  ];
  const POP = new Uint8Array(2048);
  for (let i = 1; i < 2048; i++) POP[i] = POP[i >> 1] + (i & 1);

  // 회전 4 x 반전 2 중 서로 다른 방향만
  const ORI = [], NCELL = [];
  for (const s of SHAPES) {
    const cells = [];
    s.split("|").forEach((row, i) => [...row].forEach((ch, j) => { if (ch === "#") cells.push([i, j]); }));
    NCELL.push(cells.length);
    const list = [];
    for (let t = 0; t < 8; t++) {
      const pts = cells.map(([x, y]) => {
        for (let r = 0; r < (t & 3); r++) { const tmp = x; x = y; y = -tmp; }
        if (t & 4) y = -y;
        return [x, y];
      });
      const mi = Math.min(...pts.map(p => p[0])), mj = Math.min(...pts.map(p => p[1]));
      const o = { h: 0, w: 0, n: pts.length, m: [0, 0, 0, 0, 0], ci: [], cj: [] };
      for (const [x, y] of pts) {
        const i = x - mi, j = y - mj;
        o.ci.push(i); o.cj.push(j); o.m[i] |= 1 << j;
        o.h = Math.max(o.h, i + 1); o.w = Math.max(o.w, j + 1);
      }
      o.m = o.m.slice(0, o.h);
      if (!list.some(q => q.h === o.h && q.w === o.w && q.m.every((v, k) => v === o.m[k]))) list.push(o);
    }
    ORI.push(list);
  }

  const ctz = x => 31 - Math.clz32(x & -x);

  function fitCols(b, off, o, r) {
    let acc = (1 << (W - o.w + 1)) - 1;
    for (let k = 0; k < o.n && acc; k++) acc &= ((~b[off + r + o.ci[k]]) & FULL) >> o.cj[k];
    return acc;
  }

  function place(b, off, o, r, c) {
    let lines = 0;
    for (let i = 0; i < o.h; i++) {
      const v = b[off + r + i] | (o.m[i] << c);
      if (v === FULL) { b[off + r + i] = 0; lines++; } else b[off + r + i] = v;
    }
    return lines;
  }

  function countFits(b, off, p) {
    let cnt = 0;
    for (const o of ORI[p]) for (let r = 0; r + o.h <= H; r++) cnt += POP[fitCols(b, off, o, r)];
    return cnt;
  }

  function evaluate(b, off, full, lines, pts, w) {
    let cells = 0, rtr = 0, ctr = 0, iso = 0, empty = 0, partial = 0, sq = 0, wells = 0;
    for (let r = 0; r < H; r++) {
      const x = b[off + r], c = POP[x];
      cells += c; sq += c * c;
      if (c === 0) empty++; else partial++;
      const y = (x << 1) | 1 | (1 << (W + 1));
      rtr += POP[(y ^ (y >> 1)) & 0x7FF];
      const up = r > 0 ? b[off + r - 1] : FULL, dn = r < H - 1 ? b[off + r + 1] : FULL;
      if (r < H - 1) ctr += POP[(x ^ dn) & FULL];
      if (r === 0 || r === H - 1) ctr += POP[~x & FULL];
      const e = ~x & FULL, L = (x << 1) | 1, R = (x >> 1) | (1 << (W - 1));
      iso += POP[e & L & R & up & dn];
      wells += POP[e & L & R & ~(up & dn) & FULL];
    }
    let s = w[0] * cells + w[1] * rtr + w[2] * ctr + w[3] * iso + w[4] * empty + w[5] * partial +
            w[6] * (sq / 10) + w[7] * wells + w[11] * lines + w[12] * pts;
    if (full) {
      let dead = 0, inv = 0, lg = 0;
      for (let p = 0; p < NP; p++) {
        const c = countFits(b, off, p);
        if (!c) dead++;
        inv += 1 / (1 + c);
        lg += Math.log1p(c);
      }
      s += w[8] * dead + w[9] * inv + w[10] * lg;
    }
    return s;
  }

  function bkey(b, off, used) {
    let h1 = 0x811c9dc5 ^ used, h2 = 0x01000193 + used;
    for (let i = 0; i < H; i++) {
      h1 = Math.imul(h1 ^ b[off + i], 0x01000193);
      h2 = Math.imul(h2 + b[off + i], 0x5bd1e995) ^ (h2 >>> 15);
    }
    return (h1 >>> 0) * 2097152 + ((h2 >>> 0) & 0x1FFFFF);
  }

  // ---- 탐색 (재사용 버퍼; 한 스레드 안에서 재진입하지 않음) ----
  class Level {
    constructor() { this.cap = 0; this.cnt = 0; }
    ensure(n) {
      if (n <= this.cap) return;
      this.cap = n;
      this.b = new Uint16Array(n * H); this.lines = new Int32Array(n); this.pts = new Int32Array(n);
      this.used = new Uint8Array(n); this.parent = new Int32Array(n); this.mv = new Int8Array(n * 4);
      this.score = new Float64Array(n);
    }
  }
  const LV = [new Level(), new Level(), new Level(), new Level()];
  let cCap = 0, cScore, cPar, cMv, cIdx;
  function ensureCand(n) {
    if (n <= cCap) return;
    const nc = Math.max(n, cCap * 2, 4096);
    const s = new Float32Array(nc), p = new Int32Array(nc), m = new Int8Array(nc * 4);
    if (cCap) { s.set(cScore); p.set(cPar); m.set(cMv); }
    cScore = s; cPar = p; cMv = m; cIdx = new Int32Array(nc); cCap = nc;
  }
  const tmp = new Uint16Array(H);

  function topSelect(idx, n, m) {  // 점수 상위 m개를 앞쪽으로 (Hoare quickselect)
    let lo = 0, hi = n - 1;
    const k = m - 1;
    while (lo < hi) {
      const piv = cScore[idx[(lo + hi) >> 1]];
      let i = lo, j = hi;
      while (i <= j) {
        while (cScore[idx[i]] > piv) i++;
        while (cScore[idx[j]] < piv) j--;
        if (i <= j) { const t = idx[i]; idx[i] = idx[j]; idx[j] = t; i++; j--; }
      }
      if (k <= j) hi = j; else if (k >= i) lo = i; else break;
    }
  }

  function selectTop(n, keep, par, out, pieces) {
    let M = keep * 2 + 8;
    out.ensure(keep);
    for (;;) {
      if (M > n) M = n;
      for (let i = 0; i < n; i++) cIdx[i] = i;
      topSelect(cIdx, n, M);
      const top = Array.from(cIdx.subarray(0, M)).sort((a, b) => cScore[b] - cScore[a]);
      const seen = new Set();
      let m = 0;
      for (let t = 0; t < M && m < keep; t++) {
        const i = top[t], pi = cPar[i], s = cMv[i * 4], q = cMv[i * 4 + 1], r = cMv[i * 4 + 2], c = cMv[i * 4 + 3];
        const ob = m * H;
        out.b.set(par.b.subarray(pi * H, pi * H + H), ob);
        const l = place(out.b, ob, ORI[pieces[s]][q], r, c);
        const used = par.used[pi] | (1 << s);
        const key = bkey(out.b, ob, used);
        if (seen.has(key)) continue;
        seen.add(key);
        out.lines[m] = par.lines[pi] + l; out.pts[m] = par.pts[pi] + l * l;
        out.used[m] = used; out.parent[m] = pi; out.score[m] = cScore[i];
        out.mv[m * 4] = s; out.mv[m * 4 + 1] = q; out.mv[m * 4 + 2] = r; out.mv[m * 4 + 3] = c;
        m++;
      }
      if (m >= keep || M >= n) { out.cnt = m; return m; }
      M *= 4;
    }
  }

  // pieces[0..np-1]을 모두 놓는 빔 탐색. 최종 후보는 정밀 평가.
  // 반환 { depth, nFin } — 최종 후보는 LV[depth]에 있음(score = 정밀 평가값).
  function searchCore(start, pieces, np, w, B1, B2, K) {
    const keeps = [1, B1, B2, K]; keeps[np] = K;
    const L0 = LV[0]; L0.ensure(1);
    L0.b.set(start, 0); L0.lines[0] = 0; L0.pts[0] = 0; L0.used[0] = 0; L0.parent[0] = -1; L0.cnt = 1;
    let depth = 0;
    for (let d = 1; d <= np; d++) {
      const P = LV[d - 1];
      let n = 0;
      for (let i = 0; i < P.cnt; i++) {
        const used = P.used[i], off = i * H;
        for (let s = 0; s < np; s++) {
          if (used & (1 << s)) continue;
          let skip = false;  // 같은 종류 블록이면 앞 슬롯만 사용
          for (let t = 0; t < s; t++) if (!(used & (1 << t)) && pieces[t] === pieces[s]) skip = true;
          if (skip) continue;
          const os = ORI[pieces[s]];
          for (let q = 0; q < os.length; q++) {
            const o = os[q];
            for (let r = 0; r + o.h <= H; r++) {
              let cols = fitCols(P.b, off, o, r);
              while (cols) {
                const c = ctz(cols); cols &= cols - 1;
                for (let k = 0; k < H; k++) tmp[k] = P.b[off + k];
                const l = place(tmp, 0, o, r, c);
                ensureCand(n + 1);
                cScore[n] = evaluate(tmp, 0, false, P.lines[i] + l, P.pts[i] + l * l, w);
                cPar[n] = i; cMv[n * 4] = s; cMv[n * 4 + 1] = q; cMv[n * 4 + 2] = r; cMv[n * 4 + 3] = c;
                n++;
              }
            }
          }
        }
      }
      if (n === 0) break;
      selectTop(n, keeps[d], P, LV[d], pieces);
      depth = d;
    }
    let nFin = 0;
    if (depth === np) {
      const F = LV[np];
      for (let i = 0; i < F.cnt; i++) F.score[i] = evaluate(F.b, i * H, true, F.lines[i], F.pts[i], w);
      nFin = F.cnt;
    }
    return { depth, nFin };
  }

  function tracePath(depth, idx) {
    const moves = [];
    for (let d = depth; d >= 1; d--) {
      const L = LV[d];
      moves.unshift([L.mv[idx * 4], L.mv[idx * 4 + 1], L.mv[idx * 4 + 2], L.mv[idx * 4 + 3]]);
      idx = L.parent[idx];
    }
    return moves;
  }

  // 같은 자리들을 놓는 순서만 바꿔 동시 삭제 점수 최대화(최종 판이 같은 순서만)
  function reorderForPoints(start, pieces, moves) {
    const n = moves.length;
    if (n < 2) return moves;
    const perms = n === 3 ? [[0,1,2],[0,2,1],[1,0,2],[1,2,0],[2,0,1],[2,1,0]] : [[0,1],[1,0]];
    const ref = Uint16Array.from(start);
    for (const [s, q, r, c] of moves) place(ref, 0, ORI[pieces[s]][q], r, c);
    let best = 0, bestPts = -1;
    perms.forEach((pm, k) => {
      const b = Uint16Array.from(start);
      let pts = 0;
      for (const i of pm) {
        const [s, q, r, c] = moves[i], o = ORI[pieces[s]][q];
        if (!((fitCols(b, 0, o, r) >> c) & 1)) return;
        const l = place(b, 0, o, r, c);
        pts += l * l;
      }
      if (b.every((v, i) => v === ref[i]) && pts > bestPts) { bestPts = pts; best = k; }
    });
    return perms[best].map(i => moves[i]);
  }

  // 다음 턴 한 샘플의 가치(내부 빔 탐색 최고 점수, 못 놓으면 death)
  function sampleValue(board, pc, w, cfg) {
    const { depth, nFin } = searchCore(board, pc, 3, w, cfg.iB1, cfg.iB2, cfg.iK);
    if (depth < 3) return cfg.death;
    let best = -Infinity;
    const F = LV[3];
    for (let i = 0; i < nFin; i++) if (F.score[i] > best) best = F.score[i];
    return best;
  }

  const stageOf = l => l < 30 ? 0 : l < 60 ? 1 : l < 100 ? 2 : l < 150 ? 3 : 4;
  function drawU(u, cdf) { for (let p = 0; p < NP - 1; p++) if (u < cdf[p]) return p; return NP - 1; }

  return { W, H, FULL, NP, ORI, NCELL, place, fitCols, evaluate, searchCore, tracePath, reorderForPoints,
           sampleValue, stageOf, drawU, LV };
})();
if (typeof module !== "undefined") module.exports = MoaEngine;
