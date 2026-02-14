#include "trap.h"

#define N 16

float a[N][N];
float b[N][N];
float c[N][N];

void matmul_soft() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < N; k++) {
                sum += a[i][k] * b[k][j];
            }
            c[i][j] = sum;
        }
    }
}

int main() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            a[i][j] = 1.0f;
            b[i][j] = 2.0f;
        }
    }

    matmul_soft();

    check(c[0][0] == 32.0f);

    return 0;
}
