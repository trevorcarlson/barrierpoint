// From: http://blog.speedgocomputing.com/2010/08/parallelizing-matrix-multiplication.html

#include "sim_api.h"
#include "matrix-omp.h"

#include <omp.h>

int main()
{
    init();

    // From http://stackoverflow.com/questions/11095309/openmp-set-num-threads-is-not-working
    omp_set_dynamic(0);     // Explicitly disable dynamic teams
    omp_set_num_threads(4); // Use 4 threads for all consecutive parallel regions

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
