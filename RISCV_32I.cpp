#include <RISCV.h>
#include <cassert>
#include <config.h>
#include <cvt.h>
#include <util.h>

void RISCV_32I(bool input_data[BIT_WIDTH], bool *output_data) {

  // rename -> execute -> write back

  back.Back_comb(input_data, output_data);
  back.Back_seq(input_data, output_data);
}
