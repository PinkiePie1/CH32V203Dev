#include "SandSim.h"
#include "charlie.h"
#include <string.h>

#pragma GCC optimize("O3,unroll-loops,rename-registers,ira-loop-pressure")

_Static_assert(GLOBAL_Q == 24, "SandSim uses Q24");
_Static_assert(NumberOfParticles <= 256U, "Particle IDs use uint8_t");
_Static_assert(2 * ParticleRadius <= Spacing, "Particle diameter must fit one grid cell");

static _iq invertSpacing;

#define FLUID_CELL 0U
#define AIR_CELL 1U
#define SOLID_CELL 2U

_iq particlePos[NumberOfParticles * 2];
_iq particleVel[NumberOfParticles * 2];

static _iq uVel[CellCount];
static _iq vVel[CellCount];
static _iq uPrev[CellCount];
static _iq vPrev[CellCount];
static _iq uWeight[CellCount];
static _iq vWeight[CellCount];

static _iq particleDensity[CellCount];
static uint8_t cellType[CellCount];
static _iq particleRestDensity = 0;

//内联定点乘法
static inline _iq qmul(_iq a, _iq b) {
    return (_iq)(((long long)a * (long long)b) >> 24);
}

/* 按字清零和复制网格。 */
static void clear_grid(_iq *grid) {
    unsigned int i = 0;
    for (; i + 3U < CellCount; i += 4U) {
        grid[i] = 0;
        grid[i + 1U] = 0;
        grid[i + 2U] = 0;
        grid[i + 3U] = 0;
    }
    for (; i < CellCount; i++) {
        grid[i] = 0;
    }
}

static void copy_grid(_iq *dst, const _iq *src) {
    unsigned int i = 0;
    for (; i + 3U < CellCount; i += 4U) {
        dst[i] = src[i];
        dst[i + 1U] = src[i + 1U];
        dst[i + 2U] = src[i + 2U];
        dst[i + 3U] = src[i + 3U];
    }
    for (; i < CellCount; i++) {
        dst[i] = src[i];
    }
}

/* 流体单元索引表，由particles_to_grid生成，供compute_grid_forces只遍历流体单元 */
static uint16_t fluidCells[CellCount];
static unsigned int fluidCellCount = 0;

/* 压力系数 -overRelaxiation / 可通行邻格数。 */
static _iq negOverInvSolid[5];

/* 本轮分桶固定，下一轮按新位置重建。 */
static uint16_t particleCell[NumberOfParticles];

/* Q16 小数部分，半格偏移通过加 0x8000 回绕得到。每粒子 6 字节。 */
typedef struct {
    uint16_t tx;      /* 无偏移那套的插值系数，Q16；半格偏移 = +0.5 回绕 */
    uint16_t ty;
    uint8_t  x0;      /* 无偏移那套的起始格号 */
    uint8_t  y0;
} ParticleGridSample;
static ParticleGridSample particleGridSample[NumberOfParticles];

static const _iq minX = Spacing + ParticleRadius;
static const _iq minY = Spacing + ParticleRadius;
static _iq maxX;
static _iq maxY;

#define CellPrefixCountSize (CellCount + 1U)

static uint16_t cellParticleCountPrefix[CellPrefixCountSize];
static uint8_t particlePosId[NumberOfParticles];

/* 固定墙面的速度始终为零。 */
#define SolidListMax (3U * (CellNumX + CellNumY))
static uint16_t uSolidCells[SolidListMax];
static uint16_t vSolidCells[SolidListMax];
static unsigned int uSolidCount = 0;
static unsigned int vSolidCount = 0;

/* 可通行邻格：位 0 左、位 1 右、位 2 下、位 3 上。 */
static uint8_t solidDirMask[CellCount];

/* 粒子对的最小间距，InitParticles里按 ParticleRadius 计算 */
static _iq minDist;
static _iq minDist2;

/* 距离平方分档，线性插值得到 minDist/d。 */
#define RSQRT_ENTRIES 160U
static _iq ratioTab[RSQRT_ENTRIES];
static _iq ratioXMin;
static unsigned int ratioShift;

static inline _iq qdist_ratio(_iq x) {
    unsigned int t = (unsigned int)(x - ratioXMin);
    unsigned int i = t >> ratioShift;
    unsigned int fraction = t & ((1U << ratioShift) - 1U);
    return ratioTab[i] + (_iq)(((long long)(ratioTab[i + 1U] - ratioTab[i]) * fraction) >> ratioShift);
}

