#include "predecode_checker.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void predecode_checker_seq_read(struct predecode_checker_in *in,
                                struct predecode_checker_read_data *rd) {
  assert(in);
  assert(rd);
  std::memset(rd, 0, sizeof(predecode_checker_read_data));
  rd->inp_regs = *in;
}

void predecode_checker_comb(const predecode_checker_read_data &input,
                            predecode_checker_out &output) {
  std::memset(&output, 0, sizeof(predecode_checker_out));

  for (int i = 0; i < FETCH_WIDTH; i++) {
    switch (input.inp_regs.predecode_type[i]) {
    case PREDECODE_NON_BRANCH:
      output.predict_dir_corrected[i] = false;
      break;
    case PREDECODE_DIRECT_JUMP_NO_JAL:
      output.predict_dir_corrected[i] = input.inp_regs.predict_dir[i];
      break;
    case PREDECODE_JALR:
    case PREDECODE_JAL:
      output.predict_dir_corrected[i] = true;
      break;
    default:
      std::printf("ERROR!!: predecode_type[%d] = %d\n", i,
                  input.inp_regs.predecode_type[i]);
      std::exit(1);
    }
  }

  int first_taken_index = -1;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (output.predict_dir_corrected[i]) {
      first_taken_index = i;
      break;
    }
  }

  output.predict_next_fetch_address_corrected =
      input.inp_regs.predict_next_fetch_address;

  if (first_taken_index != -1) {
    if (input.inp_regs.predecode_type[first_taken_index] ==
            PREDECODE_DIRECT_JUMP_NO_JAL ||
        input.inp_regs.predecode_type[first_taken_index] == PREDECODE_JAL) {
      output.predict_next_fetch_address_corrected =
          input.inp_regs.predecode_target_address[first_taken_index];
    }
  } else {
    output.predict_next_fetch_address_corrected = input.inp_regs.seq_next_pc;
  }

  output.predecode_flush_enable =
      (input.inp_regs.predict_next_fetch_address !=
       output.predict_next_fetch_address_corrected);
}

void predecode_checker_seq_write() {}
