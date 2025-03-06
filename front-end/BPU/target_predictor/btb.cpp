#include <cstdint>
#include <cstdio>
#include <sys/types.h>

#include "../../frontend.h"
#include "btb.h"
#include "ras.h"
#include "target_cache.h"

uint32_t btb_tag[BTB_WAY_NUM][BTB_ENTRY_NUM];
uint32_t btb_bta[BTB_WAY_NUM][BTB_ENTRY_NUM];
bool btb_valid[BTB_WAY_NUM][BTB_ENTRY_NUM];
uint32_t btb_br_type[BTB_WAY_NUM][BTB_ENTRY_NUM];
uint32_t btb_lru[BTB_ENTRY_NUM];

uint32_t btb_get_tag(uint32_t pc) { return (pc >> BTB_IDX_LEN) & BTB_TAG_MASK; }

uint32_t btb_get_idx(uint32_t pc) { return pc & BTB_IDX_MASK; }

// only for statistic
// uint64_t dir_cnt = 0;
// uint64_t call_cnt = 0;
// uint64_t ret_cnt = 0;
// uint64_t indir_cnt = 0;

void update_lru(uint32_t idx, int way) {
  uint32_t current_age = (btb_lru[idx] >> (way * 2)) & 0x3;

  // update all younger ways, but not exceed 3
  for (int i = 0; i < BTB_WAY_NUM; i++) {
    if (i == way)
      continue;
    uint32_t age = (btb_lru[idx] >> (i * 2)) & 0x3;
    if (age <= current_age) {
      uint32_t new_age = (age == 3 ? 3 : age + 1) & 0x3;
      // clear current age
      btb_lru[idx] &= ~(0x3 << (i * 2));
      // set new age
      btb_lru[idx] |= (new_age << (i * 2));
    }
  }

  // set current way to latest
  btb_lru[idx] &= ~(0x3 << (way * 2));
}

uint32_t btb_pred(uint32_t pc) {
  uint32_t idx = btb_get_idx(pc);
  uint32_t tag = btb_get_tag(pc);
  // printf("[btb_pred] idx: %x, tag: %x, pc: %x\n", idx, tag, pc);
  // find match in all ways
  for (int way = 0; way < BTB_WAY_NUM; way++) {
    if (btb_valid[way][idx] && btb_tag[way][idx] == tag) {
      // update LRU
      update_lru(idx, way);

      uint32_t br_type = btb_br_type[way][idx];
      if (br_type == BR_DIRECT) {
        // dir_cnt++;
        return btb_bta[way][idx];
      } else if (br_type == BR_CALL) {
        // call_cnt++;
        ras_push(pc + 4);
        return btb_bta[way][idx];
      } else if (br_type == BR_RET) {
        // ret_cnt++;
        uint32_t ras_top = ras_pop();
        if (ras_top != -1)
          return ras_top;
        return pc + 4;
      } else {
        // indir_cnt++;
        return tc_pred(pc);
      }
    }
  }
  DEBUG_LOG("[btb_pred] btb miss");
  return pc + 4; // btb miss
}

void btb_update(uint32_t pc, uint32_t actualAddr, uint32_t br_type,
                bool actualdir) {
  DEBUG_LOG("[btb_update] pc: %x, actualAddr: %x, br_type: %x, actualdir: %d\n",
            pc, actualAddr, br_type, actualdir);
  uint32_t idx = btb_get_idx(pc);
  uint32_t tag = btb_get_tag(pc);
  // find match in all ways
  for (int way = 0; way < BTB_WAY_NUM; way++) {
    if (btb_valid[way][idx] && btb_tag[way][idx] == tag) {
      btb_bta[way][idx] = actualAddr;
      btb_br_type[way][idx] = br_type;
      update_lru(idx, way);

      if (br_type == BR_IDIRECT) {
        tc_update(pc, actualAddr);
      }
      return;
    }
  }

  // find empty way
  for (int way = 0; way < BTB_WAY_NUM; way++) {
    if (!btb_valid[way][idx]) {
      btb_valid[way][idx] = true;
      btb_tag[way][idx] = tag;
      btb_bta[way][idx] = actualAddr;
      btb_br_type[way][idx] = br_type;
      update_lru(idx, way);
      DEBUG_LOG("[btb_update] btb allocated\n");
      if (br_type == BR_IDIRECT) {
        tc_update(pc, actualAddr);
      }
      return;
    }
  }

  // all ways are occupied, find LRU way to replace
  int lru_way = 0;
  uint32_t max_age = 0;
  for (int way = 0; way < BTB_WAY_NUM; way++) {
    uint32_t age = (btb_lru[idx] >> (way * 2)) & 0x3;
    if (age > max_age) {
      max_age = age;
      lru_way = way;
    }
  }

  // replace LRU way
  btb_valid[lru_way][idx] = true;
  btb_tag[lru_way][idx] = tag;
  btb_bta[lru_way][idx] = actualAddr;
  btb_br_type[lru_way][idx] = br_type;
  update_lru(idx, lru_way);

  if (br_type == BR_IDIRECT) {
    tc_update(pc, actualAddr);
  }
}

