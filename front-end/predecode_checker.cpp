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

void predecode_checker_comb(const struct predecode_checker_read_data *rd,
                            struct predecode_checker_out *out) {
  assert(rd);
  assert(out);
  std::memset(out, 0, sizeof(predecode_checker_out));

  for (int i = 0; i < FETCH_WIDTH; i++) {
    switch (rd->inp_regs.predecode_type[i]) {
    case PREDECODE_NON_BRANCH:
      out->predict_dir_corrected[i] = false;
      break;
    case PREDECODE_DIRECT_JUMP_NO_JAL:
      out->predict_dir_corrected[i] = rd->inp_regs.predict_dir[i];
      break;
    case PREDECODE_JALR:
    case PREDECODE_JAL:
      out->predict_dir_corrected[i] = true;
      break;
    default:
      std::printf("ERROR!!: predecode_type[%d] = %d\n", i,
                  rd->inp_regs.predecode_type[i]);
      std::exit(1);
    }
  }

  int first_taken_index = -1;
  for (int i = 0; i < FETCH_WIDTH; i++) {
    if (out->predict_dir_corrected[i]) {
      first_taken_index = i;
      break;
    }
  }

  out->predict_next_fetch_address_corrected =
      rd->inp_regs.predict_next_fetch_address;

  if (first_taken_index != -1) {
    if (rd->inp_regs.predecode_type[first_taken_index] ==
            PREDECODE_DIRECT_JUMP_NO_JAL ||
        rd->inp_regs.predecode_type[first_taken_index] == PREDECODE_JAL) {
      out->predict_next_fetch_address_corrected =
          rd->inp_regs.predecode_target_address[first_taken_index];
    }
  } else {
    out->predict_next_fetch_address_corrected = rd->inp_regs.seq_next_pc;
  }

  out->predecode_flush_enable =
      (rd->inp_regs.predict_next_fetch_address !=
       out->predict_next_fetch_address_corrected);
}

void predecode_checker_seq_write() {}
