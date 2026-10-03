// 한글 모아모아 엔진: 10x16 판, 가로줄만 삭제(중력 없음), 회전/반전 허용, 3개씩 순서 자유 배치.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 10
#define H 16
#define FULL 0x3FF
#define NP 19
#define MAXO 8
#define MAXC 10
#define NF 13  // 마지막 두 개: 이번 턴 지운 줄 수, 이번 턴 점수(동시 삭제 줄 수의 제곱 합)

typedef struct { int h, w, n; uint16_t m[5]; int8_t ci[MAXC], cj[MAXC]; } Orient;
typedef struct { uint16_t r[H]; } Board;

static Orient ori[NP][MAXO];
static int nori[NP], ncell[NP];
static int inited = 0;

static const char *SHAPES[NP] = {
    "#",
    "#.|.#|#.",
    "#|#|#",
    "#.|##",
    "#..|###",
    "#.|##|#.",
    ".#.|#.#|.#.",
    "###|#.#",
    "#####",
    "#.#|##.|#.#",
    ".#|##|.#|##",
    "#####|.#.#.",
    "#.#|.#.|###|.#.",
    "###|#.#|###",
    "###.#|#.###",
    "#.#.#|#####",
    "..#..|#####|.#.#.|..#..",
    "####|#.#.|####",
    "####|.##.|####",
};

static void init(void) {
    if (inited) return;
    for (int p = 0; p < NP; p++) {
        int ci[MAXC], cj[MAXC], n = 0, i = 0, j = 0;
        for (const char *s = SHAPES[p]; *s; s++) {
            if (*s == '|') { i++; j = 0; continue; }
            if (*s == '#') { ci[n] = i; cj[n] = j; n++; }
            j++;
        }
        ncell[p] = n;
        nori[p] = 0;
        for (int t = 0; t < 8; t++) {
            int a[MAXC], b[MAXC];
            for (int k = 0; k < n; k++) {
                int x = ci[k], y = cj[k];
                for (int r = 0; r < (t & 3); r++) { int tmp = x; x = y; y = -tmp; }  // 90도 회전
                if (t & 4) y = -y;                                                     // 좌우 반전
                a[k] = x; b[k] = y;
            }
            int mi = 99, mj = 99;
            for (int k = 0; k < n; k++) { if (a[k] < mi) mi = a[k]; if (b[k] < mj) mj = b[k]; }
            Orient o; memset(&o, 0, sizeof o);
            o.n = n;
            for (int k = 0; k < n; k++) {
                o.ci[k] = a[k] - mi; o.cj[k] = b[k] - mj;
                o.m[o.ci[k]] |= 1 << o.cj[k];
                if (o.ci[k] + 1 > o.h) o.h = o.ci[k] + 1;
                if (o.cj[k] + 1 > o.w) o.w = o.cj[k] + 1;
            }
            int dup = 0;
            for (int q = 0; q < nori[p]; q++)
                if (ori[p][q].h == o.h && ori[p][q].w == o.w && !memcmp(ori[p][q].m, o.m, sizeof o.m)) dup = 1;
            if (!dup) ori[p][nori[p]++] = o;
        }
    }
    inited = 1;
}

// row r에서 놓을 수 있는 열 위치들의 비트마스크
static inline uint32_t fit_cols(const Board *b, const Orient *o, int r) {
    uint32_t acc = (1u << (W - o->w + 1)) - 1;
    for (int k = 0; k < o->n && acc; k++)
        acc &= (uint32_t)((~b->r[r + o->ci[k]]) & FULL) >> o->cj[k];
    return acc;
}

static inline int place(Board *b, const Orient *o, int r, int c) {
    int lines = 0;
    for (int i = 0; i < o->h; i++) {
        b->r[r + i] |= o->m[i] << c;
        if (b->r[r + i] == FULL) { b->r[r + i] = 0; lines++; }
    }
    return lines;
}

static int count_fits(const Board *b, int p) {
    int cnt = 0;
    for (int q = 0; q < nori[p]; q++) {
        const Orient *o = &ori[p][q];
        for (int r = 0; r + o->h <= H; r++) cnt += __builtin_popcount(fit_cols(b, o, r));
    }
    return cnt;
}

