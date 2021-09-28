
#ifndef GROUP20_FIXED_POINT_H
#define GROUP20_FIXED_POINT_H

#define f 16384									/* 2^14*/
#define convert_int_to_float(n) (n*f) 			/*convert from int to float*/
#define convert_float_to_int_zero(x) (x/f) 		/*convert from float to int, rounding to zero*/
#define convert_float_to_int_nearest(x) (x >= 0 ? (x + f/2) / f : (x - f/2) / f)   /*rounding to nearest*/
#define add_float(x, y) (x + y)  					/*add two floats*/
#define add_float_int(x, n) (x + n*f)				/*add a float x and an int n*/
#define sub_float(x, y) (x - y)					/*subtract two floats*/
#define sub_float_int(x, n) (x - n*f) 			/*subtract float x and int n*/
#define mul_float(x, y) (((int64_t) x)*y/f) 		/*multiply two floats*/
#define mul_float_int(x, n) (x*n) 				/*multiply a float and an int*/
#define div_float(x, y) (((int64_t) x)*f/y)		/*divide two floats*/
#define div_float_int(x, n) (x/n) 				/*divide a float x by an int n*/

#endif //GROUP20_FIXED_POINT_H
