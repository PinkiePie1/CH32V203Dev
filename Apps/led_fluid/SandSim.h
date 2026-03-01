#include "IQmath_RV32.h"
#include <stdio.h>
#include <string.h>
#include "debug.h"

//模拟所用的参数
#define NumberOfParticles 60U //粒子数量
#define ParticleRadius _IQ(0.016f) //粒子半径
#define Spacing _IQ(0.05f)//网格间距
#define CellNumX 10U //x轴方向的网格数量
#define CellNumY 11U //y轴方向的网格数量
#define CellCount CellNumX*CellNumY //网格总数
#define dt _IQ(0.013f) //时间步长
#define BOUNCYNESS _IQ(-0.9f) //墙壁的弹性。

#define overRelaxiation _IQ(1.9f) 
#define stiffnessCoefficient _IQ(1.0f)

//用于访问粒子位置的一些方便的宏
#define XID(n) 2*(n)    //访问x位置
#define YID(n) 2*(n)+1  //访问y位置
#define INDEX(x,y) ((x)*CellNumY+y) //根据网格的xy坐标访问对应的网格

extern uint8_t screen[32];

void ParticleIntegrate(_iq xAcceleration, _iq yAcceleration);  //根据所提供的加速度仿真粒子的行动，注意墙壁碰撞应该在这里进行。
void PushParticlesApart(unsigned int nIters);                     //将粒子相互推开

void density_update(void);
void particles_to_grid(void);
void compute_grid_forces(unsigned int nIters);
void grid_to_particles(void);
void screen_update(void); 

void InitParticles(void);//初始化粒子，还没完成
void visualize_grid(void); //显示
