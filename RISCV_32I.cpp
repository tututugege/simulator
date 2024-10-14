#include <RISCV.h>
#include <cassert>
#include <config.h>
#include <cvt.h>
#include <util.h>

void RISCV_32I(bool input_data[BIT_WIDTH], bool *output_data) {

  // rename -> execute -> write back

  back.Back_comb(input_data, output_data);
  back.Back_seq(input_data, output_data);

  /*for (int i = 0; i < INST_WAY; i++) {*/
  /*  *(output_data + POS_OUT_FIRE + i) = back.out.ready[i] &&
   * back.in.valid[i];*/
  /*  if (valid[i] && br_tag.out.ready[i] && !back.out.ready[i])*/
  /*    has_alloc_1[i] = true;*/
  /*  else*/
  /*    has_alloc_1[i] = false;*/
  /*}*/

  /*for (int i = 0; i < INST_WAY; i++) {*/
  /*  has_alloc[i] = has_alloc_1[i];*/
  /*}*/
}
