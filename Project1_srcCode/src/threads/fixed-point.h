// thread/fixed-point.h
#include <stdint.h>
#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#define F (1 << 14)

int fp_convert_N_to_fp(int N) { return N*F; }
int fp_convert_X_to_integer_zero(int X) { return X/F; }
int fp_convert_X_to_integer_round(int X) { return (X>=0)?(X+F/2)/F:(X-F/2)/F; }

int fp_add_X_and_Y(int X, int Y) { return X+Y; }
int fp_sub_Y_from_X(int X, int Y) { return X-Y; }
int fp_add_X_and_N(int X, int N) { return X+N*F; }
int fp_sub_N_from_X(int X, int N) { return X-N*F; }

int fp_mul_X_by_Y(int X, int Y) { return ((int64_t)X)*Y/F; }
int fp_mul_X_by_N(int X, int N) { return X*N; }

int fp_div_X_by_Y(int X, int Y) { return ((int64_t)X)*F/Y;}
int fp_div_X_by_N(int X, int N) { return X/N; }

#endif