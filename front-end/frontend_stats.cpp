#include "frontend_stats.h"
#include "config/frontend_diag_config.h"
#include "frontend.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct TrainableIoStat {
  const char *module;
  const char *function_name;
  uint64_t input_bits;
  uint64_t output_bits;
};

struct AreaStat {
  const char *module;
  uint64_t reg_bits;
  uint64_t sram_bits;
};

constexpr double bits_to_kbit(uint64_t bits) {
  return static_cast<double>(bits) / 1024.0;
}

constexpr double bits_to_kbyte(uint64_t bits) {
  return static_cast<double>(bits) / 8192.0;
}

constexpr uint64_t bits_for_count(uint64_t count) {
  uint64_t bits = 0;
  uint64_t value = (count <= 1) ? 1 : (count - 1);
  while (value > 0) {
    bits++;
    value >>= 1;
  }
  return bits;
}

constexpr uint64_t kNlpIndexBits = bits_for_count(NLP_TABLE_SIZE);
constexpr uint64_t kNlpTagBits = 30; // pc >> 2
constexpr uint64_t kTcWayBits = bits_for_count(TC_WAY_NUM);
constexpr uint64_t kTcSetBits = static_cast<uint64_t>(TC_WAY_NUM) *
                                (32 + TC_TAG_LEN + 1 + 3);

const std::vector<TrainableIoStat> kTrainableIoStats = {
    {"BPU", "bank_sel_comb", 32, 2},
    {"BPU", "bank_pc_comb", 32, 32},
    {"BPU", "nlp_index_comb", 32, kNlpIndexBits},
    {"BPU", "nlp_tag_comb", 32, kNlpTagBits},
    {"TAGE", "sat_inc_3bit_comb", 3, 3},
    {"TAGE", "sat_dec_3bit_comb", 3, 3},
    {"TAGE", "sat_inc_2bit_comb", 2, 2},
    {"TAGE", "sat_dec_2bit_comb", 2, 2},
    {"TAGE", "tage_xorshift32_comb", 32, 32},
    {"BTB", "btb_xorshift32_comb", 32, 32},
    {"BTB", "btb_get_tag_comb", 32, 8},
    {"BTB", "btb_get_idx_comb", 32, 9},
    {"BTB", "btb_get_type_idx_comb", 32, 12},
    {"BTB", "bht_get_idx_comb", 32, 11},
    {"BTB", "tc_get_idx_comb", 43, 11},
    {"BTB", "tc_get_tag_comb", 32, TC_TAG_LEN},
    {"BTB", "tc_hit_check_comb", kTcSetBits + TC_TAG_LEN, kTcWayBits + 1},
    {"BTB", "tc_victim_select_comb", kTcSetBits, kTcWayBits},
    {"BTB", "bht_next_state_comb", 12, 11},
    {"BTB", "useful_next_state_comb", 4, 3},
    {"BTB", "btb_victim_select_comb", 176, 2},

    {"TAGE", "tage_ghr_update_comb", 257, 256},
    {"TAGE", "tage_fh_update_comb", 1153, 384},
    {"TAGE", "lsfr_update_comb", 4, 12},
    {"TAGE", "tage_pred_index_comb", 416, 192},
    {"TAGE", "tage_pred_select_comb", 296, 226},
    {"BTB", "btb_hit_check_comb", 324, 33},
    {"BTB", "btb_pred_output_comb", 397, 32},

    {"TAGE", "tage_update_comb", 371, 175},
    {"TAGE", "tage_gen_index_comb", 1998, 161},
    {"BTB", "btb_gen_index_pre_comb", 374, 161},
    {"BTB", "btb_mem_read_pre_comb", 468, 365},
    {"BTB", "btb_gen_index_post_comb", 900, 161},

    {"Predecode", "predecode_comb", 64, 34},
    {"PredecodeChecker", "predecode_checker_comb", 204, 37},
    {"FetchAddrFIFO", "fetch_addr_comb", 77, 70},
    {"InstructionFIFO", "instruction_fifo_comb", 925, 918},
    {"PTAB", "ptab_comb", 2528, 3779},
    {"Front2BackFIFO", "front2back_comb", 2797, 2790},

    {"TAGE", "tage_comb", 2538, 1328},
    {"BTB", "btb_comb", 2222, 1419},
};