// using namespace std;

// // file data
// FILE *log_file;
// bool log_dir;
// uint32_t log_pc;
// uint32_t log_nextpc;
// uint32_t log_br_type;
// bool show_details = false;

// uint64_t line_cnt = 0;
// int readFileData() {
//   uint32_t num1, num2, num3, num4;
//   if (fscanf(log_file, "%u %x %x %u\n", &num1, &num2, &num3, &num4) == 4) {
//     /*printf("%u 0x%08x 0x%08x %u\n", num1, num2, num3, num4);*/
//     line_cnt++;
//     log_dir = (bool)num1;
//     log_pc = num2;
//     log_nextpc = num3;
//     log_br_type = num4;
//     /*printf("%u 0x%08x 0x%08x %u\n", log_dir, log_pc, log_nextpc,
//      * log_br_type);*/
//     return 0;
//   } else {
//     printf("log file END at line %lu\n", line_cnt);
//     return 1;
//   }
// }

// #define DEBUG false
// uint64_t control_cnt = 0;
// uint64_t btb_hit = 0;
// uint64_t dir_hit = 0;
// uint64_t ras_hit = 0;
// uint64_t call_hit = 0;
// uint64_t ret_hit = 0;
// uint64_t indir_hit = 0;

// int main() {
//   log_file = fopen("/home/watts/dhrystone/gem5output_rv/fronted_log", "r");
//   if (log_file == NULL) {
//     printf("log_file open error\n");
//     return 0;
//   }
//   int log_pc_max = DEBUG ? 10 : 1000000;
//   while (log_pc_max--) {
//     int log_eof = readFileData();
//     if (log_eof != 0)
//       break;

//     if (log_dir != 1)
//       continue; // not a control inst, need to coop with tage

//     control_cnt++;
//     uint32_t pred_npc = btb_pred(log_pc);
//     if (pred_npc == log_nextpc) {
//       btb_hit++;
//       if (log_br_type == BR_DIRECT) {
//         dir_hit++;
//       } else if (log_br_type == BR_CALL) {
//         call_hit++;
//       } else if (log_br_type == BR_RET) {
//         ret_hit++;
//       } else if (log_br_type == BR_IDIRECT) {
//         indir_hit++;
//       }
//       bht_update(log_pc, log_dir);
//       continue;
//     } else {
//       btb_update(log_pc, log_nextpc, log_br_type, log_dir);
//       bht_update(log_pc, log_dir);
//     }
//   }
//   fclose(log_file);

//   ras_hit = call_hit + ret_hit;
//   double btb_acc = (double)btb_hit / control_cnt;
//   printf("[version btb]   branch_cnt = %8lu  hit = %8lu  ACC = %6.3f%%\n",
//          control_cnt, btb_hit, btb_acc * 100);
//   double dir_acc = (double)dir_hit / dir_cnt;
//   printf("[version btb]      dir_cnt = %8lu  hit = %8lu  ACC = %6.3f%%\n",
//          dir_cnt, dir_hit, dir_acc * 100);
//   double call_acc = (double)call_hit / call_cnt;
//   printf("[version btb]     call_cnt = %8lu  hit = %8lu  ACC = %6.3f%%\n",
//          call_cnt, call_hit, call_acc * 100);
//   double ret_acc = (double)ret_hit / ret_cnt;
//   printf("[version btb]      ret_cnt = %8lu  hit = %8lu  ACC = %6.3f%%\n",
//          ret_cnt, ret_hit, ret_acc * 100);
//   double ras_acc = (double)ras_hit / (call_cnt + ret_cnt);
//   printf("[version btb]      ras_cnt = %8lu  hit = %8lu  ACC = %6.3f%%\n",
//          ret_cnt + call_cnt, ras_hit, ras_acc * 100);
//   double indir_acc = (double)indir_hit / indir_cnt;
//   printf("[version btb]    indir_cnt = %8lu  hit = %8lu  ACC = %6.3f%%\n",
//          indir_cnt, indir_hit, indir_acc * 100);
//   return 0;
// }
