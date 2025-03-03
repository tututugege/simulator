#include <cstdio>
#include <cstring>
// #include <iostream>

// #include "../sequential_components/seq_comp.h"
// #include "../BPU/dir_predictor/tage_types.h"

/*#include "IO_cvt.h"*/

// convert any type of struct into boolArray
template <typename T> void structToBoolArray(const T &s, bool *boolArray) {
  const size_t byteSize = sizeof(T);

  const unsigned char *bytePtr = reinterpret_cast<const unsigned char *>(&s);
  /*printf("byte0 = %c\n", bytePtr[0]);*/

  for (size_t byteIndex = 0; byteIndex < byteSize; ++byteIndex) {
    for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
      /*printf("byteIndex %d bytevalue %x\n", byteIndex, bytePtr[byteIndex]);*/
      boolArray[byteIndex * 8 + bitIndex] =
          (bytePtr[byteIndex] >> bitIndex) & 1;
    }
  }
}

// Given a boolArray and a struct, convert the boolArray back to the struct
template <typename T> void boolArrayToStruct(const bool *boolArray, T &s) {
  const size_t byteSize = sizeof(T);

  unsigned char byteData[byteSize];
  std::memset(byteData, 0, byteSize);

  for (size_t byteIndex = 0; byteIndex < byteSize; ++byteIndex) {
    for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
      if (boolArray[byteIndex * 8 + bitIndex]) {
        byteData[byteIndex] |= (1 << bitIndex);
      }
    }
  }

  std::memcpy(&s, byteData, byteSize);
}

#ifdef IO_CVT_MAIN
// testbench
pred_1_IO *pred_IO1;
pred_1_IO IO1;
bool boolArray[100000];

using namespace std;
int main() {
  pred_IO1 = &IO1;
  for (int k = 0; k < FH_N_MAX; k++) {
    for (int i = 0; i < TN_MAX; i++) {
      pred_IO1->FH[k][i] = i * k;
    }
  }
  pred_IO1->pc = 0xdeadbeef;
  for (int i = 0; i < TN_MAX; i++) {
    pred_IO1->index[i] = i + 999;
  }
  structToBoolArray(IO1, boolArray);
  for (size_t i = 0; i < sizeof(IO1) * 8; ++i) {
    std::cout << boolArray[i];
    if (i % 32 == 31)
      std::cout << endl;
  }

  pred_IO1->pc = 0;

  for (int k = 0; k < FH_N_MAX; k++) {
    for (int i = 0; i < TN_MAX; i++) {
      pred_IO1->FH[k][i] = i * k + 999;
    }
  }

  for (int i = 0; i < TN_MAX; i++) {
    pred_IO1->index[i] = i + 9999;
  }

  printf("pc=0x%x\n", pred_IO1->pc);
  for (int k = 0; k < FH_N_MAX; k++) {
    for (int i = 0; i < TN_MAX; i++) {
      printf("FH = %d", pred_IO1->FH[k][i]);
    }
    printf("\n");
  }
  for (int i = 0; i < TN_MAX; i++) {
    printf("index = %d", pred_IO1->index[i]);
  }
  printf("\n");
  boolArrayToStruct(boolArray, IO1);
  printf("pc=0x%x\n", pred_IO1->pc);
  for (int k = 0; k < FH_N_MAX; k++) {
    for (int i = 0; i < TN_MAX; i++) {
      printf("FH = %d", pred_IO1->FH[k][i]);
    }
    printf("\n");
  }
  for (int i = 0; i < TN_MAX; i++) {
    printf("index = %d", pred_IO1->index[i]);
  }
  printf("\n");

  return 0;
}
#endif