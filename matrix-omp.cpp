// From: http://blog.speedgocomputing.com/2010/08/parallelizing-matrix-multiplication.html

#include "sim_api.h"

/* matrix-omp.cpp */
const int size = 100;
const int size2 = 75;
const int iterations = 20;

float a[size][size];
float b[size][size];
float c[size][size];

float d[size2][size2];
float e[size2][size2];
float f[size2][size2];

int main()
{
    // Initialize buffers.
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            a[i][j] = (float)i + j;
            b[i][j] = (float)i - j;
            c[i][j] = 0.0f;
        }
    }

    // Initialize buffers.
    for (int i = 0; i < size2; ++i) {
        for (int j = 0; j < size2; ++j) {
            d[i][j] = (float)i + j;
            e[i][j] = (float)i - j;
            f[i][j] = 0.0f;
        }
    }

    SimRoiStart();

    for (int itr = 0 ; itr < iterations ; itr++) {
    // Compute matrix multiplication.
    // C <- C + A x B
    #pragma omp parallel for default(none) shared(a,b,c)
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            for (int k = 0; k < size; ++k) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
//    }

//    for (int itr = 0 ; itr < iterations2 ; itr++) {
    // Compute matrix multiplication.
    // C <- C + A x B
    #pragma omp parallel for default(none) shared(d,e,f)
    for (int i = 0; i < size2; ++i) {
        for (int j = 0; j < size2; ++j) {
            for (int k = 0; k < size2; ++k) {
                f[i][j] += d[i][k] * e[k][j];
            }
        }
    }
    }

    SimRoiEnd();

    return 0;
}
