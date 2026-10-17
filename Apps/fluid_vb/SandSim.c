<<<<<<< HEAD
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

=======
#include "SandSim.h"
#include "charlie.h"

#pragma GCC optimize("O3,unroll-loops,rename-registers,ira-loop-pressure")

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

//内联定点乘法
static inline _iq qmul(_iq a, _iq b) {
    return (_iq)(((long long)a * (long long)b) >> 24);
    //return _IQmpy(a,b);
}

//memset逐字节
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

/* 固体边界单元索引表：particles_to_grid里只有这些单元需要回填速度，
 * 预先生成后就不必每帧扫描整个网格。
 * 上界：固体单元 2*(X+Y-2) 个，加上紧贴固体的一列/一行，取 3*(X+Y) 足够。 */
#define SolidListMax (3U * (CellNumX + CellNumY))
static uint16_t uSolidCells[SolidListMax];
static uint16_t vSolidCells[SolidListMax];
static unsigned int uSolidCount = 0;
static unsigned int vSolidCount = 0;

/* 每个单元的四个邻格哪些可以通行（与solidMask同义：1=可通行），
 * 位0=左 位1=右 位2=下 位3=上。压力求解里用一次查表代替四次 solidMask 读取。 */
static uint8_t solidDirMask[CellCount];

/* 单元坐标乘Spacing的查表值，省去逐粒子重复乘法 */
static _iq cellOffsetX[CellNumX];
static _iq cellOffsetY[CellNumY];

/* 粒子对的最小间距，InitParticles里按 ParticleRadius 计算 */
static _iq minDist;
static _iq minDist2;

/* 粒子对修正需要 w = minDist/d（d=sqrt(d2)），位移比例是 0.5*(w-1)。
 * 库里的 _IQ24sqrt(约52条)+_IQ24div_FAST(约85条)太贵；这里按 d2 线性分档查表
 * 得到 w 的种子，再做一次牛顿迭代，约50条。w 只在 [1, 4.4] 之间，全程不会溢出。
 * 表在 InitParticles 里用库函数生成，随 Spacing/半径自动适配。 */
#define RSQRT_SHIFT      7U                      /* 每档 128 个 Q24 单位 */
#define RSQRT_ENTRIES    (160U)                  /* 固定档数，每档宽度按参数自动选 */
static _iq ratioTab[RSQRT_ENTRIES];              /* minDist/d，Q24 */
static _iq ratioXMin;                           /* 表起点；d2 更小则退回库函数 */
static unsigned int ratioEntries;                /* 实际使用的档数 */
static unsigned int ratioShift;                  /* 每档的 d2 步长 = 2^ratioShift */
static _iq invMinDist2Q;                         /* (1/minDist²)*2^qScale */
static unsigned int qScale;                      /* 上面的缩放位数，按参数自动选，保证不溢出 */

/* 返回 w = minDist/d 的Q24近似 */
static inline _iq qdist_ratio(_iq x) {
    unsigned int t = (unsigned int)(x - ratioXMin);
    unsigned int i = t >> ratioShift;
    _iq w0, w1, w, fracq;

    if (i >= ratioEntries - 1U) {                /* 参数改动后极端值也不会越界 */
        i = ratioEntries - 2U;
        t = (1U << ratioShift) - 1U;
    }
    fracq = (_iq)((t & ((1U << ratioShift) - 1U)) << (24U - ratioShift));
    w0 = ratioTab[i];
    w1 = ratioTab[i + 1U];
    w = w0 + qmul(w1 - w0, fracq);

    /* 牛顿迭代：w = w*(1.5 - 0.5*d2*w*w/minDist²) */
    _iq r = qmul(qmul(x, qmul(w, w)), invMinDist2Q) << (24U - qScale);
    return qmul(w, _IQ(1.5) - qmul(_IQ(0.5), r));
}

/* 归一化除法用的 1/w 查询表：w 是双线性权重之和，落在 (0,4]。
 * 按 0.0625 一档存每档中心的倒数，两次牛顿迭代后相对误差约1e-5；
 * w<0.5 的稀疏格子仍走库函数。 */
#define WRECIP_SHIFT     20U                     /* 0.0625 在Q24里是 2^20 */
#define WRECIP_ENTRIES   (64U)
static _iq wRecipTab[WRECIP_ENTRIES];

