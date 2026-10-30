#ifndef __PRINT_H__
#define __PRINT_H__

#ifndef ENABLE_PRINT
#define ENABLE_PRINT 1
#endif

#ifndef PRINT
#if ENABLE_PRINT == 1
#include <stdio.h>
#define PRINT(...) printf(__VA_ARGS__)
#else
#define PRINT(...) ((void)0)
#endif
#endif

#endif