const std::vector<AreaStat> kAreaStats = {
    {"front_top_latches", 73, 0},
    {"fetch_address_fifo", 1024, 0},
    {"instruction_fifo", 14592, 0},
    {"ptab_fifo", 40256, 0},
    {"front2back_fifo", 89088, 0},
    {"bpu_top", 5735079, 0},
    {"tage_top", 969, 458752},
    {"btb_top", 841, 313344},
    {"icache", 298835, 0},
};

[[maybe_unused]] void print_trainable_io_stats() {
  std::printf("=== Trainable Function IO Stats (bit) ===\n");
  std::printf("%-18s %-30s %14s %14s\n", "Module", "Function", "InputBits",
              "OutputBits");
  uint64_t total_input_bits = 0;
  uint64_t total_output_bits = 0;
  for (const auto &item : kTrainableIoStats) {
    std::printf("%-18s %-30s %14llu %14llu\n", item.module, item.function_name,
                static_cast<unsigned long long>(item.input_bits),
                static_cast<unsigned long long>(item.output_bits));
    total_input_bits += item.input_bits;
    total_output_bits += item.output_bits;
  }
  std::printf("%-18s %-30s %14llu %14llu\n\n", "TOTAL", "-",
              static_cast<unsigned long long>(total_input_bits),
              static_cast<unsigned long long>(total_output_bits));
}

[[maybe_unused]] void print_area_stats() {
  std::printf("=== Front-end Sequential Area Stats (logical width) ===\n");
  std::printf("%-20s %12s %12s %12s %12s %12s\n", "Module", "RegBits",
              "SramBits", "TotalBits", "TotalKbit", "TotalKByte");

  uint64_t total_reg_bits = 0;
  uint64_t total_sram_bits = 0;

  for (const auto &item : kAreaStats) {
    const uint64_t total_bits = item.reg_bits + item.sram_bits;
    std::printf("%-20s %12llu %12llu %12llu %12.3f %12.3f\n", item.module,
                static_cast<unsigned long long>(item.reg_bits),
                static_cast<unsigned long long>(item.sram_bits),
                static_cast<unsigned long long>(total_bits),
                bits_to_kbit(total_bits), bits_to_kbyte(total_bits));
    total_reg_bits += item.reg_bits;
    total_sram_bits += item.sram_bits;
  }

  const uint64_t total_bits = total_reg_bits + total_sram_bits;
  std::printf("%-20s %12llu %12llu %12llu %12.3f %12.3f\n\n", "TOTAL",
              static_cast<unsigned long long>(total_reg_bits),
              static_cast<unsigned long long>(total_sram_bits),
              static_cast<unsigned long long>(total_bits), bits_to_kbit(total_bits),
              bits_to_kbyte(total_bits));

  std::printf("REG_TOTAL : %llu bit, %.3f Kbit, %.3f KByte\n",
              static_cast<unsigned long long>(total_reg_bits),
              bits_to_kbit(total_reg_bits), bits_to_kbyte(total_reg_bits));
  std::printf("SRAM_TOTAL: %llu bit, %.3f Kbit, %.3f KByte\n",
              static_cast<unsigned long long>(total_sram_bits),
              bits_to_kbit(total_sram_bits), bits_to_kbyte(total_sram_bits));
  std::printf("ALL_TOTAL : %llu bit, %.3f Kbit, %.3f KByte\n\n",
              static_cast<unsigned long long>(total_bits), bits_to_kbit(total_bits),
              bits_to_kbyte(total_bits));
}

[[maybe_unused]] void print_assumption_notes() {
  std::printf("=== Notes ===\n");
  std::printf("- IO stats scope: all named trainable *_comb functions.\n");
  std::printf("- Width rule: protocol width via *_BITS aliases (not C++ padding).\n");
  std::printf("- Area rule: only TAGE/BTB SRAM-delay storage counted as SRAM.\n");
  std::printf("- Other front-end sequential state is counted as Reg.\n");
  std::printf("- This is logical bit accounting, not physical process area.\n");
}

} // namespace

void frontend_print_stats() {
#if FRONTEND_ENABLE_TRAINING_AREA_STATS
  print_trainable_io_stats();
  print_area_stats();
  print_assumption_notes();
#else
  std::printf("frontend stats disabled by FRONTEND_ENABLE_TRAINING_AREA_STATS=0\n");
#endif
}