/* 分母归一化到 [1,2)，64 档初值加两次牛顿迭代。 */
#define WRECIP_ENTRIES 64U
static _iq wRecipTab[WRECIP_ENTRIES];

static inline _iq normalized_reciprocal(_iq w) {
    unsigned int index = (unsigned int)(w - _IQ(1.0)) >> 18;
    _iq r = wRecipTab[index];
    r = qmul(r, _IQ(2.0) - qmul(w, r));
    return qmul(r, _IQ(2.0) - qmul(w, r));
}

static inline _iq divide_weight(_iq value, _iq weight) {
    /* 常见权重使用固定移位，省去 RV32 的软件 CLZ 和 64 位变移位。 */
    if (weight >= _IQ(0.5) && weight < _IQ(4.0)) {
        unsigned int small = weight < _IQ(1.0);
        unsigned int large = weight >= _IQ(2.0);
        _iq w = small ? weight << 1 : large ? weight >> 1 : weight;
        _iq r = normalized_reciprocal(w);
        long long product = (long long)value * r;
        if (small) return (_iq)(product >> 23);
        if (large) return (_iq)(product >> 25);
        return (_iq)(product >> 24);
    }
    int shift = __builtin_clz((unsigned int)weight) - 7;
    _iq w = shift >= 0 ? weight << shift : weight >> -shift;
    _iq r = normalized_reciprocal(w);
    return (_iq)(((long long)value * r) >> (24 - shift));
}

/* 用减法补足另外两个权重，四项之和严格等于 1。 */
static inline void bilinear_weights(uint16_t tx, uint16_t ty, _iq w[4]) {
    uint32_t sy = 0x10000U - ty;
    w[1] = ((uint32_t)tx * sy) >> 8;
    w[2] = ((uint32_t)tx * ty) >> 8;
    w[0] = (_iq)(sy << 8) - w[1];
    w[3] = ((_iq)ty << 8) - w[2];
}

static inline int sample_cell(uint8_t base, uint16_t tQ16) {
    if (tQ16 >= 0x8000U) {
        return (int)base;
    }
    return (base > 0U) ? (int)base - 1 : 0;
}

static void cache_particle_grid_samples(void) {
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        _iq x = qmul(particlePos[XID(i)], invertSpacing);
        _iq y = qmul(particlePos[YID(i)], invertSpacing);
        ParticleGridSample *sample = &particleGridSample[i];
        sample->x0 = (uint8_t)_IQint(x);
        sample->y0 = (uint8_t)_IQint(y);
        sample->tx = (uint16_t)(x >> 8);
        sample->ty = (uint16_t)(y >> 8);
    }
}

static inline unsigned int is_solid_cell(unsigned int x, unsigned int y) {
    return (x == 0U || x == CellNumX - 1U || y == 0U || y == CellNumY - 1U) ? 1U : 0U;
}

