#include "IQmath_RV32.h"
#include <stdio.h>
#include <string.h>

//模拟所用的参数
#define NumberOfParticles 180U //粒子数量
#define ParticleRadius _IQ(0.035) //粒子半径，这同时也是网格的间距
#define Spacing _IQ(0.1)//网格间距
#define CellNumX 16U //x轴方向的网格数量
#define CellNumY 16U //y轴方向的网格数量
#define CellCount CellNumX*CellNumY //网格总数
#define dt _IQ(0.016) //时间步长
#define BOUNCYNESS _IQ(-0.3) //墙壁的弹性。

#define overRelaxiation _IQ(1.9) 
#define stiffnessCoefficient _IQ(1.0)

//用于访问粒子位置的一些方便的宏
#define XID(n) 2*(n)    //访问x位置
#define YID(n) 2*(n)+1  //访问y位置
#define INDEX(x,y) ((x)*CellNumY+y) //根据网格的xy坐标访问对应的网格


extern char visual_buffer[CellNumY][CellNumX+1];
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
