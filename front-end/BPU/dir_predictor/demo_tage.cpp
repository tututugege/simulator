#include "demo_tage.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#define BASE_ENTRY_NUM 2048
#define GHR_LENGTH 256
#define TN_MAX 4 // 0-indexed, which means 0,1,2,3
#define TN_ENTRY_NUM 4096
#define FH_N_MAX 3              // how many different types of Folded history
#define USEFUL_RESET_VAL 262144 // 256K

uint32_t useful_reset_cnt = 0;
bool useful_msb_reset = true;

// base_predictor : 2-bit Saturating Counter
int base_counter[BASE_ENTRY_NUM];

// Global history register
bool GHR[GHR_LENGTH];

// TN table entry
uint8_t tag_table[TN_MAX][TN_ENTRY_NUM];
uint8_t cnt_table[TN_MAX][TN_ENTRY_NUM];
uint8_t useful_table[TN_MAX][TN_ENTRY_NUM];

// uint8_t pcpn; // prediction provider
// uint8_t altpcpn;
// bool base_pred;
// bool pcpn_pred;
// bool alt_pred;
// uint32_t base_idx;

uint32_t index[TN_MAX];

// Folded history
// TN FH0 FH1 FH2 GHR_LENGTH
// 0 8 8 7 8
// 1 11 8 7 13
// 2 11 8 7 32
// 3 11 8 7 119
uint32_t FH[FH_N_MAX][TN_MAX];
uint32_t ghr_length[TN_MAX] = {8, 13, 32, 119};
uint32_t fh_length[FH_N_MAX][TN_MAX] = {8, 11, 11, 11, 8, 8, 8, 8, 7, 7, 7, 7};

void show_HR();
// well designed !
// x --> y compress
// 1. old << 1
// 2. {old, new_history xor old[highest]}
// 3. (((x % y) - 1) + 1) % y -> xor oldest bit to discard it --> x % y
void TAGE_update_FH(bool new_history) {
  for (int k = 0; k < FH_N_MAX; k++) {
    for (int i = 0; i < TN_MAX; i++) {
      bool old_highest;
      old_highest = (FH[k][i] >> (fh_length[k][i] - 1)) & (0x1);
      FH[k][i] = (FH[k][i] << 1) & ((0x1 << fh_length[k][i]) - 1);
      FH[k][i] |= new_history ^ old_highest;
      FH[k][i] ^= GHR[ghr_length[i]] << (ghr_length[i] % fh_length[k][i]);
    }
  }
}

// XiangShan : FH1 xor FH2 xor (PC >> 1)
uint8_t cal_tag(uint32_t PC, int n) {
  uint8_t ret = FH[1][n] ^ FH[2][n] ^ (PC >> 5) & (0xff);
  return ret;
}

// XiangShan : FH xor (PC >> 1)
// 4096 entries
uint32_t cal_index(uint32_t PC, int n) {
  uint32_t ret = FH[0][n] ^ (PC >> 5) & (0xfff);
  return ret;
}

uint8_t tag[TN_MAX]; // 8-bit tag
pred_out TAGE_get_prediction(uint32_t PC) {

  // base_pred : simple bimodal predictor
  // 2048 entries
  uint32_t base_idx = PC % BASE_ENTRY_NUM; // PC[x:0]
  bool base_pred = base_counter[base_idx] >= 2 ? true : false;

  ///////////////////////////////////////////////////////////////////////////////////////
  /*----------------------------------TN_TABLE-------------------------------------*/
  ///////////////////////////////////////////////////////////////////////////////////////

  // get tag for all Tn
  for (int i = 0; i < TN_MAX; i++) {
    tag[i] = cal_tag(PC, i);
  }

  // get index for all Tn
  for (int i = 0; i < TN_MAX; i++) {
    index[i] = cal_index(PC, i);
  }

  // printf("now tag: ");
  // for (int i = 0; i < TN_MAX; i++) {
  //   printf("%d ", tag[i]);
  // }
  // printf("\n");
  // printf("now index: ");
  // for (int i = 0; i < TN_MAX; i++) {
  //   printf("%d ", index[i]);
  // }
  // printf("\n");

  uint8_t pcpn = TN_MAX;
  uint8_t altpcpn = TN_MAX;
  bool alt_pred = false;
  bool pcpn_pred = false;
  // Take the longest history entry
  for (int i = TN_MAX - 1; i >= 0; i--) {
    if (tag_table[i][index[i]] == tag[i]) {
      pcpn = i;
      break;
    }
  }
  // get the altpcpn info for updating policies
  for (int i = pcpn - 1; i >= 0; i--) {
    if (tag_table[i][index[i]] == tag[i]) {
      altpcpn = i;
      break;
    }
  }
  if (altpcpn >= TN_MAX) { // alt not found
    alt_pred = base_pred;
  } else {
    if (cnt_table[altpcpn][index[altpcpn]] >= 4) {
      alt_pred = true;
    } else {
      alt_pred = false;
    }
  }

  if (pcpn >= TN_MAX) { // pcpn not found
    pcpn_pred = base_pred;
    return {base_pred, alt_pred, pcpn, altpcpn};
  }
  if (cnt_table[pcpn][index[pcpn]] >= 4) {
    pcpn_pred = true;
    return {true, alt_pred, pcpn, altpcpn};
  }
  pcpn_pred = false;
  return {false, alt_pred, pcpn, altpcpn};
}

