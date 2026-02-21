#include "SandSim.h"

char visual_buffer[CellNumY][CellNumX+1];
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
static _iq solidMask[CellCount];
static uint8_t cellType[CellCount];
static _iq particleRestDensity = 0;

static _iq minX = Spacing + ParticleRadius;
static _iq minY = Spacing + ParticleRadius;
static _iq maxX;
static _iq maxY;

#define CellPrefixCountSize (CellCount + 1U)

static unsigned int cellParticleCountPrefix[CellPrefixCountSize];
static unsigned int particlePosId[NumberOfParticles];

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

static int iq_floor_to_int(_iq value) {
    return (int)(value >> GLOBAL_Q);
}

static void setup_solid_mask(void) {
    for (unsigned int x = 0; x < CellNumX; x++) {
        for (unsigned int y = 0; y < CellNumY; y++) {
            _iq s = _IQ(1);
            if (x == 0U || x == CellNumX - 1U || y == 0U || y == CellNumY - 1U) {
                s = _IQ(0.0);
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
    //memset(pressure, 0, sizeof(pressure));
    memset(particleDensity, 0, sizeof(particleDensity));
    particleRestDensity = _IQ(0.0);
    setup_solid_mask();



    const _iq h = Spacing;
    const _iq r = ParticleRadius;
    const _iq dx = _IQmpy(_IQ(2.0), r);
    const _iq dy = _IQmpy(_IQ(0.86602540378), dx);
    invertSpacing = _IQdiv(_IQ(1.0), Spacing);
    maxX = _IQmpy(_IQ(CellNumX - 1U), Spacing) - ParticleRadius;
    maxY = _IQmpy(_IQ(CellNumY - 1U), Spacing) - ParticleRadius;

    unsigned int p_num = 0;
    for (unsigned int i = 0; i < CellNumX && p_num < NumberOfParticles; i++) {
        for (unsigned int j = 0; j < CellNumY && p_num < NumberOfParticles; j++) {
            _iq px = h + r + _IQmpy(dx, _IQ(i)) + ((j % 2U == 0U) ? _IQ(0.0) : r);
            _iq py = h + r + _IQmpy(dy, _IQ(j));
            if (px > _IQmpy(_IQ(CellNumX - 1U), h) - r || py > _IQmpy(_IQ(CellNumY - 1U), h) - r) {
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


    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        particleVel[XID(i)] += _IQmpy(xAcceleration, dt);
        particleVel[YID(i)] += _IQmpy(yAcceleration, dt);
        particlePos[XID(i)] += _IQmpy(particleVel[XID(i)], dt);
        particlePos[YID(i)] += _IQmpy(particleVel[YID(i)], dt);

        _iq x = particlePos[XID(i)];
        _iq y = particlePos[YID(i)];

        if (x < minX) {
            x = minX;
            particleVel[XID(i)] = _IQmpy(particleVel[XID(i)], BOUNCYNESS);
        }
        if (x > maxX) {
            x = maxX;
            particleVel[XID(i)] = _IQmpy(particleVel[XID(i)], BOUNCYNESS);
        }
        if (y < minY) {
            y = minY;
            particleVel[YID(i)] = _IQmpy(particleVel[YID(i)] ,BOUNCYNESS);
        }
        if (y > maxY) {
            y = maxY;
            particleVel[YID(i)] = _IQmpy(particleVel[YID(i)] ,BOUNCYNESS);
        }

        particlePos[XID(i)] = x;
        particlePos[YID(i)] = y;
    }
}

void PushParticlesApart(unsigned int nIters) {
    memset(cellParticleCountPrefix, 0, sizeof(cellParticleCountPrefix));
    memset(particlePosId, 0, sizeof(particlePosId));

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int xi = (unsigned int)clamp_index(iq_floor_to_int(_IQmpy(particlePos[XID(i)], invertSpacing)), 0, (int)CellNumX - 1);
        unsigned int yi = (unsigned int)clamp_index(iq_floor_to_int(_IQmpy(particlePos[YID(i)], invertSpacing)), 0, (int)CellNumY - 1);
        cellParticleCountPrefix[INDEX(xi, yi)]++;
    }

    unsigned int prefix = 0;
    for (unsigned int i = 0; i < CellCount; i++) {
        prefix += cellParticleCountPrefix[i];
        cellParticleCountPrefix[i] = prefix;
    }
    cellParticleCountPrefix[CellCount] = prefix;

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        unsigned int xi = (unsigned int)clamp_index(iq_floor_to_int(_IQmpy(particlePos[XID(i)], invertSpacing)), 0, (int)CellNumX - 1);
        unsigned int yi = (unsigned int)clamp_index(iq_floor_to_int(_IQmpy(particlePos[YID(i)], invertSpacing)), 0, (int)CellNumY - 1);
        unsigned int cellNr = INDEX(xi, yi);
        unsigned int gridIndex = --cellParticleCountPrefix[cellNr];
        particlePosId[gridIndex] = i;
    }

    const _iq minDist = _IQmpy(_IQ(2.0), ParticleRadius);
    const _iq minDist2 = _IQmpy(minDist, minDist);

    for (unsigned int iter = 0; iter < nIters; iter++) {
        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            _iq px = particlePos[XID(i)];
            _iq py = particlePos[YID(i)];

            int pxi = clamp_index(iq_floor_to_int(_IQmpy(px, invertSpacing)), 0, (int)CellNumX - 1);
            int pyi = clamp_index(iq_floor_to_int(_IQmpy(py, invertSpacing)), 0, (int)CellNumY - 1);
            int x0 = (pxi > 0) ? pxi - 1 : 0;
            int y0 = (pyi > 0) ? pyi - 1 : 0;
            int x1 = (pxi + 1 < (int)CellNumX) ? pxi + 1 : (int)CellNumX - 1;
            int y1 = (pyi + 1 < (int)CellNumY) ? pyi + 1 : (int)CellNumY - 1;

            for (int xi = x0; xi <= x1; xi++) {
                for (int yi = y0; yi <= y1; yi++) {
                    unsigned int cellNr = INDEX((unsigned int)xi, (unsigned int)yi);
                    unsigned int firstIdx = cellParticleCountPrefix[cellNr];
                    unsigned int lastIdx = cellParticleCountPrefix[cellNr + 1U];
                    for (unsigned int j = firstIdx; j < lastIdx; j++) {
                        unsigned int id = particlePosId[j];
                        if (id == i) {
                            continue;
                        }

                        _iq qx = particlePos[XID(id)];
                        _iq qy = particlePos[YID(id)];
                        _iq dx = qx - px;
                        _iq dy = qy - py;
                        _iq d2 = _IQmpy(dx, dx) + _IQmpy(dy, dy);
                        if (d2 > minDist2 || d2 == _IQ(0.0)) {
                            continue;
                        }

                        _iq d = _IQsqrt(d2);
                        _iq s = _IQdiv(_IQmpy(_IQ(0.5), (minDist - d)), d);
                        dx = _IQmpy(dx, s);
                        dy = _IQmpy(dy, s);
                        particlePos[XID(i)] -= dx;
                        particlePos[YID(i)] -= dy;
                        particlePos[XID(id)] += dx;
                        particlePos[YID(id)] += dy;
                    }
                }
            }
        }
    }
    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        _iq x = particlePos[XID(i)];
        _iq y = particlePos[YID(i)];

        if (x < minX) {
            x = minX;
            particleVel[XID(i)] = _IQmpy(particleVel[XID(i)], BOUNCYNESS);
        }
        if (x > maxX) {
            x = maxX;
            particleVel[XID(i)] = _IQmpy(particleVel[XID(i)], BOUNCYNESS);
        }
        if (y < minY) {
            y = minY;
            particleVel[YID(i)] = _IQmpy(particleVel[YID(i)] ,BOUNCYNESS);
        }
        if (y > maxY) {
            y = maxY;
            particleVel[YID(i)] = _IQmpy(particleVel[YID(i)] ,BOUNCYNESS);
        }

        particlePos[XID(i)] = x;
        particlePos[YID(i)] = y;
    }
}

void density_update(void) {
    memset(particleDensity, 0, sizeof(particleDensity));

    const _iq h = Spacing;
    const _iq h1 = invertSpacing;
    const _iq h2 = _IQmpy(_IQ(0.5), h);

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        _iq x = clampf_local(particlePos[XID(i)], h, _IQmpy(_IQ(CellNumX - 1U), h));
        _iq y = clampf_local(particlePos[YID(i)], h, _IQmpy(_IQ(CellNumY - 1U), h));

        int x0 = iq_floor_to_int(_IQmpy((x - h2), h1));
        int y0 = iq_floor_to_int(_IQmpy((y - h2), h1));
        x0 = clamp_index(x0, 0, (int)CellNumX - 2);
        y0 = clamp_index(y0, 0, (int)CellNumY - 2);
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        _iq tx = _IQmpy(((x - h2) - _IQmpy(_IQ(x0), h)), h1);
        _iq ty = _IQmpy(((y - h2) - _IQmpy(_IQ(y0), h)), h1);
        _iq sx = _IQ(1.0) - tx;
        _iq sy = _IQ(1.0) - ty;

        particleDensity[INDEX(x0, y0)] += _IQmpy(sx, sy);
        particleDensity[INDEX(x1, y0)] += _IQmpy(tx, sy);
        particleDensity[INDEX(x1, y1)] += _IQmpy(tx, ty);
        particleDensity[INDEX(x0, y1)] += _IQmpy(sx, ty);
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
    memcpy(uPrev, uVel, sizeof(uVel));
    memcpy(vPrev, vVel, sizeof(vVel));

    memset(uVel, 0, sizeof(uVel));
    memset(vVel, 0, sizeof(vVel));
    memset(uWeight, 0, sizeof(uWeight));
    memset(vWeight, 0, sizeof(vWeight));

    for (unsigned int i = 0; i < CellCount; i++) {
        cellType[i] = (solidMask[i] == _IQ(0.0)) ? SOLID_CELL : AIR_CELL;
    }

    for (unsigned int i = 0; i < NumberOfParticles; i++) {
        int xi = clamp_index(iq_floor_to_int(_IQmpy(particlePos[XID(i)], invertSpacing)), 0, (int)CellNumX - 1);
        int yi = clamp_index(iq_floor_to_int(_IQmpy(particlePos[YID(i)], invertSpacing)), 0, (int)CellNumY - 1);
        unsigned int cellNr = INDEX((unsigned int)xi, (unsigned int)yi);
        cellType[cellNr] = FLUID_CELL;
        
    }

    const _iq h = Spacing;
    const _iq h1 = invertSpacing;
    const _iq h2 = _IQmpy(_IQ(0.5), h);

    for (int component = 0; component < 2; component++) {
        _iq dx = (component == 0) ? _IQ(0.0) : h2;
        _iq dy = (component == 0) ? h2 : _IQ(0.0);
        _iq *f = (component == 0) ? uVel : vVel;
        _iq *w = (component == 0) ? uWeight : vWeight;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            _iq x = clampf_local(particlePos[XID(i)], h, _IQmpy(_IQ(CellNumX - 1U), h));
            _iq y = clampf_local(particlePos[YID(i)], h, _IQmpy(_IQ(CellNumY - 1U), h));

            int x0 = iq_floor_to_int(_IQmpy((x - dx), h1));
            int y0 = iq_floor_to_int(_IQmpy((y - dy), h1));
            x0 = clamp_index(x0, 0, (int)CellNumX - 2);
            y0 = clamp_index(y0, 0, (int)CellNumY - 2);
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            _iq tx = _IQmpy(((x - dx) - _IQmpy(_IQ(x0), h)), h1);
            _iq ty = _IQmpy(((y - dy) - _IQmpy(_IQ(y0), h)), h1);
            _iq sx = _IQ(1.0) - tx;
            _iq sy = _IQ(1.0) - ty;

            _iq w0 = _IQmpy(sx, sy);
            _iq w1 = _IQmpy(tx, sy);
            _iq w2 = _IQmpy(tx, ty);
            _iq w3 = _IQmpy(sx, ty);

            _iq pv = particleVel[2 * i + (unsigned int)component];
            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = INDEX((unsigned int)x1, (unsigned int)y0);
            unsigned int nr2 = INDEX((unsigned int)x1, (unsigned int)y1);
            unsigned int nr3 = INDEX((unsigned int)x0, (unsigned int)y1);

            f[nr0] += _IQmpy(pv, w0); w[nr0] += w0;
            f[nr1] += _IQmpy(pv, w1); w[nr1] += w1;
            f[nr2] += _IQmpy(pv, w2); w[nr2] += w2;
            f[nr3] += _IQmpy(pv, w3); w[nr3] += w3;
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
}

void compute_grid_forces(unsigned int nIters) {
    //memset(pressure, 0, sizeof(pressure));
    memcpy(uPrev, uVel, sizeof(uVel));
    memcpy(vPrev, vVel, sizeof(vVel));

    //const _iq cp = _IQdiv(_IQmpy(_IQ(1000.0), Spacing), dt);

    for (unsigned int iter = 0; iter < nIters; iter++) {
        for (unsigned int x = 1; x < CellNumX - 1U; x++) {
            for (unsigned int y = 1; y < CellNumY - 1U; y++) {
                unsigned int center = INDEX(x, y);
                if (cellType[center] != FLUID_CELL) {
                    continue;
                }

                unsigned int left = INDEX(x - 1U, y);
                unsigned int right = INDEX(x + 1U, y);
                unsigned int bottom = INDEX(x, y - 1U);
                unsigned int top = INDEX(x, y + 1U);

                _iq sx0 = solidMask[left];
                _iq sx1 = solidMask[right];
                _iq sy0 = solidMask[bottom];
                _iq sy1 = solidMask[top];
                _iq s = sx0 + sx1 + sy0 + sy1;
                if (s == _IQ(0.0)) {
                    continue;
                }

                _iq div = uVel[right] - uVel[center] + vVel[top] - vVel[center];

                if (particleRestDensity > _IQ(0.0)) {
                    _iq compression = particleDensity[center] - particleRestDensity;
                    if (compression > _IQ(0.0)) {
                        compression = _IQmpy(compression,stiffnessCoefficient);
                        div -= compression;
                    }
                }

                _iq p = -_IQdiv(div, s);
                p = _IQmpy(p, overRelaxiation);
                //pressure[center] += _IQmpy(cp, p);

                uVel[center] -= _IQmpy(sx0, p);
                uVel[right] += _IQmpy(sx1, p);
                vVel[center] -= _IQmpy(sy0, p);
                vVel[top] += _IQmpy(sy1, p);
            }
        }
    }
}

void grid_to_particles(void) {
    const _iq flipRatio = _IQ(0.9);
    const _iq h = Spacing;
    const _iq h1 = invertSpacing;
    const _iq h2 = _IQmpy(_IQ(0.5), h);

    for (int component = 0; component < 2; component++) {
        _iq dx = (component == 0) ? _IQ(0.0) : h2;
        _iq dy = (component == 0) ? h2 : _IQ(0.0);
        _iq *f = (component == 0) ? uVel : vVel;
        _iq *prevF = (component == 0) ? uPrev : vPrev;
        int offset = (component == 0) ? (int)CellNumY : 1;

        for (unsigned int i = 0; i < NumberOfParticles; i++) {
            _iq x = clampf_local(particlePos[XID(i)], h, _IQmpy(_IQ(CellNumX - 1U), h));
            _iq y = clampf_local(particlePos[YID(i)], h, _IQmpy(_IQ(CellNumY - 1U), h));

            int x0 = clamp_index(iq_floor_to_int(_IQmpy((x - dx), h1)), 0, (int)CellNumX - 2);
            int y0 = clamp_index(iq_floor_to_int(_IQmpy((y - dy), h1)), 0, (int)CellNumY - 2);
            int x1 = x0 + 1;
            int y1 = y0 + 1;

            _iq tx = _IQmpy(((x - dx) - _IQmpy(_IQ(x0), h)), h1);
            _iq ty = _IQmpy(((y - dy) - _IQmpy(_IQ(y0), h)), h1);
            _iq sx = _IQ(1.0) - tx;
            _iq sy = _IQ(1.0) - ty;

            _iq d0 = _IQmpy(sx, sy);
            _iq d1 = _IQmpy(tx, sy);
            _iq d2 = _IQmpy(tx, ty);
            _iq d3 = _IQmpy(sx, ty);

            unsigned int nr0 = INDEX((unsigned int)x0, (unsigned int)y0);
            unsigned int nr1 = INDEX((unsigned int)x1, (unsigned int)y0);
            unsigned int nr2 = INDEX((unsigned int)x1, (unsigned int)y1);
            unsigned int nr3 = INDEX((unsigned int)x0, (unsigned int)y1);

            _iq valid0 = (cellType[nr0] != AIR_CELL || ((int)nr0 - offset >= 0 && cellType[(unsigned int)((int)nr0 - offset)] != AIR_CELL)) ? _IQ(1.0) : _IQ(0.0);
            _iq valid1 = (cellType[nr1] != AIR_CELL || ((int)nr1 - offset >= 0 && cellType[(unsigned int)((int)nr1 - offset)] != AIR_CELL)) ? _IQ(1.0) : _IQ(0.0);
            _iq valid2 = (cellType[nr2] != AIR_CELL || ((int)nr2 - offset >= 0 && cellType[(unsigned int)((int)nr2 - offset)] != AIR_CELL)) ? _IQ(1.0) : _IQ(0.0);
            _iq valid3 = (cellType[nr3] != AIR_CELL || ((int)nr3 - offset >= 0 && cellType[(unsigned int)((int)nr3 - offset)] != AIR_CELL)) ? _IQ(1.0) : _IQ(0.0);

            _iq d = _IQmpy(valid0, d0) + _IQmpy(valid1, d1) + _IQmpy(valid2, d2) + _IQmpy(valid3, d3);
            if (d <= _IQ(0.0)) {
                continue;
            }

            _iq picV = _IQdiv(_IQmpy(valid0, _IQmpy(d0, f[nr0])) + _IQmpy(valid1, _IQmpy(d1, f[nr1])) + _IQmpy(valid2, _IQmpy(d2, f[nr2])) + _IQmpy(valid3, _IQmpy(d3, f[nr3])), d);
            _iq corr = _IQdiv(_IQmpy(valid0, _IQmpy(d0, (f[nr0] - prevF[nr0]))) + _IQmpy(valid1, _IQmpy(d1, (f[nr1] - prevF[nr1])))
                         + _IQmpy(valid2, _IQmpy(d2, (f[nr2] - prevF[nr2]))) + _IQmpy(valid3, _IQmpy(d3, (f[nr3] - prevF[nr3]))), d);
            _iq oldV = particleVel[2 * i + (unsigned int)component];
            _iq flipV = oldV + corr;
            particleVel[2 * i + (unsigned int)component] = _IQmpy((_IQ(1.0) - flipRatio), picV) + _IQmpy(flipRatio, flipV);
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
            uint16_t index = (x%16)+((y>>3)<<4);
            screen[index] &= ~(1 << (y&7UL));
            //screen[index] |= (1 << (y&7UL));
            if (cell == FLUID_CELL) {
                screen[index] |= (1 << (y&7UL));
            } else if (cell == SOLID_CELL) {
                screen[index] |= (1 << (y&7UL));
            }
            x++;
            y++;
            
        }
    }    
}

void visualize_grid() {
    //显示
    
    // 根据网格类型直接显示
    for (int y = 0; y < CellNumY; y++) {
        for (int x = 0; x < CellNumX; x++) {
            unsigned int cell = cellType[INDEX(x, y)];
            char symbol = '-';
            if (cell == FLUID_CELL) {
                symbol = 'x';
            } else if (cell == SOLID_CELL) {
                symbol = '#';
            }
            visual_buffer[y][x] = symbol;
        }
        visual_buffer[y][CellNumX] = '\0';
    }
}

void printLocation(unsigned int n) {
    return;
}
