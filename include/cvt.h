#pragma once
#include <cstring>
#include <fstream>
#include <iostream>
using namespace std;

inline int cvt_bit_to_number(bool input_data[], int k) {

  int output_number = 0;
  int i;
  for (i = 0; i < k - 1; i++) {
    output_number += uint32_t(input_data[k - 1 - i]) << i;
    // cout<<input_data[i];
  }
  if (input_data[0]) {
    output_number -= uint32_t(1) << (k - 1);
  }
  // cout<<' '<<dec<< output_number<<endl;
  return output_number;
};

inline bool *cvt_number_to_bit(int input_data, int k) {
  int i;
  // bool*	output_bits;
  bool *output_bits = new bool[k];
  for (i = 0; i < k; i++) {
    output_bits[i] = (input_data >> (k - 1 - i)) & int(1);
  }
  return output_bits;
};

inline long cvt_bit_to_number_unsigned(bool input_data[], int k) {

  uint32_t output_number = 0;
  int i;
  for (i = 0; i < k; i++) {
    output_number += uint32_t(input_data[i]) << (k - 1 - i);
    // cout<<input_data[i];
  }
  // cout<<' '<<dec<<output_number<<endl;
  return output_number;
};

inline bool *cvt_number_to_bit_unsigned(uint32_t input_data, int k) {

  bool *output_bits = new bool[k];
  int i;
  for (i = 0; i < k; i++) {
    output_bits[i] = (input_data >> (k - 1 - i)) & uint32_t(1);
    // cout<<output_number[i];
  }
  // cout<<' '<<hex<<input_data<<endl;
  return output_bits;
};

inline void sign_extend(bool *bit_output, int output_length, bool *bit_input,
                        int input_length) {
  for (int i = 0; i < output_length - input_length; i++)
    bit_output[i] = bit_input[0];
  for (int i = 0; i < input_length; i++)
    bit_output[output_length - input_length + i] = bit_input[i];
}

void inline zero_extend(bool *bit_output, int output_length, bool *bit_input,
                        int input_length) {
  for (int i = 0; i < output_length - input_length; i++)
    bit_output[i] = 0;
  for (int i = 0; i < input_length; i++)
    bit_output[output_length - input_length + i] = bit_input[i];
}

void inline add_bit_list(bool *bit_output, bool *bit_input_a, bool *bit_input_b,
                         int length) {
  bool c = 0;
  for (int i = length - 1; i >= 0; i--) {
    int temp = int(bit_input_a[i]) + int(bit_input_b[i]) + int(c);
    bit_output[i] = temp % 2 == 1 ? 1 : 0;
    c = temp > 1 ? 1 : 0;
  }
}

void inline copy_indice(bool *dst, uint32_t dst_idx, bool *src,
                        uint32_t src_idx, uint32_t num) {
  memcpy(dst + dst_idx, src + src_idx, num * sizeof(bool));
}

template <typename T> void cout_indice(T arr, uint32_t idx, uint32_t num) {
  for (int i = 0; i < num; i++)
    cout << arr[i + idx];
  cout << endl;
}

void inline init_indice(bool *arr, uint32_t idx, uint32_t num) {
  memset(arr + idx, 0, num * sizeof(bool));
}

void inline init_indice(uint32_t *arr, uint32_t idx, uint32_t num) {
  memset(arr + idx, 0, num * sizeof(uint32_t));
}

void inline cvt_number_to_bit_unsigned(bool *output_number, uint32_t input_data,
                                       int k) {
  int i;
  for (i = 0; i < k; i++) {
    output_number[i] = (input_data >> (k - 1 - i)) & uint32_t(1);
    //	cout<<output_number[i];
  }
  // cout<<' '<< dec<<input_data<<endl;
};

void inline cvt_number_to_bit(bool *output_number, int input_data, int k) {
  int i;
  for (i = 0; i < k; i++) {
    output_number[i] = (input_data >> (k - 1 - i)) & uint32_t(1);
    //	cout<<output_number[i];
  }
  // cout<<' '<< input_data<<endl;
};

const string reg_names[32] = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

const string csr_names[21] = {
    "mtvec",   "mepc",    "mcause",  "mie",  "mip",   "mtval",   "mscratch",
    "mstatus", "mideleg", "medeleg", "sepc", "stvec", "scause",  "sscratch",
    "stval",   "sstatus", "sie",     "sip",  "satp",  "mhartid", "misa"};

void inline print_regs(bool *arr) {
  for (int i = 0; i < 32; i++) {
    if (i % 10 == 0)
      cout << endl;
    bool reg[32];
    copy_indice(reg, 0, arr, i * 32, 32);
    uint32_t reg_value = cvt_bit_to_number_unsigned(reg, 32);
    cout << reg_names[i] << ": 0x" << hex << reg_value << "\t";
  }
  cout << endl;
}

void inline print_csr_regs(bool *arr) {
  for (int i = 0; i < 21; i++) {
    if (i % 10 == 0)
      cout << endl;
    bool reg[32];
    copy_indice(reg, 0, arr + 1024 * sizeof(bool), i * 32, 32);
    uint32_t reg_value = cvt_bit_to_number_unsigned(reg, 32);
    cout << csr_names[i] << ": 0x" << hex << reg_value << "\t";
  }
  cout << endl;
}

void inline write_bool_to_file(ofstream &file, bool *arr, uint32_t num) {
  char temp[4] = {0};
  for (int i = 0; i < num / 32; i++) {
    for (int j = 0; j < 4; j++) {
      temp[3 - j] = 0;
      for (int k = 0; k < 8; k++) {
        if (arr[i * 32 + j * 8 + k])
          temp[3 - j] |= 1 << (7 - k);
      }
    }
    file.write(temp, 4);
  }
}