uint8_t bit_update_2(uint8_t data, bool is_inc) {
  uint8_t ret;
  if (is_inc) {
    if (data >= 3)
      ret = 3;
    else
      ret = data + 1;

  } else {
    if (data == 0)
      ret = 0;
    else
      ret = data - 1;
  }
  return ret;
}

uint8_t bit_update_3(uint8_t data, bool is_inc) {
  uint8_t ret;
  if (is_inc) {
    if (data >= 7)
      ret = 7;
    else
      ret = data + 1;

  } else {
    if (data == 0)
      ret = 0;
    else
      ret = data - 1;
  }
  return ret;
}

void do_GHR_update(bool real_dir) {
  for (int i = GHR_LENGTH - 1; i > 0; i--) {
    GHR[i] = GHR[i - 1];
  }
  GHR[0] = real_dir;
  // printf("now GHR: ");
  // for (int i = 0; i < GHR_LENGTH; i++) {
  //   printf("%d", GHR[i]);
  // }
  // printf("\n");
}

// real_dir == trur <-> taken
void TAGE_do_update(uint32_t PC, bool real_dir, pred_out pred_out) {
  /*printf("[TAGEupd] %x %d %d %d %x %x\n", PC, real_dir, pred_out.pred,*/
  /*       pred_out.altpred, pred_out.pcpn, pred_out.altpcpn);*/
  bool pred_dir = pred_out.pred;
  bool alt_pred = pred_out.altpred;
  uint8_t pcpn = pred_out.pcpn;
  uint8_t altpcpn = pred_out.altpcpn;

  // get tag for all Tn
  for (int i = 0; i < TN_MAX; i++) {
    tag[i] = cal_tag(PC, i);
  }
  // get index for all Tn
  for (int i = 0; i < TN_MAX; i++) {
    index[i] = cal_index(PC, i);
  }

  // printf("TAGE_do_update(%x, %d, %d, %d, %d, %d);\n", PC, real_dir, pred_dir,
  //        alt_pred, pcpn, altpcpn);
  // if (PC == 0x11e0c) {
  //   show_HR();
  // }
  // 1. update 2-bit useful counter
  // pcpn found
  if (pcpn < TN_MAX) {
    if ((pred_dir != alt_pred)) {
      if (pred_dir == real_dir) {
        useful_table[pcpn][index[pcpn]] =
            bit_update_2(useful_table[pcpn][index[pcpn]], true);
        /*printf("adding %d %d == %u\n", pcpn, index[pcpn],*/
        /*       useful_table[pcpn][index[pcpn]]);*/
      } else {
        useful_table[pcpn][index[pcpn]] =
            bit_update_2(useful_table[pcpn][index[pcpn]], false);
      }
    }

    // 2. update cnt
    if (real_dir == true) {
      cnt_table[pcpn][index[pcpn]] =
          bit_update_3(cnt_table[pcpn][index[pcpn]], true);

    } else {
      cnt_table[pcpn][index[pcpn]] =
          bit_update_3(cnt_table[pcpn][index[pcpn]], false);
    }
  }
  // pcpn not found, update base_counter
  else {
    uint32_t base_idx = PC % BASE_ENTRY_NUM; // PC[x:0]
    if (real_dir == true) {
      base_counter[base_idx] = bit_update_2(base_counter[base_idx], true);
    } else {
      base_counter[base_idx] = bit_update_2(base_counter[base_idx], false);
    }
  }

  // 3. pred_dir != real_dir
  // If the provider component Ti is not the component using
  // the longest history (i.e., i < M) , we try to allocate an entry on a
  // predictor component Tk using a longer history than Ti (i.e., i < k < M)

  if (pred_dir != real_dir) {

    /*printf("woooops");*/
    bool new_entry_found_j = false;
    int j_i, j_j;
    bool new_entry_found_k = false;
    int k_i, k_j;

    if (pcpn <= TN_MAX - 2 ||
        pcpn == TN_MAX) { // pcpn is NOT using the longest history or not found

      for (int i = pcpn == TN_MAX ? 0 : (pcpn + 1); i < TN_MAX; i++) {
        // try to find a useful==0
        /*for (int j = 0; j < TN_ENTRY_NUM; j++) {*/
        int j = index[i];
        if (useful_table[i][j] == 0) {
          if (new_entry_found_j == false) {
            new_entry_found_j = true;
            j_i = i;
            j_j = j;
            continue;
          } else {
            new_entry_found_k = true;
            k_i = i;
            k_j = j;
            break;
          }
          /*tag_table[i][j] = cal_tag(PC, i);*/
          /*cnt_table[i][j] = real_dir ? 4 : 3; // weak correct?*/
          /*useful_table[i][j] = 0;*/
          /*new_entry_found = true;*/
          /*break;*/
        }
        /*}*/
        /*if (new_entry_found_k == true)*/
        /*  break;*/
      }
      if (new_entry_found_j == false) { // no new entry allocated
        for (int i = pcpn + 1; i < TN_MAX; i++) {
          /*for (int j = 0; j < TN_ENTRY_NUM; j++) {*/
          int j = index[i];
          useful_table[i][j] = bit_update_2(useful_table[i][j], false);
          /*}*/
        }
      }
      // alocate new entry
      else {
        int random_pick = random() % 3;
        /*printf("rp = %d\n", random_pick);*/

        if (new_entry_found_k == true && random_pick == 0) {
          /*tag_table[k_i][k_j] = cal_tag(PC, k_i);*/
          tag_table[k_i][k_j] = tag[k_i];
          cnt_table[k_i][k_j] = real_dir ? 4 : 3; // weak correct?
          useful_table[k_i][k_j] = 0;
          /*printf("Knew tag %u i%d j%d\n", tag_table[k_i][k_j], k_i, k_j);*/
        } else {
          /*tag_table[j_i][j_j] = cal_tag(PC, j_i);*/
          tag_table[j_i][j_j] = tag[j_i];
          cnt_table[j_i][j_j] = real_dir ? 4 : 3; // weak correct?
          useful_table[j_i][j_j] = 0;
          /*printf("Jnew tag %u i%d j%d\n", tag_table[j_i][j_j], j_i, j_j);*/
        }
      }
    }
  }

  // 4. Periodically, the whole column of
  // most significant bits of the u counters is reset to zero, then whole column
  // of least significant bits are reset.
  useful_reset_cnt++;
  uint32_t u_clear_cnt = useful_reset_cnt;
  uint32_t u_cnt = u_clear_cnt & (0x7ff);
  uint32_t row_cnt = (u_clear_cnt >> 11) & (0xfff);
  bool u_msb_reset = ((u_clear_cnt) >> 23) & (0x1);

  if (u_cnt == 0) {
    // printf("[TAGE_do_update] cleaning useful table\n");
    // reset the msb
    if (u_msb_reset == true) {
      for (int i = 0; i < TN_MAX; i++) {
        /*for (int j = 0; j < TN_ENTRY_NUM; j++) {*/
        useful_table[i][row_cnt] &= 0x1;
        /*}*/
      }
    }
    // reset the lsb
    else {
      for (int i = 0; i < TN_MAX; i++) {
        /*for (int j = 0; j < TN_ENTRY_NUM; j++) {*/
        useful_table[i][row_cnt] &= 0x2;
        /*}*/
      }
    }
    /*useful_msb_reset = 1 ^ useful_msb_reset; // do flip*/
  }

  // // 5. update GHR and compress history
  // do_GHR_update(real_dir);
  // TAGE_update_FH(real_dir);
}