void InitParticles(void) {
    fluidCellCount = 0;
    memset(particleVel, 0, sizeof(particleVel));
    memset(uVel, 0, sizeof(uVel));
    memset(vVel, 0, sizeof(vVel));
    memset(uPrev, 0, sizeof(uPrev));
    memset(vPrev, 0, sizeof(vPrev));
    memset(particleDensity, 0, sizeof(particleDensity));
    particleRestDensity = _IQ(0.0);

    minDist = 2 * ParticleRadius;
    minDist2 = qmul(minDist, minDist);
    ratioXMin = minDist2 >> 4;
    unsigned int range = (unsigned int)(minDist2 - ratioXMin);
    ratioShift = 0;
    while ((range >> ratioShift) >= RSQRT_ENTRIES - 1U) {
        ratioShift++;
    }
    unsigned int entries = (range >> ratioShift) + 2U;
    for (unsigned int i = 0; i < entries; i++) {
        _iq x = ratioXMin + (_iq)(i << ratioShift);
        ratioTab[i] = _IQdiv(minDist, _IQsqrt(x));
    }

    for (unsigned int i = 0; i < WRECIP_ENTRIES; i++) {
        _iq w = _IQ(1.0) + (_iq)(i << 18) + (1L << 17);
        wRecipTab[i] = _IQdiv(_IQ(1.0), w);
    }

    uSolidCount = 0;
    vSolidCount = 0;
    for (unsigned int x = 0; x < CellNumX; x++) {
        for (unsigned int y = 0; y < CellNumY; y++) {
            unsigned int idx = INDEX(x, y);
            uint8_t solidHere = (uint8_t)is_solid_cell(x, y);
            uint8_t leftSolid = (x > 0U) ? (uint8_t)is_solid_cell(x - 1U, y) : 0U;
            uint8_t rightSolid = (x + 1U < CellNumX) ? (uint8_t)is_solid_cell(x + 1U, y) : 0U;
            uint8_t bottomSolid = (y > 0U) ? (uint8_t)is_solid_cell(x, y - 1U) : 0U;
            uint8_t topSolid = (y + 1U < CellNumY) ? (uint8_t)is_solid_cell(x, y + 1U) : 0U;

            cellType[idx] = solidHere ? SOLID_CELL : AIR_CELL;
            if (solidHere || leftSolid) {
                uSolidCells[uSolidCount++] = (uint16_t)idx;
            }
            if (solidHere || bottomSolid) {
                vSolidCells[vSolidCount++] = (uint16_t)idx;
            }
            solidDirMask[idx] = (uint8_t)((leftSolid ? 0U : 1U) | (rightSolid ? 0U : 2U)
                                        | (bottomSolid ? 0U : 4U) | (topSolid ? 0U : 8U));
        }
    }

    const _iq h = Spacing;
    const _iq r = ParticleRadius;
    const _iq dx = qmul(_IQ(2.0), r);
    const _iq dy = qmul(_IQ(0.86602540378), dx);
    invertSpacing = _IQdiv(_IQ(1.0), Spacing);
    maxX = qmul(_IQ(CellNumX - 1U), Spacing) - ParticleRadius;
    maxY = qmul(_IQ(CellNumY - 1U), Spacing) - ParticleRadius;

    for (unsigned int s = 1U; s <= 4U; s++) {
        negOverInvSolid[s] = -_IQdiv(overRelaxiation, _IQ(s));
    }
    unsigned int p_num = 0;
    for (unsigned int i = 0; i < CellNumX && p_num < NumberOfParticles; i++) {
        for (unsigned int j = 0; j < CellNumY && p_num < NumberOfParticles; j++) {
            _iq px = h + r + qmul(dx, _IQ(i)) + ((j % 2U == 0U) ? _IQ(0.0) : r);
            _iq py = h + r + qmul(dy, _IQ(j));
            if (px > qmul(_IQ(CellNumX - 1U), h) - r || py > qmul(_IQ(CellNumY - 1U), h) - r) {
                continue;
            }
            particlePos[XID(p_num)] = px;
            particlePos[YID(p_num)] = py;
            p_num++;
        }
    }

    for (; p_num < NumberOfParticles; p_num++) {
        particlePos[XID(p_num)] = h + r;
        particlePos[YID(p_num)] = h + r;
    }
}

/* 墙面只反弹朝外的速度，粒子分离造成的越界不会翻转朝内速度。 */
static void clamp_particles(void) {
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        _iq *pos = &particlePos[2U * i];
        _iq *vel = &particleVel[2U * i];
        if (pos[0] < minX) {
            pos[0] = minX;
            if (vel[0] < 0) vel[0] = qmul(vel[0], BOUNCYNESS);
        } else if (pos[0] > maxX) {
            pos[0] = maxX;
            if (vel[0] > 0) vel[0] = qmul(vel[0], BOUNCYNESS);
        }
        if (pos[1] < minY) {
            pos[1] = minY;
            if (vel[1] < 0) vel[1] = qmul(vel[1], BOUNCYNESS);
        } else if (pos[1] > maxY) {
            pos[1] = maxY;
            if (vel[1] > 0) vel[1] = qmul(vel[1], BOUNCYNESS);
        }
    }
}

void ParticleIntegrate(_iq xAcceleration, _iq yAcceleration) {
    const _iq dvx = qmul(xAcceleration, dt);
    const _iq dvy = qmul(yAcceleration, dt);
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        particleVel[XID(i)] += dvx;
        particleVel[YID(i)] += dvy;
        particlePos[XID(i)] += qmul(particleVel[XID(i)], dt);
        particlePos[YID(i)] += qmul(particleVel[YID(i)], dt);
    }
    clamp_particles();
}

