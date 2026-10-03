"""Cross-Entropy Method로 평가 가중치 튜닝 (시뮬레이션 생존 턴 수 최대화)."""
import ctypes, json, os, sys, time
import numpy as np
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
FEATS = ["cells", "row_trans", "col_trans", "iso_holes", "empty_rows", "partial_rows",
         "row_sq", "wells", "dead_types", "inv_fits", "log_fits", "lines", "pts"]
_L = None


def lib():
    global _L
    if _L is None:
        _L = ctypes.CDLL(os.path.join(HERE, os.environ.get("LIB", "libengine.so")))
    return _L


_DIST_SET = False


def play(args):
    global _DIST_SET
    if not _DIST_SET and os.environ.get("DIST"):  # 단계별 블록 분포(5x19 json)
        P = np.ascontiguousarray(json.load(open(os.path.join(HERE, os.environ["DIST"]))), dtype=np.float64)
        lib().mm_set_dist(P.ctypes.data_as(ctypes.c_void_p))
    _DIST_SET = True
    w, seed, beam, max_turns = args
    w = np.zeros(13); w[:len(args[0])] = args[0]  # 예전 12개짜리 가중치는 0으로 채움
    st = (ctypes.c_int * 10)()
    cfg = (ctypes.c_int * 8)(*([int(x) for x in beam[:8]] + [0] * (8 - len(beam[:8]))))
    death = float(beam[8]) if len(beam) > 8 else -2000.0
    lib().mm_play(ctypes.c_uint64(seed), w.ctypes.data_as(ctypes.c_void_p), cfg, ctypes.c_double(death), max_turns,
                  int(os.environ.get("TARGET", 0)), st)
    return list(st)


def evaluate_pop(pool, pop, seeds, beam, max_turns):
    jobs = [(w, s, beam, max_turns) for w in pop for s in seeds]
    res = pool.map(play, jobs, chunksize=1)
    turns = np.array([r[0] for r in res], dtype=float).reshape(len(pop), len(seeds))
    return turns


def main():
    beam = tuple(int(x) for x in os.environ.get("BEAM", "16,16,64").split(","))
    iters, popn, nelite, ngames = int(os.environ.get("ITERS", 30)), 128, 16, int(os.environ.get("GAMES", 32))
    max_turns = int(os.environ.get("MAXT", 3000))
    out = os.environ.get("OUT", os.path.join(HERE, "weights.json"))
    mu = np.array([-1, -0.5, -0.3, -2, 1, -1, 0.5, -0.5, -20, -5, 0.5, 0], dtype=float)
    if os.path.exists(out):
        mu = np.array(json.load(open(out))["w"], dtype=float)
    sigma = np.maximum(np.abs(mu) * 0.5, 0.5)
    rng = np.random.default_rng(0)
    best = (-1, mu)
    with Pool(os.cpu_count()) as pool:
        for it in range(iters):
            seeds = rng.integers(1, 2**62, ngames).tolist()
            pop = [mu] + [mu + sigma * rng.standard_normal(len(mu)) for _ in range(popn - 1)]
            t = time.time()
            turns = evaluate_pop(pool, pop, seeds, beam, max_turns)
            fit = np.log(turns).mean(1)  # 기하평균: 운 좋은 판 하나에 휘둘리지 않게
            order = np.argsort(-fit)
            elite = np.array([pop[i] for i in order[:nelite]])
            mu = elite.mean(0)
            sigma = 0.7 * sigma + 0.3 * elite.std(0) + 0.02
            gm = np.exp(fit[order[0]])
            print(f"it {it:2d}  best geo {gm:7.1f}  center geo {np.exp(fit[0]):7.1f}  "
                  f"median turns(best) {np.median(turns[order[0]]):6.0f}  {time.time()-t:5.1f}s", flush=True)
            if np.exp(fit[0]) > best[0]:
                best = (np.exp(fit[0]), pop[0])
            json.dump({"w": mu.tolist(), "feats": FEATS, "beam": beam}, open(out, "w"), indent=1)
    print("final mu", dict(zip(FEATS, np.round(mu, 3))))


if __name__ == "__main__":
    main()
