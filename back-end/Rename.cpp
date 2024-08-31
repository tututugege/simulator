#include "Rename.h"

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

    // waw
    bool dest_we = true;
    for (int j = i + 1; j < WAY; j++) {
      if (in.dest_areg_en[j] && in.dest_areg_idx[j] == in.dest_areg_idx[i]) {
        dest_we = false;
        break;
      }
    }

    int old_dest_preg_idx = spec_RAT[in.dest_areg_idx[i]];

    if (dest_we)
      spec_RAT[in.dest_areg_idx[i]] = out.dest_preg_idx[i];
  }
}

/**/
/*    for (int i = 0; i < ARF_NUM; i++)*/
/*        Rename[i] = i;*/
/**/
/*    for (int i = 0; i < PRF_NUM - ARF_NUM; i++)*/
/*        free_list[i] = ARF_NUM + i;*/
/**/
/*    Rename_in in;*/
/*    // in.src1_areg_en[0] = 1;*/
/*    // in.src2_areg_en[0] = 1;*/
/*    // in.dest_areg_en[0] = 1;*/
/*    // in.src1_areg_idx[0] = 3;*/
/*    // in.src2_areg_idx[0] = 2;*/
/*    // in.dest_areg_idx[0] = 5;*/
/**/
/*    // in.src1_areg_en[1] = 1;*/
/*    // in.src2_areg_en[1] = 1;*/
/*    // in.dest_areg_en[1] = 1;*/
/*    // in.src1_areg_idx[1] = 4;*/
/*    // in.src2_areg_idx[1] = 2;*/
/*    // in.dest_areg_idx[1] = 6;*/
/**/
/*    // in.src1_areg_en[2] = 1;*/
/*    // in.src2_areg_en[2] = 1;*/
/*    // in.dest_areg_en[2] = 1;*/
/*    // in.src1_areg_idx[2] = 7;*/
/*    // in.src2_areg_idx[2] = 1;*/
/*    // in.dest_areg_idx[2] = 4;*/
/**/
/*    // in.src1_areg_en[3] = 1;*/
/*    // in.src2_areg_en[3] = 1;*/
/*    // in.dest_areg_en[3] = 1;*/
/*    // in.src1_areg_idx[3] = 6;*/
/*    // in.src2_areg_idx[3] = 1;*/
/*    // in.dest_areg_idx[3] = 6;*/
/**/
/*    in.src1_areg_en[0] = 1;*/
/*    in.src2_areg_en[0] = 1;*/
/*    in.dest_areg_en[0] = 1;*/
/*    in.src1_areg_idx[0] = 1;*/
/*    in.src2_areg_idx[0] = 2;*/
/*    in.dest_areg_idx[0] = 1;*/
/**/
/*    in.src1_areg_en[1] = 1;*/
/*    in.src2_areg_en[1] = 1;*/
/*    in.dest_areg_en[1] = 1;*/
/*    in.src1_areg_idx[1] = 3;*/
/*    in.src2_areg_idx[1] = 4;*/
/*    in.dest_areg_idx[1] = 3;*/
/**/
/*    in.src1_areg_en[2] = 1;*/
/*    in.src2_areg_en[2] = 1;*/
/*    in.dest_areg_en[2] = 1;*/
/*    in.src1_areg_idx[2] = 6;*/
/*    in.src2_areg_idx[2] = 1;*/
/*    in.dest_areg_idx[2] = 5;*/
/**/
/*    in.src1_areg_en[3] = 1;*/
/*    in.src2_areg_en[3] = 1;*/
/*    in.dest_areg_en[3] = 1;*/
/*    in.src1_areg_idx[3] = 8;*/
/*    in.src2_areg_idx[3] = 9;*/
/*    in.dest_areg_idx[3] = 9;*/
/**/
/*    Rename_out out = RAT_cycle(in);*/
/**/
/*    for (int i = 0; i < WAY; i++) {*/
/*        printf("%d, %d op %d\t", in.dest_areg_idx[i], in.src1_areg_idx[i],
 * in.src2_areg_idx[i]);*/
/*        printf("%d, %d op %d\n", out.dest_preg_idx[i], out.src1_preg_idx[i],
 * out.src2_preg_idx[i]);*/
/*    }*/
/**/
/*    for (int i = 0; i < ARF_NUM; i++) {*/
/*        printf("%d: %d\n", i, Rename[i]);*/
/*    }*/
/*}*/