// 특징값 계산. full=1이면 블록 적합도(비싼 특징)까지 계산.
static void features(const Board *b, int full, double *f) {
    int cells = 0, rtr = 0, ctr = 0, iso = 0, empty = 0, partial = 0, sq = 0, wells = 0;
    for (int r = 0; r < H; r++) {
        uint32_t x = b->r[r];
        int c = __builtin_popcount(x);
        cells += c; sq += c * c;
        if (c == 0) empty++; else partial++;
        uint32_t y = (x << 1) | 1 | (1u << (W + 1));
        rtr += __builtin_popcount((y ^ (y >> 1)) & ((1u << (W + 1)) - 1));
        uint32_t up = r > 0 ? b->r[r - 1] : FULL, dn = r < H - 1 ? b->r[r + 1] : FULL;
        ctr += __builtin_popcount((x ^ dn) & FULL) * (r < H - 1) + (r == 0) * __builtin_popcount(~x & FULL) +
               (r == H - 1) * __builtin_popcount(~x & FULL);
        uint32_t e = ~x & FULL;
        uint32_t L = (x << 1) | 1, R = (x >> 1) | (1u << (W - 1));
        iso += __builtin_popcount(e & L & R & up & dn);
        wells += __builtin_popcount(e & L & R & ~(up & dn));  // 좌우가 막힌 빈칸(세로 통로)
    }
    f[0] = cells; f[1] = rtr; f[2] = ctr; f[3] = iso; f[4] = empty; f[5] = partial;
    f[6] = sq / 10.0; f[7] = wells;
    f[8] = f[9] = f[10] = 0;
    if (full) {
        int dead = 0; double inv = 0, lg = 0;
        for (int p = 0; p < NP; p++) {
            int c = count_fits(b, p);
            if (!c) dead++;
            inv += 1.0 / (1 + c);
            lg += log1p(c);
        }
        f[8] = dead; f[9] = inv; f[10] = lg;
    }
}

static double evaluate(const Board *b, int full, int lines, int pts, const double *w) {
    double f[NF];
    features(b, full, f);
    f[11] = lines; f[12] = pts;
    double s = 0;
    for (int k = 0; k < NF; k++) s += w[k] * f[k];
    return s;
}

// ---------- 탐색 ----------
typedef struct { Board b; int lines, pts; uint8_t used; int16_t parent; int8_t p, o, r, c; double score; } Node;
typedef struct { float score; int32_t parent; int8_t s, o, r, c; } Cand;  // 자식 후보(보드는 선택 후 재구성)

static int g_threads = 1;

static uint64_t bhash(const Board *b, int used) {
    uint64_t h = 1469598103934665603ULL ^ used;
    for (int i = 0; i < H; i++) { h ^= b->r[i]; h *= 1099511628211ULL; h ^= h >> 29; }
    return h;
}

static int cmp_cand(const void *x, const void *y) {
    float a = ((const Cand *)x)->score, b = ((const Cand *)y)->score;
    return (a < b) - (a > b);
}

