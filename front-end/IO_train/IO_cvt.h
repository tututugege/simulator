#ifndef _IO_CVT_H_
#define _IO_CVT_H_

#include <cstdio>
#include <cstring>

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

#endif