static inline _iq qrecip_pos(_iq w) {
    unsigned int i = (unsigned int)(w >> WRECIP_SHIFT);
    _iq r;
    if (i >= WRECIP_ENTRIES) {
        i = WRECIP_ENTRIES - 1U;
    }
    r = wRecipTab[i];
    r = qmul(r, _IQ(2.0) - qmul(w, r));
    r = qmul(r, _IQ(2.0) - qmul(w, r));
    return r;
}

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

        /* 不带偏移的那套直接算 */
        int x0 = clamp_index(_IQint(qmul(x, invertSpacing)), 0, (int)CellNumX - 2);
        int y0 = clamp_index(_IQint(qmul(y, invertSpacing)), 0, (int)CellNumY - 2);
        _iq fx = x - cellOffsetX[x0];          /* 0 <= fx < h */
        _iq fy = y - cellOffsetY[y0];
        sample->x0[0] = (uint8_t)x0;
        sample->y0[0] = (uint8_t)y0;
        sample->tx[0] = qmul(fx, invertSpacing);
        sample->ty[0] = qmul(fy, invertSpacing);

        /* 半格偏移的那套：坐标减 h/2 后，格号只会是本格或前一格，
         * 由小数部分和 h/2 的大小关系直接决定，省掉两次乘法和钳位。 */
        int x1 = x0;
        int y1 = y0;
        if (fx < h2 && x0 > 0) {
            x1 = x0 - 1;
            fx += h;
        }
        if (fy < h2 && y0 > 0) {
            y1 = y0 - 1;
            fy += h;
        }
        sample->x0[1] = (uint8_t)x1;
        sample->y0[1] = (uint8_t)y1;
        sample->tx[1] = qmul(fx - h2, invertSpacing);
        sample->ty[1] = qmul(fy - h2, invertSpacing);
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

    /* 网格布局是静态的：单元类型、邻格可通行掩码、边界单元表只在这里算一次 */
    for (unsigned int x = 0; x < CellNumX; x++) {
        cellOffsetX[x] = qmul(_IQ(x), Spacing);
    }
    for (unsigned int y = 0; y < CellNumY; y++) {
        cellOffsetY[y] = qmul(_IQ(y), Spacing);
    }

    /* 粒子对间距，以及 w=minDist/d 的查表（种子足够准，一次牛顿迭代即收敛） */
    minDist = qmul(_IQ(2.0), ParticleRadius);
    minDist2 = qmul(minDist, minDist);
    ratioXMin = minDist2 >> 4;          /* 对应 d=minDist/4，此时 w=4，Q24装得下 */
    if (minDist2 > ratioXMin) {
        unsigned int range = (unsigned int)(minDist2 - ratioXMin);
        ratioShift = 0;
        while ((range >> ratioShift) >= (RSQRT_ENTRIES - 2U)) {
            ratioShift++;
        }
        ratioEntries = (range >> ratioShift) + 2U;
        for (unsigned int i = 0; i < ratioEntries; i++) {
            _iq x = ratioXMin + (_iq)(i << ratioShift);
            ratioTab[i] = _IQdiv(minDist, _IQsqrt(x));
        }
        /* 选缩放位数，使 (1/minDist²)*2^qScale 落在 int32 内 */
        qScale = 24U;
        while (qScale > 0U && (((long long)1 << (qScale + 24U)) / minDist2) > 0x7FFFFFFFLL) {
            qScale--;
        }
        invMinDist2Q = _IQdiv((_iq)(1L << qScale), minDist2);   /* = (1/minDist²)*2^qScale */
    } else {
        ratioXMin = 0x7FFFFFFF;         /* 参数太极端就全部走库函数分支 */
        ratioShift = 1U;
        ratioEntries = 2U;
    }

    /* 1/w 查询表（归一化除法用） */
    for (unsigned int i = 0; i < WRECIP_ENTRIES; i++) {
        _iq wc = (_iq)((i << WRECIP_SHIFT) + (1L << (WRECIP_SHIFT - 1U)));
        wRecipTab[i] = _IQdiv(_IQ(1.0), wc);
    }

    uSolidCount = 0;
    vSolidCount = 0;
    for (unsigned int x = 0; x < CellNumX; x++) {
        for (unsigned int y = 0; y < CellNumY; y++) {
            unsigned int idx = INDEX(x, y);
            uint8_t solidHere = (solidMask[idx] == 0U);
            uint8_t leftSolid = (x > 0U && solidMask[INDEX(x - 1U, y)] == 0U);
            uint8_t rightSolid = (x + 1U < CellNumX && solidMask[INDEX(x + 1U, y)] == 0U);
            uint8_t bottomSolid = (y > 0U && solidMask[INDEX(x, y - 1U)] == 0U);
            uint8_t topSolid = (y + 1U < CellNumY && solidMask[INDEX(x, y + 1U)] == 0U);

            cellType[idx] = solidHere ? SOLID_CELL : AIR_CELL;
            if (solidHere || leftSolid) {
                uSolidCells[uSolidCount++] = (uint16_t)idx;
            }
            if (solidHere || bottomSolid) {
                vSolidCells[vSolidCount++] = (uint16_t)idx;
            }
            /* 注意与solidMask同义：位为1表示该方向可通行 */
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
    for (unsigned int iter = 0; iter < nIters; iter++) {
        /* 每对粒子只处理一次（id > i）：原实现对 (i,j) 和 (j,i) 各扫一遍，
         * 第二次通常已经在minDist附近、几乎不产生位移，却仍要付距离测试和
         * sqrt/div 的开销。改成单侧后一帧的调用次数直接减半。 */
        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            _iq px = particlePos[XID(i)];
            _iq py = particlePos[YID(i)];

            /* 固定扫3x3会扫进很多不可能重叠的格子（cell 0.05 > minDist 0.034）。
             * 按 ±minDist 算出真正可能相交的格子范围：先求本格，再看格内小数位置
             * 决定要不要往两边多扫一格，比再乘两次省事。 */
            int pxi = _IQint(qmul(px, invertSpacing));
            int pyi = _IQint(qmul(py, invertSpacing));
            _iq fx = px - cellOffsetX[pxi];
            _iq fy = py - cellOffsetY[pyi];
            int x0 = pxi - ((fx < minDist) ? 1 : 0);
            int x1 = pxi + ((fx >= Spacing - minDist) ? 1 : 0);
            int y0 = pyi - ((fy < minDist) ? 1 : 0);
            int y1 = pyi + ((fy >= Spacing - minDist) ? 1 : 0);

            for (int xi = x0; xi <= x1; xi++) {
                /* 同一列的相邻单元在计数排序结果中连续，合并遍历且保持粒子顺序。 */
                unsigned int firstIdx = cellParticleCountPrefix[INDEX((unsigned int)xi, (unsigned int)y0)];
                unsigned int lastIdx = cellParticleCountPrefix[INDEX((unsigned int)xi, (unsigned int)y1) + 1U];
                for (unsigned int j = firstIdx; j < lastIdx; j++) {
                    unsigned int id = particlePosId[j];
                    if (id <= i) {
                        continue;
                    }

                    _iq qx = particlePos[XID(id)];
                    _iq qy = particlePos[YID(id)];
                    _iq dx = qx - px;
                    _iq dy = qy - py;
                    /* 包围盒先行排除：|dx|或|dy|超过minDist时必定不相交。
                     * 平移minDist后一次无符号比较就能判掉两个方向。 */
                    if ((unsigned int)(dx + minDist) > (unsigned int)(minDist + minDist)
                        || (unsigned int)(dy + minDist) > (unsigned int)(minDist + minDist)) {
                        continue;
                    }
                    _iq d2 = qmul(dx, dx) + qmul(dy, dy);
                    if (d2 > minDist2 || d2 == _IQ(0.0)) {
                        continue;
                    }

                    /* 0.5是因为一对粒子各移动一半。用 minDist/d 的查表结果直接算
                     * s = 0.5*(w - 1)，省掉 sqrt+div；d2 太小时退回库函数。 */
                    _iq s;
                    if (d2 >= ratioXMin) {
                        s = qmul(_IQ(0.5), qdist_ratio(d2) - _IQ(1.0));
                    } else {
                        _iq d = _IQsqrt(d2);
                        s = _IQdiv(qmul(_IQ(0.5), (minDist - d)), d);
                    }
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

    /* 上一帧标成流体的单元改回空气：固体单元和其余空气单元本来就没被动过，
     * 不必每帧按solidMask重扫整个网格。 */
    for (unsigned int i = 0; i < fluidCellCount; i++) {
        cellType[fluidCells[i]] = AIR_CELL;
    }

    /* 四个缓冲区分别清零：编译器会把每个循环展开成整字写入，比合并遍历更快 */
    clear_grid(uVel);
    clear_grid(vVel);
    clear_grid(uWeight);
    clear_grid(vWeight);

    /* 采样缓存先算，标记流体单元直接复用它的格号：粒子位置被钳在 [minX,maxX] 内，
     * 格号不会落到最后一行/列，与原来 clamp 到 CellNumX-1 的结果一致 */
    cache_particle_grid_samples();

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int cellNr = INDEX(particleGridSample[i].x0[0], particleGridSample[i].y0[0]);
        cellType[cellNr] = FLUID_CELL;
    }

    for (int component = 0; component < 2; component++) {
        /* restrict：f 和 w 是不同数组，编译器可以放心把加载提到循环外 */
        _iq * restrict f = (component == 0) ? uVel : vVel;
        _iq * restrict w = (component == 0) ? uWeight : vWeight;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            const ParticleGridSample *sample = &particleGridSample[i];
            int x0 = sample->x0[component];
            int y0 = sample->y0[1 - component];
            //int x1 = x0 + 1;
            //int y1 = y0 + 1;
            _iq tx = sample->tx[component];
            _iq ty = sample->ty[1 - component];
            _iq sy = _IQ(1.0) - ty;

            /* w0 = sx*sy = sy - w1，w3 = sx*ty = ty - w2，只需两次乘法 */
            _iq w1 = qmul(tx, sy);
            _iq w2 = qmul(tx, ty);
            _iq w0 = sy - w1;
            _iq w3 = ty - w2;

            _iq pv = particleVel[2 * i + (unsigned int)component];
            /* 四个格子相邻，只需要一次乘法 */
            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = nr0 + CellNumY;
            unsigned int nr2 = nr1 + 1U;
            unsigned int nr3 = nr0 + 1U;

            f[nr0] += qmul(pv, w0); w[nr0] += w0;
            f[nr1] += qmul(pv, w1); w[nr1] += w1;
            f[nr2] += qmul(pv, w2); w[nr2] += w2;
            f[nr3] += qmul(pv, w3); w[nr3] += w3;
        }
        for (unsigned int i = 0; i < CellCount; i++) {
            _iq wv = w[i];
            if (wv > _IQ(0.0)) {
                if (wv >= _IQ(0.5)) {
                    f[i] = qmul(f[i], qrecip_pos(wv));
                } else {
                    f[i] = _IQdiv(f[i], wv);
                }
            }
        }

        /* 只在固体边界单元上回填速度，表在InitParticles里按同一顺序生成 */
        if (component == 0) {
            for (unsigned int n = 0; n < uSolidCount; n++) {
                unsigned int idx = uSolidCells[n];
                uVel[idx] = uPrev[idx];
            }
        } else {
            for (unsigned int n = 0; n < vSolidCount; n++) {
                unsigned int idx = vSolidCells[n];
                vVel[idx] = vPrev[idx];
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
        /* solidDirMask一位代表一个可通行邻格，popcount就是原来的邻格累加 */
        unsigned int dirs = solidDirMask[center];
        unsigned int s = (dirs & 1U) + ((dirs >> 1) & 1U) + ((dirs >> 2) & 1U) + ((dirs >> 3) & 1U);
        uWeight[center] = negOverInvSolid[s];
        _iq compression = (particleRestDensity > 0)
                        ? particleDensity[center] - particleRestDensity : 0;
        vWeight[center] = (compression > 0) ? qmul(compression, stiffnessCoefficient) : 0;
    }

    /* restrict 告诉编译器这几个数组互不重叠，便于把循环里的加载提到外面 */
    _iq * restrict cu = uVel;
    _iq * restrict cv = vVel;
    const _iq * restrict cwu = uWeight;
    const _iq * restrict cwv = vWeight;
    const uint16_t * restrict ccells = fluidCells;
    const uint8_t * restrict cmask = solidDirMask;

    for (unsigned int iter = 0; iter < nIters; iter++) {
        /* 只遍历流体单元，索引在particles_to_grid中按原有扫描顺序生成，松弛迭代顺序不变 */
        for (unsigned int n = 0; n < fluidCellCount; n++) {
            unsigned int center = ccells[n];
            unsigned int right = center + CellNumY;
            unsigned int top = center + 1U;

            _iq pressureScale = cwu[center];
            /* 内部流体单元至少有一个可通行邻格，pressureScale 不可能为0，省掉判断 */

            _iq div = cu[right] - cu[center] + cv[top] - cv[center];
            div -= cwv[center];

            /* 原式 p = -_IQdiv(div, s) * overRelaxiation；s只可能是1~4，查表把除法换成一次乘法 */
            _iq p = qmul(div, pressureScale);
            //pressure[center] += qmul(cp, p);

            /* 位0=左 位1=右 位2=下 位3=上，为1表示该方向可通行，与solidMask一致 */
            unsigned int dirs = cmask[center];
            if (dirs & 1U) {
                cu[center] -= p;
            }
            if (dirs & 2U) {
                cu[right] += p;
            }
            if (dirs & 4U) {
                cv[center] -= p;
            }
            if (dirs & 8U) {
                cv[top] += p;
            }
        }
    }
}

void grid_to_particles(void) {
    const _iq flipRatio = _IQ(0.9);
    for (int component = 0; component < 2; component++) {
        _iq * restrict f = (component == 0) ? uVel : vVel;
        const _iq * restrict prevF = (component == 0) ? uPrev : vPrev;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            const ParticleGridSample *sample = &particleGridSample[i];
            int x0 = sample->x0[component];
            int y0 = sample->y0[1 - component];
            //int x1 = x0 + 1;
            //int y1 = y0 + 1;
            _iq tx = sample->tx[component];
            _iq ty = sample->ty[1 - component];
            _iq sx = _IQ(1.0) - tx;
            _iq sy = _IQ(1.0) - ty;

            _iq d0 = qmul(sx, sy);
            _iq d1 = qmul(tx, sy);
            _iq d2 = qmul(tx, ty);
            _iq d3 = qmul(sx, ty);

            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = nr0 + CellNumY;
            unsigned int nr2 = nr1 + 1U;
            unsigned int nr3 = nr0 + 1U;

            /* 每个格子要检查的"迎风"邻居偏移：左/下邻格存在时才回退，
             * 否则用0偏移（等于再测一次本格，结果与原判断等价），
             * 这样省掉每格一次 (int)nr - offset >= 0 的比较。 */
            unsigned int o0, o1, o2, o3;
            if (component == 0) {
                o0 = (x0 > 0) ? (unsigned int)CellNumY : 0U;
                o1 = (unsigned int)CellNumY;
                o2 = (unsigned int)CellNumY;
                o3 = o0;
            } else {
                o0 = (y0 > 0) ? 1U : 0U;
                o1 = o0;
                o2 = 1U;
                o3 = 1U;
            }

            //条件加法比乘1快
            _iq d = 0;
            _iq picNum = 0;
            _iq corrNum = 0;
            if (cellType[nr0] != AIR_CELL || cellType[nr0 - o0] != AIR_CELL) {
                d += d0;
                picNum += qmul(d0, f[nr0]);
                corrNum += qmul(d0, (f[nr0] - prevF[nr0]));
            }
            if (cellType[nr1] != AIR_CELL || cellType[nr1 - o1] != AIR_CELL) {
                d += d1;
                picNum += qmul(d1, f[nr1]);
                corrNum += qmul(d1, (f[nr1] - prevF[nr1]));
            }
            if (cellType[nr2] != AIR_CELL || cellType[nr2 - o2] != AIR_CELL) {
                d += d2;
                picNum += qmul(d2, f[nr2]);
                corrNum += qmul(d2, (f[nr2] - prevF[nr2]));
            }
            if (cellType[nr3] != AIR_CELL || cellType[nr3 - o3] != AIR_CELL) {
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
            x--;
            y--;
            if (cell == FLUID_CELL) {
                LED_SetPixel(x*16+y,LEDON);
            } else if (cell == SOLID_CELL) {
                LED_SetPixel(x*16+y,LEDON);
            } else {
                LED_SetPixel(x*16+y,LEDOFF);
            }
            x++;
            y++;
            
        }
    }    
}

>>>>>>> 42aa53b (2.7mA)
