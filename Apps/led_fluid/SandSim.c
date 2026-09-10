#include "SandSim.h"
#include "charlie.h"

uint8_t screen[32];
_iq invertSpacing;

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

//static _iq pressure[CellCount];
static _iq particleDensity[CellCount];
static uint8_t solidMask[CellCount]; //1=可通过, 0=固体
static uint8_t cellType[CellCount];
static _iq particleRestDensity = 0;

/* 内联定点乘法：结果与库函数_IQ24mpy完全一致((int64)a*b>>24)，省去每次乘法的函数调用开销 */
static inline _iq qmul(_iq a, _iq b) {
    return (_iq)(((long long)a * (long long)b) >> 24);
    //return _IQmpy(a,b);
}

/* 当前nano libc的memset逐字节处理；网格天然按32位对齐。
 * 每次展开四项，使GCC保留字访问循环，避免又转换成逐字节库调用。 */
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

/* 流体单元索引表，由particles_to_grid生成，供compute_grid_forces只遍历流体单元 */
static uint16_t fluidCells[CellCount];
static unsigned int fluidCellCount = 0;

/* negOverInvSolid[s] = -overRelaxiation/s (s只可能为1~4)，在InitParticles中计算，代替压力求解里的逐次除法 */
static _iq negOverInvSolid[5];

/* PushParticlesApart计数排序两次扫描之间复用的粒子所属单元号 */
static uint16_t particleCell[NumberOfParticles];

/* 两种交错网格偏移各缓存一组坐标，供速度传输和密度计算复用。
 * particles_to_grid之后直到grid_to_particles结束，粒子位置保持不变。
 * 只缓存一维插值参数（每粒子20字节），避免保存两套四点权重。 */
typedef struct {
    _iq tx[2];
    _iq ty[2];
    uint8_t x0[2];
    uint8_t y0[2];
} ParticleGridSample;
static ParticleGridSample particleGridSample[NumberOfParticles];

/* 四个Q24双线性权重之和接近1；缓存常见分母的精确库函数结果。 */
static _iq invWeightSum[4];

static _iq minX = Spacing + ParticleRadius;
static _iq minY = Spacing + ParticleRadius;
static _iq maxX;
static _iq maxY;

#define CellPrefixCountSize (CellCount + 1U)

static uint8_t cellParticleCountPrefix[CellPrefixCountSize];
static uint8_t particlePosId[NumberOfParticles];

void printLocation(unsigned int n);