static void build_particle_grid(void) {
    /* 本工具链的 memset 逐字节写入，这里每次清四个计数。 */
    unsigned int n = 0;
    for (; n + 3U < CellPrefixCountSize; n += 4U) {
        cellParticleCountPrefix[n] = 0;
        cellParticleCountPrefix[n + 1U] = 0;
        cellParticleCountPrefix[n + 2U] = 0;
        cellParticleCountPrefix[n + 3U] = 0;
    }
    for (; n < CellPrefixCountSize; n++) cellParticleCountPrefix[n] = 0;
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int x = (unsigned int)_IQint(qmul(particlePos[XID(i)], invertSpacing));
        unsigned int y = (unsigned int)_IQint(qmul(particlePos[YID(i)], invertSpacing));
        unsigned int cell = INDEX(x, y);
        particleCell[i] = (uint16_t)cell;
        cellParticleCountPrefix[cell]++;
    }
    unsigned int prefix = 0;
    for (unsigned int cell = 0; cell < CellCount; cell++) {
        prefix += cellParticleCountPrefix[cell];
        cellParticleCountPrefix[cell] = prefix;
    }
    cellParticleCountPrefix[CellCount] = prefix;
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        particlePosId[--cellParticleCountPrefix[particleCell[i]]] = (uint8_t)i;
    }
}

static inline void separate_pair(unsigned int i, unsigned int j) {
    _iq dx = particlePos[XID(j)] - particlePos[XID(i)];
    _iq dy = particlePos[YID(j)] - particlePos[YID(i)];
    if ((unsigned int)(dx + minDist) >= (unsigned int)(2 * minDist)
        || (unsigned int)(dy + minDist) >= (unsigned int)(2 * minDist)) {
        return;
    }
    _iq d2 = qmul(dx, dx) + qmul(dy, dy);
    if (d2 >= minDist2) return;

    if (d2 == 0) {
        /* 重合或距离小于 Q24 平方精度时，沿一个轴分开。 */
        if (dx != 0) {
            dx = (dx > 0 ? minDist - dx : -minDist - dx) / 2;
            dy = 0;
        } else if (dy != 0) {
            dy = (dy > 0 ? minDist - dy : -minDist - dy) / 2;
        } else if ((i ^ j) & 1U) {
            dx = minDist / 2;
        } else {
            dy = minDist / 2;
        }
    } else {
        _iq scale;
        if (d2 >= ratioXMin) {
            scale = (qdist_ratio(d2) - _IQ(1.0)) / 2;
        } else {
            /* 近距离保留 Q32 平方精度，避免截断把距离低估。 */
            _iq square = (_iq)(((long long)dx * dx + (long long)dy * dy) >> 16);
            _iq distance = _IQsqrt(square) >> 4;
            scale = _IQdiv((minDist - distance) / 2, distance);
        }
        dx = qmul(dx, scale);
        dy = qmul(dy, scale);
    }
    particlePos[XID(i)] -= dx;
    particlePos[YID(i)] -= dy;
    particlePos[XID(j)] += dx;
    particlePos[YID(j)] += dy;
}

void PushParticlesApart(unsigned int nIters) {
    for (unsigned int iter = 0; iter < nIters; iter++) {
        build_particle_grid();
        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            unsigned int cell = particleCell[i];
            unsigned int ownEnd = cellParticleCountPrefix[cell + 1U];
            /* 桶内编号降序排列，遇到自己就结束；上方一格全部处理。 */
            for (unsigned int n = cellParticleCountPrefix[cell]; n < ownEnd; n++) {
                unsigned int j = particlePosId[n];
                if (j <= i) break;
                separate_pair(i, j);
            }
            for (unsigned int n = ownEnd; n < cellParticleCountPrefix[cell + 2U]; n++) {
                separate_pair(i, particlePosId[n]);
            }
            /* 右侧三格连续；另一半邻域由邻居处理。 */
            unsigned int right = cell + CellNumY;
            for (unsigned int n = cellParticleCountPrefix[right - 1U];
                 n < cellParticleCountPrefix[right + 2U]; n++) {
                separate_pair(i, particlePosId[n]);
            }
        }
        clamp_particles();
    }
}

