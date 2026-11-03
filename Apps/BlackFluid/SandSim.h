#ifndef SANDSIM_H
#define SANDSIM_H

#include <stdint.h>
#include "IQmath_RV32.h"

#define NumberOfParticles 144U
#define ParticleRadius _IQ(0.019006578f)
#define Spacing _IQ(0.05f)
#define CellNumX 17U
#define CellNumY 18U
#define CellCount (CellNumX * CellNumY)
#define FluidPushIterations 1U
#define FluidPressureIterations 6U
#define dt _IQ(0.014f)
#define BOUNCYNESS _IQ(-0.9f)
#define overRelaxiation _IQ(1.8f)
#define stiffnessCoefficient _IQ(1.0f)

#define XID(n) (2U * (n))
#define YID(n) (2U * (n) + 1U)
#define INDEX(x, y) ((x) * CellNumY + (y))

void InitParticles(void);
void ParticleIntegrate(_iq xAcceleration, _iq yAcceleration);
void PushParticlesApart(unsigned int nIters);
void particles_to_grid(void);
void density_update(void);
void compute_grid_forces(unsigned int nIters);
void grid_to_particles(void);
void screen_update(void);

#endif