static int clamp_index(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static _iq clampf_local(_iq value, _iq min_value, _iq max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void cache_particle_grid_samples(void) {
    const _iq h = Spacing;
    const _iq h2 = qmul(_IQ(0.5), h);
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        _iq x = clampf_local(particlePos[XID(i)], h, qmul(_IQ(CellNumX - 1U), h));
        _iq y = clampf_local(particlePos[YID(i)], h, qmul(_IQ(CellNumY - 1U), h));
        ParticleGridSample *sample = &particleGridSample[i];
        for (unsigned int shifted = 0; shifted < 2U; shifted++) {
            _iq offset = shifted ? h2 : 0;
            int x0 = clamp_index(_IQint(qmul(x - offset, invertSpacing)), 0, (int)CellNumX - 2);
            int y0 = clamp_index(_IQint(qmul(y - offset, invertSpacing)), 0, (int)CellNumY - 2);
            sample->x0[shifted] = (uint8_t)x0;
            sample->y0[shifted] = (uint8_t)y0;
            sample->tx[shifted] = qmul((x - offset) - qmul(_IQ(x0), h), invertSpacing);
            sample->ty[shifted] = qmul((y - offset) - qmul(_IQ(y0), h), invertSpacing);
        }
    }
}

static void setup_solid_mask(void) {
    for (unsigned int x = 0; x < CellNumX; x++) {
        for (unsigned int y = 0; y < CellNumY; y++) {
            uint8_t s = 1U;
            if (x == 0U || x == CellNumX - 1U || y == 0U || y == CellNumY - 1U) {
                s = 0U;
            }
            solidMask[INDEX(x, y)] = s;
        }
    }
}

void InitParticles() {
    memset(particleVel, 0, sizeof(particleVel));
    memset(uVel, 0, sizeof(uVel));
    memset(vVel, 0, sizeof(vVel));
    memset(uPrev, 0, sizeof(uPrev));
    memset(vPrev, 0, sizeof(vPrev));
    memset(particleDensity, 0, sizeof(particleDensity));
    particleRestDensity = _IQ(0.0);
    setup_solid_mask();



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
    for (unsigned int loss = 0; loss < 4U; loss++) {
        invWeightSum[loss] = _IQdiv(_IQ(1.0), _IQ(1.0) - (_iq)loss);
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

void ParticleIntegrate(_iq xAcceleration, _iq yAcceleration) {
    const _iq dvx = qmul(xAcceleration, dt);
    const _iq dvy = qmul(yAcceleration, dt);
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        particleVel[XID(i)] += dvx;
        particleVel[YID(i)] += dvy;
        particlePos[XID(i)] += qmul(particleVel[XID(i)], dt);
        particlePos[YID(i)] += qmul(particleVel[YID(i)], dt);

        _iq x = particlePos[XID(i)];
        _iq y = particlePos[YID(i)];

        if (x < minX) {
            x = minX;
            particleVel[XID(i)] = qmul(particleVel[XID(i)], BOUNCYNESS);
        }
        if (x > maxX) {
            x = maxX;
            particleVel[XID(i)] = qmul(particleVel[XID(i)], BOUNCYNESS);
        }
        if (y < minY) {
            y = minY;
            particleVel[YID(i)] = qmul(particleVel[YID(i)] ,BOUNCYNESS);
        }
        if (y > maxY) {
            y = maxY;
            particleVel[YID(i)] = qmul(particleVel[YID(i)] ,BOUNCYNESS);
        }

        particlePos[XID(i)] = x;
        particlePos[YID(i)] = y;
    }
}

void PushParticlesApart(unsigned int nIters) {
    memset(cellParticleCountPrefix, 0, sizeof(cellParticleCountPrefix));

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int xi = (unsigned int)clamp_index(_IQint(qmul(particlePos[XID(i)], invertSpacing)), 0, (int)CellNumX - 1);
        unsigned int yi = (unsigned int)clamp_index(_IQint(qmul(particlePos[YID(i)], invertSpacing)), 0, (int)CellNumY - 1);
        unsigned int cellNr = INDEX(xi, yi);
        particleCell[i] = (uint16_t)cellNr;
        cellParticleCountPrefix[cellNr]++;
    }

    unsigned int prefix = 0;
    for (unsigned int i = 0; i < CellCount; i++) {
        prefix += cellParticleCountPrefix[i];
        cellParticleCountPrefix[i] = prefix;
    }
    cellParticleCountPrefix[CellCount] = prefix;

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int cellNr = particleCell[i];
        unsigned int gridIndex = --cellParticleCountPrefix[cellNr];
        particlePosId[gridIndex] = i;
    }

    const _iq minDist = qmul(_IQ(2.0), ParticleRadius);
    const _iq minDist2 = qmul(minDist, minDist);

    for (unsigned int iter = 0; iter < nIters; iter++) {
        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            _iq px = particlePos[XID(i)];
            _iq py = particlePos[YID(i)];

            int pxi = clamp_index(_IQint(qmul(px, invertSpacing)), 0, (int)CellNumX - 1);
            int pyi = clamp_index(_IQint(qmul(py, invertSpacing)), 0, (int)CellNumY - 1);
            int x0 = (pxi > 0) ? pxi - 1 : 0;
            int y0 = (pyi > 0) ? pyi - 1 : 0;
            int x1 = (pxi + 1 < (int)CellNumX) ? pxi + 1 : (int)CellNumX - 1;
            int y1 = (pyi + 1 < (int)CellNumY) ? pyi + 1 : (int)CellNumY - 1;

            for (int xi = x0; xi <= x1; xi++) {
                /* 同一列的相邻单元在计数排序结果中连续，合并遍历且保持粒子顺序。 */
                unsigned int firstIdx = cellParticleCountPrefix[INDEX((unsigned int)xi, (unsigned int)y0)];
                unsigned int lastIdx = cellParticleCountPrefix[INDEX((unsigned int)xi, (unsigned int)y1) + 1U];
                for (unsigned int j = firstIdx; j < lastIdx; j++) {
                    unsigned int id = particlePosId[j];
                    if (id == i) {
                        continue;
                    }

                    _iq qx = particlePos[XID(id)];
                    _iq qy = particlePos[YID(id)];
                    _iq dx = qx - px;
                    _iq dy = qy - py;
                    _iq d2 = qmul(dx, dx) + qmul(dy, dy);
                    if (d2 > minDist2 || d2 == _IQ(0.0)) {
                        continue;
                    }

                    _iq d = _IQsqrt(d2);
                    _iq s = _IQdiv(qmul(_IQ(0.5), (minDist - d)), d);
                    dx = qmul(dx, s);
                    dy = qmul(dy, s);
                    particlePos[XID(i)] -= dx;
                    particlePos[YID(i)] -= dy;
                    particlePos[XID(id)] += dx;
                    particlePos[YID(id)] += dy;
                }
            }
        }
    }
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        _iq x = particlePos[XID(i)];
        _iq y = particlePos[YID(i)];

        if (x < minX) {
            x = minX;
            particleVel[XID(i)] = qmul(particleVel[XID(i)], BOUNCYNESS);
        }
        if (x > maxX) {
            x = maxX;
            particleVel[XID(i)] = qmul(particleVel[XID(i)], BOUNCYNESS);
        }
        if (y < minY) {
            y = minY;
            particleVel[YID(i)] = qmul(particleVel[YID(i)] ,BOUNCYNESS);
        }
        if (y > maxY) {
            y = maxY;
            particleVel[YID(i)] = qmul(particleVel[YID(i)] ,BOUNCYNESS);
        }

        particlePos[XID(i)] = x;
        particlePos[YID(i)] = y;
    }
}