void density_update(void) {
    clear_grid(particleDensity);

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        const ParticleGridSample *sample = &particleGridSample[i];
        int x0 = sample_cell(sample->x0, sample->tx);
        int y0 = sample_cell(sample->y0, sample->ty);
        uint16_t tx = sample->tx + 0x8000U;
        uint16_t ty = sample->ty + 0x8000U;
        _iq w[4];
        bilinear_weights(tx, ty, w);
        unsigned int cell = INDEX(x0, y0);
        particleDensity[cell] += w[0];
        particleDensity[cell + CellNumY] += w[1];
        particleDensity[cell + CellNumY + 1U] += w[2];
        particleDensity[cell + 1U] += w[3];
    }

    if (particleRestDensity == _IQ(0.0)) {
        long long sum = 0;
        for (unsigned int n = 0; n < fluidCellCount; n++) {
            sum += particleDensity[fluidCells[n]];
        }
        if (fluidCellCount > 0U) {
            particleRestDensity = (_iq)(sum / fluidCellCount);
        }
    }
}

void particles_to_grid(void) {
    /* 只清除上一帧的流体标记。 */
    for (unsigned int i = 0; i < fluidCellCount; i++) {
        cellType[fluidCells[i]] = AIR_CELL;
    }

    clear_grid(uVel);
    clear_grid(vVel);
    clear_grid(uWeight);
    clear_grid(vWeight);

    cache_particle_grid_samples();

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int cellNr = INDEX(particleGridSample[i].x0, particleGridSample[i].y0);
        cellType[cellNr] = FLUID_CELL;
    }

    for (int component = 0; component < 2; component++) {
        _iq * restrict f = (component == 0) ? uVel : vVel;
        _iq * restrict w = (component == 0) ? uWeight : vWeight;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            const ParticleGridSample *sample = &particleGridSample[i];
            int x0 = (component == 0) ? (int)sample->x0 : sample_cell(sample->x0, sample->tx);
            int y0 = (component == 0) ? sample_cell(sample->y0, sample->ty) : (int)sample->y0;
            uint16_t tx = component == 0 ? sample->tx : sample->tx + 0x8000U;
            uint16_t ty = component == 0 ? sample->ty + 0x8000U : sample->ty;
            _iq weights[4];
            bilinear_weights(tx, ty, weights);

            _iq pv = particleVel[2 * i + (unsigned int)component];
            /* 四个格子相邻，只需要一次乘法 */
            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = nr0 + CellNumY;
            unsigned int nr2 = nr1 + 1U;
            unsigned int nr3 = nr0 + 1U;

            f[nr0] += qmul(pv, weights[0]); w[nr0] += weights[0];
            f[nr1] += qmul(pv, weights[1]); w[nr1] += weights[1];
            f[nr2] += qmul(pv, weights[2]); w[nr2] += weights[2];
            f[nr3] += qmul(pv, weights[3]); w[nr3] += weights[3];
        }
        /* 只归一化可流动的面，墙面随后直接置零。 */
        unsigned int firstX = component == 0 ? 2U : 1U;
        unsigned int firstY = component == 0 ? 1U : 2U;
        for (unsigned int x = firstX; x < CellNumX - 1U; x++) {
            for (unsigned int y = firstY; y < CellNumY - 1U; y++) {
                unsigned int cell = INDEX(x, y);
                if (w[cell] > 0) f[cell] = divide_weight(f[cell], w[cell]);
            }
        }

        if (component == 0) {
            for (unsigned int n = 0; n < uSolidCount; n++) {
                unsigned int idx = uSolidCells[n];
                uVel[idx] = 0;
            }
        } else {
            for (unsigned int n = 0; n < vSolidCount; n++) {
                unsigned int idx = vSolidCells[n];
                vVel[idx] = 0;
            }
        }
    }

    /* 按网格顺序求压力，保持松弛迭代顺序。 */
    fluidCellCount = 0;
    for (unsigned int x = 1; x < CellNumX - 1U; x++) {
        for (unsigned int y = 1; y < CellNumY - 1U; y++) {
            unsigned int idx = INDEX(x, y);
            if (cellType[idx] == FLUID_CELL) {
                fluidCells[fluidCellCount++] = (uint16_t)idx;
            }
        }
    }
}