// quickselect: 점수 상위 m개를 배열 앞쪽으로 모은다(순서는 미정)
static void top_select(Cand *a, int n, int m) {
    int lo = 0, hi = n - 1, k = m - 1;
    while (lo < hi) {
        float piv = a[(lo + hi) >> 1].score;
        int i = lo, j = hi;
        while (i <= j) {
            while (a[i].score > piv) i++;
            while (a[j].score < piv) j--;
            if (i <= j) { Cand t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (k <= j) hi = j; else if (k >= i) lo = i; else break;
    }
}

// 후보 중 점수 상위 keep개(중복 보드 제거)를 Node로 만들어 out에 채운다
static int select_top(Cand *c, int n, int keep, const Node *par, const int *pieces, Node *out) {
    int M = keep * 2 + 8;
    for (;;) {
        if (M > n) M = n;
        top_select(c, n, M);
        qsort(c, M, sizeof(Cand), cmp_cand);
        int cap = 1; while (cap < 2 * M + 16) cap <<= 1;
        uint64_t *set = calloc(cap, sizeof(uint64_t));
        int m = 0;
        for (int i = 0; i < M && m < keep; i++) {
            const Node *pa = &par[c[i].parent];
            Node x;
            x.b = pa->b;
            int l = place(&x.b, &ori[pieces[c[i].s]][c[i].o], c[i].r, c[i].c);
            x.lines = pa->lines + l; x.pts = pa->pts + l * l;
            x.used = pa->used | (1 << c[i].s);
            x.parent = c[i].parent; x.p = c[i].s; x.o = c[i].o; x.r = c[i].r; x.c = c[i].c;
            x.score = c[i].score;
            uint64_t k = bhash(&x.b, x.used) | 1;
            int h = (int)(k & (cap - 1)), dup = 0;
            while (set[h]) { if (set[h] == k) { dup = 1; break; } h = (h + 1) & (cap - 1); }
            if (dup) continue;
            set[h] = k;
            out[m++] = x;
        }
        free(set);
        if (m >= keep || M >= n) return m;
        M *= 4;  // 중복이 많아 모자라면 더 넓게 다시
    }
}

// B1,B2: 1·2번째 블록 후 빔 폭, K: 최종 정밀평가 후보 수.
// K2>0이면 상위 K2개 후보를 다음 턴 S개 샘플(내부 빔 iB)로 기대값 평가, 다음 턴 사망은 death 점수.
typedef struct { int B1, B2, K, K2, S, iB1, iB2, iK; double death; uint64_t seed; int base_lines; } SearchCfg;

// 단계별 블록 분포: 누적 지운 줄 30/60/100/150에서 단계 상승. 기본은 균등.
#define NSTAGE 5
static double g_cdf[NSTAGE][NP];
static int g_dist_set = 0;
static int stage_of(int lines) { return lines < 30 ? 0 : lines < 60 ? 1 : lines < 100 ? 2 : lines < 150 ? 3 : 4; }
static void ensure_dist(void) {
    if (g_dist_set) return;
    for (int k = 0; k < NSTAGE; k++) for (int p = 0; p < NP; p++) g_cdf[k][p] = (p + 1.0) / NP;
    g_dist_set = 1;
}
static int draw_u(double u, int stage) {
    for (int p = 0; p < NP - 1; p++) if (u < g_cdf[stage][p]) return p;
    return NP - 1;
}

static uint64_t rng_next(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }

// pieces[0..np-1] (np=1~3)을 모두 놓는 최선 배치. 반환: 놓을 수 있었던 블록 수.
// moves[i*4..] = piece_slot, orient, row, col (놓는 순서대로)
static int search(const Board *start, const int *pieces, int np, const double *w, SearchCfg cfg, int *moves, double *best_score) {
    init();
    Node *lvl[4]; int cnt[4];
    lvl[0] = malloc(sizeof(Node)); cnt[0] = 1;
    lvl[0][0].b = *start; lvl[0][0].lines = 0; lvl[0][0].pts = 0; lvl[0][0].used = 0; lvl[0][0].parent = -1;
    int keeps[4] = {1, cfg.B1, cfg.B2, cfg.K};
    keeps[np] = cfg.K;
    int depth = 0;
    size_t cap = 4096;
    Cand *ch = malloc(cap * sizeof(Cand));
    for (int d = 1; d <= np; d++) {
        size_t n = 0;
        for (int i = 0; i < cnt[d - 1]; i++) {
            const Node *pa = &lvl[d - 1][i];
            for (int s = 0; s < np; s++) {
                if (pa->used & (1 << s)) continue;
                int skip = 0;  // 같은 종류 블록이면 앞 슬롯만 사용
                for (int t = 0; t < s; t++) if (!(pa->used & (1 << t)) && pieces[t] == pieces[s]) skip = 1;
                if (skip) continue;
                int p = pieces[s];
                for (int q = 0; q < nori[p]; q++) {
                    const Orient *o = &ori[p][q];
                    for (int r = 0; r + o->h <= H; r++) {
                        uint32_t cols = fit_cols(&pa->b, o, r);
                        while (cols) {
                            int c = __builtin_ctz(cols); cols &= cols - 1;
                            if (n == cap) { cap *= 2; ch = realloc(ch, cap * sizeof(Cand)); }
                            Board tb = pa->b;
                            int l = place(&tb, o, r, c);
                            Cand *x = &ch[n++];
                            x->score = (float)evaluate(&tb, 0, pa->lines + l, pa->pts + l * l, w);
                            x->parent = i; x->s = s; x->o = q; x->r = r; x->c = c;
                        }
                    }
                }
            }
        }
        if (n == 0) break;
        lvl[d] = malloc(keeps[d] * sizeof(Node));
        cnt[d] = select_top(ch, (int)n, keeps[d], lvl[d - 1], pieces, lvl[d]);
        depth = d;
    }
    free(ch);
    int best = 0;
    if (depth == np) {  // 최종 후보는 비싼 평가로 다시 채점
        Node *fin = lvl[np]; int nf = cnt[np];
        double bs = -1e300;
        for (int i = 0; i < nf; i++) {
            fin[i].score = evaluate(&fin[i].b, 1, fin[i].lines, fin[i].pts, w);
            if (fin[i].score > bs) { bs = fin[i].score; best = i; }
        }
        if (cfg.K2 > 0 && cfg.S > 0 && nf > 1) {  // 다음 턴 기대값 lookahead
            int *idx = malloc(nf * sizeof(int));
            for (int i = 0; i < nf; i++) idx[i] = i;
            int k2 = cfg.K2 < nf ? cfg.K2 : nf, S = cfg.S;
            for (int a = 0; a < k2; a++)  // 부분 선택 정렬
                for (int b2 = a + 1; b2 < nf; b2++)
                    if (fin[idx[b2]].score > fin[idx[a]].score) { int t = idx[a]; idx[a] = idx[b2]; idx[b2] = t; }
            ensure_dist();
            double *u = malloc(S * 3 * sizeof(double));  // 모든 후보에 같은 난수 사용 (단계별 분포로 변환)
            uint64_t rs = cfg.seed * 0x9E3779B97F4A7C15ULL + 12345;
            for (int t = 0; t < S * 3; t++) u[t] = (rng_next(&rs) >> 11) * (1.0 / 9007199254740992.0);
            double *val = malloc((size_t)k2 * S * sizeof(double));
            SearchCfg in = {cfg.iB1, cfg.iB2, cfg.iK, 0, 0, 0, 0, 0, 0, 0, 0};
            #pragma omp parallel for schedule(dynamic) num_threads(g_threads) if (g_threads > 1)
            for (int t = 0; t < k2 * S; t++) {
                int mv[12], pc[3]; double v;
                const Node *x = &fin[idx[t / S]];
                int st = stage_of(cfg.base_lines + x->lines);
                for (int k = 0; k < 3; k++) pc[k] = draw_u(u[(t % S) * 3 + k], st);
                int dd = search(&x->b, pc, 3, w, in, mv, &v);
                val[t] = dd == 3 ? v : cfg.death;
            }
            bs = -1e300;
            for (int a = 0; a < k2; a++) {
                double tot = 0;
                for (int s = 0; s < S; s++) tot += val[a * S + s];
                double sc = tot / S + w[11] * fin[idx[a]].lines + w[12] * fin[idx[a]].pts;
                if (sc > bs) { bs = sc; best = idx[a]; }
            }
            free(val); free(u); free(idx);
        }
        if (best_score) *best_score = bs;
    } else if (best_score) *best_score = -1e300;
    if (depth > 0) {
        int idx = best;
        for (int d = depth; d >= 1; d--) {
            const Node *x = &lvl[d][idx];
            moves[(d - 1) * 4 + 0] = x->p; moves[(d - 1) * 4 + 1] = x->o;
            moves[(d - 1) * 4 + 2] = x->r; moves[(d - 1) * 4 + 3] = x->c;
            idx = x->parent;
        }
    }
    for (int d = 0; d <= depth; d++) free(lvl[d]);
    return depth;
}

// 정해진 배치(같은 자리들)를 놓는 순서만 바꿔 동시 삭제 점수(줄 수 제곱 합)를 최대화.
// 모든 블록이 놓이고 최종 판이 같은 순서만 허용하므로 생존에는 영향이 없다.
static void reorder_for_points(const Board *start, const int *pieces, int n, int *moves) {
    if (n < 2) return;
    static const int perms3[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    static const int perms2[2][3] = {{0,1,0},{1,0,0}};
    int np = n == 3 ? 6 : 2;
    Board ref = *start;
    for (int k = 0; k < n; k++) {
        const Orient *o = &ori[pieces[moves[k * 4]]][moves[k * 4 + 1]];
        place(&ref, o, moves[k * 4 + 2], moves[k * 4 + 3]);
    }
    int best = -1, bestpts = -1;
    for (int q = 0; q < np; q++) {
        const int *pm = n == 3 ? perms3[q] : perms2[q];
        Board b = *start; int pts = 0, ok = 1;
        for (int k = 0; k < n && ok; k++) {
            const int *m = &moves[pm[k] * 4];
            const Orient *o = &ori[pieces[m[0]]][m[1]];
            if (!((fit_cols(&b, o, m[2]) >> m[3]) & 1)) { ok = 0; break; }
            int l = place(&b, o, m[2], m[3]);
            pts += l * l;
        }
        if (ok && !memcmp(&b, &ref, sizeof b) && pts > bestpts) { bestpts = pts; best = q; }
    }
    if (best <= 0) return;  // 원래 순서(0번)가 이미 최선
    const int *pm = n == 3 ? perms3[best] : perms2[best];
    int tmp[12];
    for (int k = 0; k < n; k++) memcpy(&tmp[k * 4], &moves[pm[k] * 4], 4 * sizeof(int));
    memcpy(moves, tmp, n * 4 * sizeof(int));
}

// ---------- 외부 API ----------
void mm_set_threads(int n) { g_threads = n < 1 ? 1 : n; }
int mm_num_orients(int p) { init(); return nori[p]; }
int mm_orient(int p, int q, uint16_t *masks) { init(); for (int i = 0; i < 5; i++) masks[i] = ori[p][q].m[i]; return ori[p][q].h; }
int mm_cells(int p) { init(); return ncell[p]; }

// cfgi = {B1, B2, K, K2, S, iB1, iB2, iK}
static SearchCfg make_cfg(const int *ci, double death, uint64_t seed) {
    SearchCfg c = {ci[0], ci[1], ci[2], ci[3], ci[4], ci[5], ci[6], ci[7], death, seed, 0};
    return c;
}

// base_lines: 지금까지 지운 줄 수(단계 판정용)
int mm_search(const uint16_t *board, const int *pieces, int np, const double *w, const int *cfgi, double death,
              uint64_t seed, int base_lines, int *moves, double *score) {
    init();
    Board b; memcpy(b.r, board, sizeof b.r);
    SearchCfg cfg = make_cfg(cfgi, death, seed);
    cfg.base_lines = base_lines;
    int d = search(&b, pieces, np, w, cfg, moves, score);
    reorder_for_points(&b, pieces, d, moves);
    return d;
}

// probs: NSTAGE x NP 확률표(행마다 정규화)
void mm_set_dist(const double *probs) {
    for (int k = 0; k < NSTAGE; k++) {
        double tot = 0, acc = 0;
        for (int p = 0; p < NP; p++) tot += probs[k * NP + p];
        for (int p = 0; p < NP; p++) { acc += probs[k * NP + p] / tot; g_cdf[k][p] = acc; }
        g_cdf[k][NP - 1] = 1.0;
    }
    g_dist_set = 1;
}

void mm_features(const uint16_t *board, double *f) {
    init(); Board b; memcpy(b.r, board, sizeof b.r); features(&b, 1, f); f[11] = f[12] = 0;
}

// 한 판 시뮬레이션. stats: [턴 수, 지운 줄, 놓은 칸, 생존여부(1=max_turns 도달)]
// 점수: 블록 하나로 n줄 동시 삭제 시 300*n^2. target 점수 도달 시 종료(0이면 무시).
// stats: [턴 수, 지운 줄, 놓은 칸, 성공(목표점수 또는 max_turns 도달), 점수, 동시삭제 횟수 n=1..5]
void mm_play(uint64_t seed, const double *w, const int *cfgi, double death, int max_turns, int target, int *stats) {
    init();
    Board b; memset(&b, 0, sizeof b);
    uint64_t s = seed * 2654435761ULL + 88172645463325252ULL;
    SearchCfg cfg = make_cfg(cfgi, death, 0);
    int turns = 0, lines = 0, cells = 0, alive = 1, score = 0, hist[6] = {0};
    while (turns < max_turns && !(target > 0 && score >= target)) {
        int pieces[3], moves[12];
        ensure_dist();
        for (int i = 0; i < 3; i++) pieces[i] = draw_u((rng_next(&s) >> 11) * (1.0 / 9007199254740992.0), stage_of(lines));
        cfg.seed = rng_next(&s);
        cfg.base_lines = lines;  // lookahead 샘플은 실제 다음 블록과 독립
        int d = search(&b, pieces, 3, w, cfg, moves, NULL);
        if (d == 3) reorder_for_points(&b, pieces, 3, moves);
        if (d < 3) { alive = 0; break; }
        for (int i = 0; i < 3; i++) {
            int p = pieces[moves[i * 4]];
            int l = place(&b, &ori[p][moves[i * 4 + 1]], moves[i * 4 + 2], moves[i * 4 + 3]);
            lines += l; score += 300 * l * l; hist[l]++;
            cells += ncell[p];
        }
        turns++;
    }
    stats[0] = turns; stats[1] = lines; stats[2] = cells; stats[3] = alive; stats[4] = score;
    for (int k = 1; k <= 5; k++) stats[4 + k] = hist[k];
}