void density_update(void) {
    clear_grid(particleDensity);

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        const ParticleGridSample *sample = &particleGridSample[i];
        int x0 = sample->x0[1];
        int y0 = sample->y0[1];
        int x1 = x0 + 1;
        int y1 = y0 + 1;
        _iq tx = sample->tx[1];
        _iq ty = sample->ty[1];
        _iq sx = _IQ(1.0) - tx;
        _iq sy = _IQ(1.0) - ty;

        particleDensity[INDEX(x0, y0)] += qmul(sx, sy);
        particleDensity[INDEX(x1, y0)] += qmul(tx, sy);
        particleDensity[INDEX(x1, y1)] += qmul(tx, ty);
        particleDensity[INDEX(x0, y1)] += qmul(sx, ty);
    }

    if (particleRestDensity == _IQ(0.0)) {
        _iq sum = _IQ(0.0);
        unsigned int numFluid = 0;
        for (unsigned int i = 0; i < CellCount; i++) {
            if (cellType[i] == FLUID_CELL) {
                sum += particleDensity[i];
                numFluid++;
            }
        }
        if (numFluid > 0U) {
            particleRestDensity = _IQdiv(sum, _IQ(numFluid));
        }
    }
}

void particles_to_grid(void) {
    //memcpy(uPrev, uVel, sizeof(uVel));
    //memcpy(vPrev, vVel, sizeof(vVel));

    clear_grid(uVel);
    clear_grid(vVel);
    clear_grid(uWeight);
    clear_grid(vWeight);

    for (unsigned int i = 0; i < CellCount; i++) {
        cellType[i] = (solidMask[i] == 0U) ? SOLID_CELL : AIR_CELL;
    }

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        int xi = clamp_index(_IQint(qmul(particlePos[XID(i)], invertSpacing)), 0, (int)CellNumX - 1);
        int yi = clamp_index(_IQint(qmul(particlePos[YID(i)], invertSpacing)), 0, (int)CellNumY - 1);
        unsigned int cellNr = INDEX((unsigned int)xi, (unsigned int)yi);
        cellType[cellNr] = FLUID_CELL;
        
    }

    cache_particle_grid_samples();

    for (int component = 0; component < 2; component++) {
        _iq *f = (component == 0) ? uVel : vVel;
        _iq *w = (component == 0) ? uWeight : vWeight;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            const ParticleGridSample *sample = &particleGridSample[i];
            int x0 = sample->x0[component];
            int y0 = sample->y0[1 - component];
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            _iq tx = sample->tx[component];
            _iq ty = sample->ty[1 - component];
            _iq sx = _IQ(1.0) - tx;
            _iq sy = _IQ(1.0) - ty;

            _iq w0 = qmul(sx, sy);
            _iq w1 = qmul(tx, sy);
            _iq w2 = qmul(tx, ty);
            _iq w3 = qmul(sx, ty);

            _iq pv = particleVel[2 * i + (unsigned int)component];
            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = INDEX((unsigned int)x1, (unsigned int)y0);
            unsigned int nr2 = INDEX((unsigned int)x1, (unsigned int)y1);
            unsigned int nr3 = INDEX((unsigned int)x0, (unsigned int)y1);

            f[nr0] += qmul(pv, w0); w[nr0] += w0;
            f[nr1] += qmul(pv, w1); w[nr1] += w1;
            f[nr2] += qmul(pv, w2); w[nr2] += w2;
            f[nr3] += qmul(pv, w3); w[nr3] += w3;
        }

        for (unsigned int i = 0; i < CellCount; i++) {
            if (w[i] > _IQ(0.0)) {
                f[i] = _IQdiv(f[i], w[i]);
            }
        }

        for (unsigned int x = 0; x < CellNumX; x++) {
            for (unsigned int y = 0; y < CellNumY; y++) {
                unsigned int idx = INDEX(x, y);
                unsigned int solid = (cellType[idx] == SOLID_CELL) ? 1U : 0U;
                if (component == 0) {
                    unsigned int leftSolid = (x > 0U && cellType[INDEX(x - 1U, y)] == SOLID_CELL) ? 1U : 0U;
                    if (solid || leftSolid) {
                        uVel[idx] = uPrev[idx];
                    }
                } else {
                    unsigned int bottomSolid = (y > 0U && cellType[INDEX(x, y - 1U)] == SOLID_CELL) ? 1U : 0U;
                    if (solid || bottomSolid) {
                        vVel[idx] = vPrev[idx];
                    }
                }
            }
        }
    }

    /* 记录流体单元（与原compute_grid_forces相同的扫描顺序），压力求解只遍历这些单元 */
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
    //memset(pressure, 0, sizeof(pressure));
    memcpy(uPrev, uVel, sizeof(uVel));
    memcpy(vPrev, vVel, sizeof(vVel));

    //const _iq cp = _IQdiv(qmul(_IQ(1000.0), Spacing), dt);

    /* 权重归一化已结束，复用两个权重缓冲区保存本帧不变的压力系数和密度修正。
     * 下一次particles_to_grid会清零它们，不增加压力求解的RAM占用。 */
    for (unsigned int n = 0; n < fluidCellCount; n++) {
        unsigned int center = fluidCells[n];
        unsigned int s = solidMask[center - CellNumY] + solidMask[center + CellNumY]
                       + solidMask[center - 1U] + solidMask[center + 1U];
        uWeight[center] = negOverInvSolid[s];
        _iq compression = (particleRestDensity > 0)
                        ? particleDensity[center] - particleRestDensity : 0;
        vWeight[center] = (compression > 0) ? qmul(compression, stiffnessCoefficient) : 0;
    }

    for (unsigned int iter = 0; iter < nIters; iter++) {
        /* 只遍历流体单元，索引在particles_to_grid中按原有扫描顺序生成，松弛迭代顺序不变 */
        for (unsigned int n = 0; n < fluidCellCount; n++) {
            unsigned int center = fluidCells[n];
            unsigned int left = center - CellNumY;
            unsigned int right = center + CellNumY;
            unsigned int bottom = center - 1U;
            unsigned int top = center + 1U;

            unsigned int sx0 = solidMask[left];
            unsigned int sx1 = solidMask[right];
            unsigned int sy0 = solidMask[bottom];
            unsigned int sy1 = solidMask[top];
            _iq pressureScale = uWeight[center];
            if (pressureScale == 0) {
                continue;
            }

            _iq div = uVel[right] - uVel[center] + vVel[top] - vVel[center];
            div -= vWeight[center];

            /* 原式 p = -_IQdiv(div, s) * overRelaxiation；s只可能是1~4，查表把除法换成一次乘法 */
            _iq p = qmul(div, pressureScale);
            //pressure[center] += qmul(cp, p);

            /* solidMask取值为0或1，原来的_IQmpy(sx, p)等价于条件加减 */
            if (sx0 != 0U) {
                uVel[center] -= p;
            }
            if (sx1 != 0U) {
                uVel[right] += p;
            }
            if (sy0 != 0U) {
                vVel[center] -= p;
            }
            if (sy1 != 0U) {
                vVel[top] += p;
            }
        }
    }
}