void compute_grid_forces(unsigned int nIters) {
    copy_grid(uPrev, uVel);
    copy_grid(vPrev, vVel);

    /* 权重缓冲区复用为本帧的压力系数和密度修正。 */
    for (unsigned int n = 0; n < fluidCellCount; n++) {
        unsigned int center = fluidCells[n];
        unsigned int dirs = solidDirMask[center];
        unsigned int s = (dirs & 1U) + ((dirs >> 1) & 1U) + ((dirs >> 2) & 1U) + ((dirs >> 3) & 1U);
        uWeight[center] = negOverInvSolid[s];
        _iq compression = (particleRestDensity > 0)
                        ? particleDensity[center] - particleRestDensity : 0;
        vWeight[center] = (compression > 0) ? qmul(compression, stiffnessCoefficient) : 0;
    }

    _iq * restrict cu = uVel;
    _iq * restrict cv = vVel;
    const _iq * restrict cwu = uWeight;
    const _iq * restrict cwv = vWeight;
    const uint16_t * restrict ccells = fluidCells;
    const uint8_t * restrict cmask = solidDirMask;

    for (unsigned int iter = 0; iter < nIters; iter++) {
        for (unsigned int n = 0; n < fluidCellCount; n++) {
            unsigned int center = ccells[n];
            unsigned int right = center + CellNumY;
            unsigned int top = center + 1U;

            _iq pressureScale = cwu[center];

            _iq div = cu[right] - cu[center] + cv[top] - cv[center];
            div -= cwv[center];

            _iq p = qmul(div, pressureScale);

            unsigned int dirs = cmask[center];
            if (dirs == 15U) {
                cu[center] -= p;
                cu[right] += p;
                cv[center] -= p;
                cv[top] += p;
            } else {
                if (dirs & 1U) cu[center] -= p;
                if (dirs & 2U) cu[right] += p;
                if (dirs & 4U) cv[center] -= p;
                if (dirs & 8U) cv[top] += p;
            }
        }
    }
}

void grid_to_particles(void) {
    const _iq flipRatio = _IQ(0.9);
    /* 每个面只计算一次 FLIP 修正；权重缓冲区改存有效面掩码。 */
    for (unsigned int x = 0; x < CellNumX; x++) {
        for (unsigned int y = 0; y < CellNumY; y++) {
            unsigned int cell = INDEX(x, y);
            unsigned int occupied = cellType[cell] != AIR_CELL;
            uWeight[cell] = (occupied || (x > 0 && cellType[cell - CellNumY] != AIR_CELL)) ? -1 : 0;
            vWeight[cell] = (occupied || (y > 0 && cellType[cell - 1U] != AIR_CELL)) ? -1 : 0;
            uPrev[cell] = uVel[cell] - qmul(flipRatio, uPrev[cell]);
            vPrev[cell] = vVel[cell] - qmul(flipRatio, vPrev[cell]);
        }
    }
    for (unsigned int component = 0; component < 2U; component++) {
        const _iq *restrict blended = component == 0 ? uPrev : vPrev;
        const _iq *restrict valid = component == 0 ? uWeight : vWeight;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            const ParticleGridSample *sample = &particleGridSample[i];
            int x0 = component == 0 ? sample->x0 : sample_cell(sample->x0, sample->tx);
            int y0 = component == 0 ? sample_cell(sample->y0, sample->ty) : sample->y0;
            uint16_t tx = component == 0 ? sample->tx : sample->tx + 0x8000U;
            uint16_t ty = component == 0 ? sample->ty + 0x8000U : sample->ty;
            unsigned int cell = INDEX(x0, y0);
            _iq w[4];
            bilinear_weights(tx, ty, w);
            w[0] &= valid[cell];
            w[1] &= valid[cell + CellNumY];
            w[2] &= valid[cell + CellNumY + 1U];
            w[3] &= valid[cell + 1U];
            _iq sum = w[0] + w[1] + w[2] + w[3];
            if (sum == 0) continue;
            _iq acc = qmul(w[0], blended[cell])
                    + qmul(w[1], blended[cell + CellNumY])
                    + qmul(w[2], blended[cell + CellNumY + 1U])
                    + qmul(w[3], blended[cell + 1U]);
            if (sum != _IQ(1.0)) acc = divide_weight(acc, sum);
            unsigned int id = 2U * i + component;
            particleVel[id] = acc + qmul(flipRatio, particleVel[id]);
        }
    }
}

void screen_update(void) {
    for (unsigned int x = 1; x < CellNumX - 1U; x++) {
        for (unsigned int y = 1; y < CellNumY - 1U; y++) {
            LED_SetPixel((x - 1U) * 16U + y - 1U,
                         cellType[INDEX(x, y)] == FLUID_CELL ? LEDON : LEDOFF);
        }
    }
}