///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
// this is only for TESTING
// this is only for TESTING
// this is only for TESTING
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
void show_TAGE() {
  printf("base \n");
  for (int i = 0; i < BASE_ENTRY_NUM; i++) {
    if (base_counter[i] != 0)
      printf("index %3x base_counter %1x\n", i, base_counter[i]);
  }
  for (int i = 0; i < TN_MAX; i++) {
    printf("T %d\n", i);
    for (int j = 0; j < TN_ENTRY_NUM; j++) {
      if (tag_table[i][j] != 0 || cnt_table[i][j] != 0 ||
          useful_table[i][j] != 0)
        printf("index %3x tag %2x cnt %1x useful %1x\n", j, tag_table[i][j],
               cnt_table[i][j], useful_table[i][j]);
    }
  }
}

void show_HR() {
  for (int i = 0; i < GHR_LENGTH; i++) {
    printf("%d", GHR[i]);
  }
  printf("\n");
  for (int i = 0; i < FH_N_MAX; i++) {
    for (int j = 0; j < TN_MAX; j++) {
      printf("FH%d%d %u\n", i, j, FH[i][j]);
    }
  }
}
#define DEBUG true
// #define DEBUG false
// #define DEBUG_MAIN
#ifdef DEBUG_MAIN
using namespace std;

