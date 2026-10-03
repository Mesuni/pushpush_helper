"""가중치/빔 설정을 고정 시드 N판으로 벤치마크."""
import json, os, sys, time
import numpy as np
from multiprocessing import Pool
from tune import play, HERE

def bench(w, beam, n=256, max_turns=int(os.environ.get("MAXT", 5000)), pool=None):
    jobs = [(np.array(w), s, beam, max_turns) for s in range(1000, 1000 + n)]
    res = np.array(pool.map(play, jobs, chunksize=1))
    t = res[:, 0].astype(float)
    return dict(geo=np.exp(np.log(t).mean()), mean=t.mean(), median=np.median(t),
                p10=np.percentile(t, 10), p90=np.percentile(t, 90), reach=res[:, 3].mean(), lines_per_turn=res[:, 1].sum() / t.sum(),
                score_mean=res[:, 4].mean(), score_med=np.median(res[:, 4]), pts_per_line=res[:, 4].sum() / max(1, res[:, 1].sum()),
                multi=(res[:, 5:10].sum(0) / res[:, 5:10].sum()).round(3).tolist())

if __name__ == "__main__":
    wf = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "weights.json")
    w = json.load(open(wf))["w"]
    if os.environ.get("WPTS"):  # 동시삭제 점수 가중치 덮어쓰기
        w = (list(w) + [0] * 13)[:13]; w[12] = float(os.environ["WPTS"])
    beams = [tuple(map(float, b.split(","))) for b in (sys.argv[2:] or ["16,16,64", "64,64,256", "256,256,1024"])]
    with Pool(os.cpu_count()) as pool:
        for b in beams:
            t = time.time(); r = bench(w, b, pool=pool)
            print(b, os.environ.get("WPTS", ""), {k: (round(v, 3) if not isinstance(v, list) else v) for k, v in r.items()}, f"{time.time()-t:.0f}s", flush=True)
