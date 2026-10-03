// 서버 없이 브라우저에서 추천 계산: 기본 탐색은 메인 스레드, 다음 턴 lookahead는 Web Worker로 병렬.
// MOA_W(가중치), MOA_DIST(단계별 블록 확률 5x19)는 빌드 시 삽입된다.
const MOA_CDF = MOA_DIST.map(p => { let a = 0; const s = p.reduce((x, y) => x + y, 0); return p.map(v => (a += v / s)); });
const MOA_BASE = { B1: 64, B2: 64, K: 256, iB1: 8, iB2: 8, iK: 32, death: -2000 };
let moaWorkers = [], moaJob = 0, MOA_K2 = 8, MOA_S = 16;

const WORKER_MAIN = `
onmessage = e => {
  const d = e.data;
  if (d.ping) { postMessage({ id: d.id }); return; }
  const sums = d.cands.map(c => {
    const cdf = d.cdfs[MoaEngine.stageOf(d.baseLines + c.lines)];
    const b = Uint16Array.from(c.b);
    let t = 0;
    for (let s = 0; s < d.S; s++) {
      const pc = [0, 1, 2].map(k => MoaEngine.drawU(d.u[s * 3 + k], cdf));
      t += MoaEngine.sampleValue(b, pc, d.w, d.cfg);
    }
    return t;
  });
  postMessage({ id: d.id, sums });
};`;

function moaRun(wk, msg) {
  return new Promise((resolve, reject) => {
    const h = e => { if (e.data.id === msg.id) { wk.removeEventListener('message', h); resolve(e.data); } };
    wk.addEventListener('message', h);
    wk.addEventListener('error', reject, { once: true });
    wk.postMessage(msg);
  });
}

async function moaStartWorkers() {
  const n = Math.max(1, Math.min(8, (navigator.hardwareConcurrency || 4) - 1));
  try {
    const src = document.getElementById('engine-src').textContent + '\n' + WORKER_MAIN;
    const url = URL.createObjectURL(new Blob([src], { type: 'text/javascript' }));
    const ws = Array.from({ length: n }, () => new Worker(url));
    await Promise.race([
      Promise.all(ws.map(wk => moaRun(wk, { id: ++moaJob, ping: 1 }))),
      new Promise((_, rej) => setTimeout(() => rej(new Error('timeout')), 3000)),
    ]);
    moaWorkers = ws;
  } catch (e) {
    moaWorkers = [];  // Worker를 못 쓰면 메인 스레드에서 작은 설정으로 계산
  }
  [MOA_K2, MOA_S] = moaWorkers.length >= 6 ? [32, 64] : moaWorkers.length >= 3 ? [24, 48] : moaWorkers.length ? [16, 32] : [8, 16];
  return moaWorkers.length;
}

async function moaLookahead(cands, u, baseLines) {
  const msgBase = { u, cdfs: MOA_CDF, baseLines, w: MOA_W, cfg: MOA_BASE, S: MOA_S };
  if (!moaWorkers.length) {
    return cands.map(c => {
      const cdf = MOA_CDF[MoaEngine.stageOf(baseLines + c.lines)], b = Uint16Array.from(c.b);
      let t = 0;
      for (let s = 0; s < MOA_S; s++)
        t += MoaEngine.sampleValue(b, [0, 1, 2].map(k => MoaEngine.drawU(u[s * 3 + k], cdf)), MOA_W, MOA_BASE);
      return t;
    });
  }
  const groups = moaWorkers.map(() => []);
  cands.forEach((c, i) => groups[i % moaWorkers.length].push(i));
  const sums = new Array(cands.length);
  await Promise.all(groups.map(async (g, k) => {
    if (!g.length) return;
    const res = await moaRun(moaWorkers[k], { ...msgBase, id: ++moaJob, cands: g.map(i => cands[i]) });
    g.forEach((i, j) => sums[i] = res.sums[j]);
  }));
  return sums;
}

function localPieces() {
  return MoaEngine.ORI.map((os, p) => ({
    id: p, cells: MoaEngine.NCELL[p], shape: os[0].m.map(m => Array.from({ length: os[0].w }, (_, j) => m >> j & 1)),
  }));
}

async function localSolve(board, pieces, baseLines) {
  const E = MoaEngine, np = pieces.length, start = Uint16Array.from(board);
  const { depth, nFin } = E.searchCore(start, pieces, np, MOA_W, MOA_BASE.B1, MOA_BASE.B2, MOA_BASE.K);
  let moves;
  if (depth < np) {
    moves = depth ? E.tracePath(depth, 0) : [];
  } else {
    const F = E.LV[np];
    const order = [...Array(nFin).keys()].sort((a, b) => F.score[b] - F.score[a]);
    const k2 = Math.min(MOA_K2, nFin);
    // 탐색 버퍼는 다음 계산에 재사용되므로 await 전에 후보를 복사해 둔다
    const cands = order.slice(0, Math.max(1, k2)).map(i => ({
      b: Array.from(F.b.subarray(i * E.H, i * E.H + E.H)), lines: F.lines[i], pts: F.pts[i], moves: E.tracePath(np, i),
    }));
    let best = 0;
    if (k2 > 1) {
      const u = Array.from({ length: MOA_S * 3 }, () => Math.random());  // 모든 후보에 같은 샘플
      const sums = await moaLookahead(cands, u, baseLines);
      let bs = -Infinity;
      cands.forEach((c, a) => {
        const v = sums[a] / MOA_S + MOA_W[11] * c.lines + MOA_W[12] * c.pts;
        if (v > bs) { bs = v; best = a; }
      });
    }
    moves = E.reorderForPoints(start, pieces, cands[best].moves);
  }
  const b = board.slice();
  let total = 0;
  const steps = moves.map(([s, q, r, c]) => {
    const p = pieces[s], o = E.ORI[p][q], cells = [];
    for (let i = 0; i < o.h; i++) for (let j = 0; j < E.W; j++) if (((o.m[i] << c) >> j) & 1) cells.push([r + i, j]);
    const l = E.place(b, 0, o, r, c);
    total += l;
    return { piece: p, cells, lines: l, shape: o.m.map(m => Array.from({ length: o.w }, (_, j) => m >> j & 1)) };
  });
  return { placed: moves.length, need: np, steps, board_after: b, lines: total };
}