FILE *log_file;
bool log_dir;
uint32_t log_pc;
uint32_t log_nextpc;
uint32_t log_br_type;
bool show_details = false;
uint64_t line_cnt = 0;

int readFileData() {
  uint32_t num1, num2, num3, num4;
  if (fscanf(log_file, "%u %x %x %u\n", &num1, &num2, &num3, &num4) == 4) {
    line_cnt++;
    log_dir = (bool)num1;
    log_pc = num2;
    log_nextpc = num3;
    log_br_type = num4;
    return 0;
  } else {
    printf("log file END at line %lu\n", line_cnt);
    return 1;
  }
}

uint64_t inst_cnt = 0;
uint64_t bp_cnt = 0;
bool bp_dir;

int main() {
  srand(time(0));
  log_file = fopen("/home/watts/dhrystone/gem5output_rv/fronted_log", "r");
  if (log_file == NULL) {
    printf("log_file open error\n");
    return 0;
  }

  int log_pc_max = DEBUG ? 100 : 1000000;
  while (log_pc_max--) {
    int log_eof = readFileData();
    if (log_eof != 0)
      break;

    inst_cnt++;
    bp_dir = TAGE_get_prediction(log_pc);
    TAGE_do_update(log_pc, log_dir, bp_dir);
    if (show_details == true) {
      printf("TAGE_bp = %d", bp_dir);
      if (bp_dir == log_dir)
        printf("HIT%d", log_dir);
      printf("\n");
    }
    if (bp_dir == log_dir)
      bp_cnt++;
  }
  fclose(log_file);

  double acc = (double)bp_cnt / inst_cnt;
  printf("[version tage]     inst_cnt = %lu bp_cnt = %lu ACC = %.3f%%\n",
         inst_cnt, bp_cnt, acc * 100);
  return 0;
}
#endif
// int main() {
//   srand(7);
//   bool on = 0;
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x1008c));
//   TAGE_do_update(0x1008c, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x10090));
//   TAGE_do_update(0x10090, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x10094));
//   TAGE_do_update(0x10094, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x10098));
//   TAGE_do_update(0x10098, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x1009c));
//   TAGE_do_update(0x1009c, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x100a0));
//   TAGE_do_update(0x100a0, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x100a4));
//   TAGE_do_update(0x100a4, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x100a8));
//   TAGE_do_update(0x100a8, 1, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11dd4));
//   TAGE_do_update(0x11dd4, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11dd8));
//   TAGE_do_update(0x11dd8, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11ddc));
//   TAGE_do_update(0x11ddc, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11de0));
//   TAGE_do_update(0x11de0, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11de4));
//   TAGE_do_update(0x11de4, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11de8));
//   TAGE_do_update(0x11de8, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11dec));
//   TAGE_do_update(0x11dec, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11df0));
//   TAGE_do_update(0x11df0, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11df4));
//   TAGE_do_update(0x11df4, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11df8));
//   TAGE_do_update(0x11df8, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11dfc));
//   TAGE_do_update(0x11dfc, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e00));
//   TAGE_do_update(0x11e00, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e04));
//   TAGE_do_update(0x11e04, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e08));
//   TAGE_do_update(0x11e08, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e0c));
//   TAGE_do_update(0x11e0c, 1, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11df8));
//   TAGE_do_update(0x11df8, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11dfc));
//   TAGE_do_update(0x11dfc, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e00));
//   TAGE_do_update(0x11e00, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e04));
//   TAGE_do_update(0x11e04, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e08));
//   TAGE_do_update(0x11e08, 0, 0);
//   show_TAGE();
//   // if (on)
//   TAGE_get_prediction(0x11e0c);
//   show_TAGE();
//   TAGE_do_update(0x11e0c, 1, 0);
//   show_TAGE();
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11df8));
//   TAGE_do_update(0x11df8, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11dfc));
//   TAGE_do_update(0x11dfc, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e00));
//   TAGE_do_update(0x11e00, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e04));
//   TAGE_do_update(0x11e04, 0, 0);
//   if (on)
//     printf("%d\n", TAGE_get_prediction(0x11e08));
//   TAGE_do_update(0x11e08, 0, 0);
//   show_TAGE();
//   printf("%d\n", TAGE_get_prediction(0x11e0c));
//   show_TAGE();
//   TAGE_do_update(0x11e0c, 1, 1);
//   return 0;
// }
