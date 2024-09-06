#include "Rename.h"
#include "../cvt.h"
#include "config.h"

void Rename::init() {
  for (int i = 0; i < ARF_NUM; i++) {
    spec_RAT[i] = i;
    arch_RAT[i] = i;
  }

  for (int i = 0; i < PRF_NUM - ARF_NUM; i++)
    free_list[i] = ARF_NUM + i;
}

int Rename::alloc_reg() {
  if (free_list_count == PRF_NUM)
    return -1;

  free_list_count++;
  int ret = free_list[free_list_head];
  free_list_head = (free_list_head + 1) % PRF_NUM;

  return ret;
}

void Rename::free_reg(int idx) {
  free_list_count--;
  free_list[free_list_tail] = idx;
  free_list_tail = (free_list_tail + 1) % PRF_NUM;
}

void Rename::cycle() {
  for (int i = 0; i < WAY; i++) {
    int new_preg = alloc_reg();
    out.dest_preg_idx[i] = new_preg;
    out.src1_preg_idx[i] = spec_RAT[in.src1_areg_idx[i]];
    out.src2_preg_idx[i] = spec_RAT[in.src2_areg_idx[i]];
    out.old_dest_preg_idx[i] = spec_RAT[in.dest_areg_idx[i]];
  }

  for (int i = 0; i < WAY; i++) {
    if (in.dest_areg_en[i] == 0)
      continue;

    // raw
    for (int j = i + 1; j < WAY; j++) {
      if (in.src1_areg_en[j] && in.src1_areg_idx[j] == in.dest_areg_idx[i]) {
        out.src1_preg_idx[j] = out.dest_preg_idx[i];
      }

      if (in.src2_areg_en[j] && in.src2_areg_idx[j] == in.dest_areg_idx[i]) {
        out.src2_preg_idx[j] = out.dest_preg_idx[i];
      }
    }

    // waw 同一preg的映射表写入只取最新的  写入rob的old_preg信息可能不来自RAT
    bool dest_we = true;
    for (int j = i + 1; j < WAY; j++) {
      if (in.dest_areg_en[j] && in.dest_areg_idx[j] == in.dest_areg_idx[i]) {
        dest_we = false;
        out.old_dest_preg_idx[j] = out.dest_preg_idx[i];
        break;
      }
    }

    if (dest_we)
      spec_RAT[in.dest_areg_idx[i]] = out.dest_preg_idx[i];
  }
}

void Rename::recover() {
  for (int i = 0; i < ARF_NUM; i++) {
    spec_RAT[i] = arch_RAT[i];
  }
}

void Rename::print_reg(bool *output_data) {
  int preg_idx;
  for (int i = 0; i < ARF_NUM; i++) {
    preg_idx = arch_RAT[i];
    uint32_t data = cvt_bit_to_number_unsigned(output_data + preg_idx * 32, 32);

    cout << reg_names[i] << ": " << hex << data << " ";

    if (i % 8 == 0)
      cout << endl;
  }
  cout << endl;
}

void Rename::print_RAT() {
  for (int i = 0; i < ARF_NUM; i++) {
    cout << dec << i << ":" << dec << arch_RAT[i] << " ";

    if (i % 8 == 0)
      cout << endl;
  }
  cout << endl;
}