void grid_to_particles(void) {
    const _iq flipRatio = _IQ(0.9);
    for (int component = 0; component < 2; component++) {
        _iq *f = (component == 0) ? uVel : vVel;
        _iq *prevF = (component == 0) ? uPrev : vPrev;
        int offset = (component == 0) ? (int)CellNumY : 1;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            const ParticleGridSample *sample = &particleGridSample[i];
            int x0 = sample->x0[component];
            int y0 = sample->y0[1 - component];
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            _iq tx = sample->tx[component];
            _iq ty = sample->ty[1 - component];
            _iq sx = _IQ(1.0) - tx;
            _iq sy = _IQ(1.0) - ty;

            _iq d0 = qmul(sx, sy);
            _iq d1 = qmul(tx, sy);
            _iq d2 = qmul(tx, ty);
            _iq d3 = qmul(sx, ty);

            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = INDEX((unsigned int)x1, (unsigned int)y0);
            unsigned int nr2 = INDEX((unsigned int)x1, (unsigned int)y1);
            unsigned int nr3 = INDEX((unsigned int)x0, (unsigned int)y1);

            //条件加法比乘1快
            _iq d = 0;
            _iq picNum = 0;
            _iq corrNum = 0;
            if (cellType[nr0] != AIR_CELL || ((int)nr0 - offset >= 0 && cellType[(unsigned int)((int)nr0 - offset)] != AIR_CELL)) {
                d += d0;
                picNum += qmul(d0, f[nr0]);
                corrNum += qmul(d0, (f[nr0] - prevF[nr0]));
            }
            if (cellType[nr1] != AIR_CELL || ((int)nr1 - offset >= 0 && cellType[(unsigned int)((int)nr1 - offset)] != AIR_CELL)) {
                d += d1;
                picNum += qmul(d1, f[nr1]);
                corrNum += qmul(d1, (f[nr1] - prevF[nr1]));
            }
            if (cellType[nr2] != AIR_CELL || ((int)nr2 - offset >= 0 && cellType[(unsigned int)((int)nr2 - offset)] != AIR_CELL)) {
                d += d2;
                picNum += qmul(d2, f[nr2]);
                corrNum += qmul(d2, (f[nr2] - prevF[nr2]));
            }
            if (cellType[nr3] != AIR_CELL || ((int)nr3 - offset >= 0 && cellType[(unsigned int)((int)nr3 - offset)] != AIR_CELL)) {
                d += d3;
                picNum += qmul(d3, f[nr3]);
                corrNum += qmul(d3, (f[nr3] - prevF[nr3]));
            }
            if (d <= _IQ(0.0)) {
                continue;
            }

            // 先算D倒数
            unsigned int loss = (unsigned int)(_IQ(1.0) - d);
            _iq invD = (loss < 4U) ? invWeightSum[loss] : _IQdiv(_IQ(1.0), d);
            _iq picV = qmul(picNum, invD);
            _iq corr = qmul(corrNum, invD);
            _iq oldV = particleVel[2 * i + (unsigned int)component];
            _iq flipV = oldV + corr;
            particleVel[2 * i + (unsigned int)component] = qmul((_IQ(1.0) - flipRatio), picV) + qmul(flipRatio, flipV);
        }
    }
}

void screen_update() {
    //显示

    // 根据网格类型直接显示
    for (int y = 1; y < CellNumY-1; y++) {
        for (int x = 1; x < CellNumX-1; x++) {
            uint32_t cell = cellType[INDEX(x, y)];
            uint16_t pixel = (uint16_t)((x - 1) * 16 + (y - 1));
            if (cell == FLUID_CELL || cell == SOLID_CELL) {
                LED_SetPixel(pixel,LEDON);
            } else {
                LED_SetPixel(pixel,LEDOFF);
            }
        }
    }

}

