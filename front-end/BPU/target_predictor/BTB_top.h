#ifndef BTB_TOP_H
#define BTB_TOP_H

#include "../../frontend.h"
#include "../BPU_configs.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>

struct BtbSetData {
  uint32_t tag[BTB_WAY_NUM];
  uint32_t bta[BTB_WAY_NUM];
  bool valid[BTB_WAY_NUM];     // the valid bit in BTB_ENTRY
  uint8_t useful[BTB_WAY_NUM]; // 3-bit
};

struct TcSetData {
  uint32_t target[TC_WAY_NUM];
  uint32_t tag[TC_WAY_NUM];
  bool valid[TC_WAY_NUM];
  uint8_t useful[TC_WAY_NUM];
};

struct HitCheckOut {
  int hit_way; // 0123
  bool hit;
};

class BTB_TOP {
public:
  // ------------------------------------------------------------------------
  // 状态枚举定义（需要在结构体之前定义）
  // ------------------------------------------------------------------------
  enum State {
    S_IDLE = 0,
    S_STAGE2 = 1,
    S_IDLE_WAIT_DATA = 2,
    S_STAGE2_WAIT_DATA = 3
  };

  struct InputPayload {
    uint32_t pred_pc;
    bool pred_req;
    bool upd_valid;
    uint32_t upd_pc;
    uint32_t upd_actual_addr;
    uint8_t upd_br_type_in;
    bool upd_actual_dir;
  };

  struct OutputPayload {
    uint32_t pred_target;
    bool btb_pred_out_valid;
    bool btb_update_done;
    bool busy;
  };

  // 状态输入结构体（包含所有寄存器）
  struct StateInput {
    State state;
    // input latches
    bool do_pred_latch;
    bool do_upd_latch;
    uint32_t upd_pc_latch;
    uint32_t upd_actual_addr_latch;
    uint8_t upd_br_type_latch;
    bool upd_actual_dir_latch;
    // pipeline latches
    uint32_t pred_calc_pc_latch;
    uint32_t pred_calc_btb_tag_latch;
    uint32_t pred_calc_btb_idx_latch;
    uint32_t pred_calc_type_idx_latch;
    uint32_t pred_calc_bht_idx_latch;
    // uint32_t pred_calc_tc_idx_latch;
    uint32_t upd_calc_next_bht_val_latch;
    HitCheckOut upd_calc_hit_info_latch;
    int upd_calc_victim_way_latch;
    int upd_calc_w_target_way_latch;
    uint8_t upd_calc_next_useful_val_latch;
    bool upd_calc_writes_btb_latch;
  };

  // Index生成结果
  struct IndexResult {
    uint32_t btb_idx;
    uint32_t type_idx;
    uint32_t bht_idx;
    uint32_t tc_idx;
    uint32_t tag;
    bool read_address_valid;
  };

  // 内存读取结果
  struct MemReadResult {
    BtbSetData r_btb_set;
    TcSetData r_tc_set;
    uint8_t r_type;
    uint32_t r_bht;
    bool read_data_valid;
  };

  // 三阶段 Read 阶段输出
  struct ReadData {
    StateInput state_in;
    IndexResult idx_1;
    MemReadResult mem_1;
    IndexResult idx_2;
    MemReadResult mem_2;

    bool sram_delay_active;
    int sram_delay_counter;
    MemReadResult sram_delayed_data;
    bool new_read_valid;
    MemReadResult new_read_data;
    uint32_t sram_prng_state;

    bool pred_read_valid;
    uint32_t pred_btb_idx;
    uint32_t pred_type_idx;
    uint32_t pred_bht_idx;
    uint32_t pred_tc_idx;
    uint32_t pred_tag;
    uint8_t pred_type_data;
    uint32_t pred_bht_data;
    BtbSetData pred_btb_set;
    TcSetData pred_tc_set;

    bool upd_read_valid;
    uint32_t upd_btb_idx;
    uint32_t upd_type_idx;
    uint32_t upd_bht_idx;
    uint32_t upd_tag;
    uint32_t upd_bht_data;
    uint32_t upd_next_bht_data;
    BtbSetData upd_btb_set;

    bool upd_tc_read_valid;
    uint32_t upd_tc_write_idx;
    uint32_t upd_tc_write_tag;
    TcSetData upd_tc_set;
  };

  // 组合逻辑计算结果结构体
  struct CombResult {
    State next_state;
    uint32_t btb_idx;
    uint32_t type_idx;
    uint32_t bht_idx;
    uint32_t tc_idx;
    uint32_t tag;
    BtbSetData r_btb_set;
    uint8_t r_type;
    uint32_t r_bht;
    HitCheckOut hit_info;
    uint32_t pred_target;

    // Update Path Calculation
    uint32_t next_bht_val;
    HitCheckOut upd_hit_info;
    int victim_way;
    int w_target_way;
    uint8_t next_useful_val;
    bool upd_writes_btb;

    // Stage 1 calculation results (for pipeline)
    uint32_t s1_pred_tag;
    uint32_t s1_pred_btb_idx;
    uint32_t s1_pred_type_idx;
    uint32_t s1_pred_bht_idx;
    uint32_t s1_pred_tc_idx;

    OutputPayload out_regs;

    bool sram_delay_active_next;
    int sram_delay_counter_next;
    MemReadResult sram_delayed_data_next;
    uint32_t sram_prng_state_next;

    bool do_pred_latch_next;
    bool do_upd_latch_next;
    uint32_t upd_pc_latch_next;
    uint32_t upd_actual_addr_latch_next;
    uint8_t upd_br_type_latch_next;
    bool upd_actual_dir_latch_next;
    uint32_t pred_calc_pc_latch_next;
    uint32_t pred_calc_btb_tag_latch_next;
    uint32_t pred_calc_btb_idx_latch_next;
    uint32_t pred_calc_type_idx_latch_next;
    uint32_t pred_calc_bht_idx_latch_next;
    uint32_t upd_calc_next_bht_val_latch_next;
    HitCheckOut upd_calc_hit_info_latch_next;
    int upd_calc_victim_way_latch_next;
    int upd_calc_w_target_way_latch_next;
    uint8_t upd_calc_next_useful_val_latch_next;
    bool upd_calc_writes_btb_latch_next;
    bool type_we_commit;
    uint32_t type_wr_idx;
    uint8_t type_wdata_commit;
    bool bht_we_commit;
    uint32_t bht_wr_idx;
    uint32_t bht_wdata_commit;
    bool tc_we_commit;
    int tc_wr_way;
    uint32_t tc_wr_idx;
    uint32_t tc_wdata_commit;
    uint32_t tc_wtag_commit;
    bool tc_wvalid_commit;
    uint8_t tc_wuseful_commit;
    bool btb_we_commit;
    int btb_wr_way;
    uint32_t btb_wr_idx;
    uint32_t btb_wr_tag;
    uint32_t btb_wr_bta;
    bool btb_wr_valid;
    uint8_t btb_wr_useful;
  };

  struct BtbGenIndexPreCombIn {
    InputPayload inp;
    StateInput state_in;
  };

  struct BtbGenIndexPreCombOut {
    IndexResult idx;
  };

  struct BtbMemReadPreCombIn {
    IndexResult idx_1;
    StateInput state_in;
    uint8_t pre_type_data;
    uint32_t pre_bht_data;
  };

  struct BtbMemReadPreCombOut {
    MemReadResult mem;
  };

  struct BtbGenIndexPostCombIn {
    InputPayload inp;
    StateInput state_in;
    IndexResult idx_1;
    MemReadResult mem_1;
  };

  struct BtbGenIndexPostCombOut {
    IndexResult idx_2;
  };

  struct BtbCoreCombIn {
    InputPayload inp;
    StateInput state_in;
    IndexResult idx_2;
    MemReadResult mem_2;
  };

  struct BtbCoreCombOut {
    CombResult result;
  };

  struct BtbCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct BtbCombOut {
    OutputPayload out_regs;
    CombResult req;
  };

  struct BtbXorshift32CombIn {
    uint32_t state;
  };

  struct BtbXorshift32CombOut {
    uint32_t next_state;
  };

  struct BtbGetTagCombIn {
    uint32_t pc;
  };

  struct BtbGetTagCombOut {
    uint32_t tag;
  };

  struct BtbGetIdxCombIn {
    uint32_t pc;
  };

  struct BtbGetIdxCombOut {
    uint32_t idx;
  };

  struct BtbGetTypeIdxCombIn {
    uint32_t pc;
  };

  struct BtbGetTypeIdxCombOut {
    uint32_t idx;
  };

  struct BhtGetIdxCombIn {
    uint32_t pc;
  };

  struct BhtGetIdxCombOut {
    uint32_t idx;
  };

  struct TcGetIdxCombIn {
    uint32_t pc;
    uint32_t bht_value;
  };

  struct TcGetIdxCombOut {
    uint32_t idx;
  };

  struct TcGetTagCombIn {
    uint32_t pc;
  };

  struct TcGetTagCombOut {
    uint32_t tag;
  };

  struct BhtNextStateCombIn {
    uint32_t current_bht;
    uint8_t br_type;
    bool pc_dir;
    uint32_t actual_target;
  };

  struct BhtNextStateCombOut {
    uint32_t next_bht;
  };

  struct UsefulNextStateCombIn {
    uint8_t current_val;
    bool correct;
  };

  struct UsefulNextStateCombOut {
    uint8_t next_val;
  };

  struct BtbHitCheckCombIn {
    BtbSetData set_data;
    uint32_t tag;
  };

  struct BtbHitCheckCombOut {
    HitCheckOut hit_info;
  };

  struct TcHitCheckCombIn {
    TcSetData set_data;
    uint32_t tag;
  };

  struct TcHitCheckCombOut {
    HitCheckOut hit_info;
  };

  struct BtbPredOutputCombIn {
    uint32_t pc;
    uint8_t br_type;
    HitCheckOut hit_info;
    BtbSetData set_data;
    TcSetData tc_set;
  };

  struct BtbPredOutputCombOut {
    uint32_t pred_target;
  };

  struct BtbVictimSelectCombIn {
    BtbSetData set_data;
  };

  struct BtbVictimSelectCombOut {
    int victim_way;
  };

  struct TcVictimSelectCombIn {
    TcSetData set_data;
  };

  struct TcVictimSelectCombOut {
    int victim_way;
  };

private:
  // 内存存储
  uint32_t mem_btb_tag[BTB_WAY_NUM][BTB_ENTRY_NUM];
  uint32_t mem_btb_bta[BTB_WAY_NUM][BTB_ENTRY_NUM];
  bool mem_btb_valid[BTB_WAY_NUM][BTB_ENTRY_NUM];
  uint8_t mem_btb_useful[BTB_WAY_NUM][BTB_ENTRY_NUM];

  uint8_t mem_type[BTB_TYPE_ENTRY_NUM];
  uint32_t mem_bht[BHT_ENTRY_NUM];
  uint32_t mem_tc_target[TC_WAY_NUM][TC_ENTRY_NUM];
  uint32_t mem_tc_tag[TC_WAY_NUM][TC_ENTRY_NUM];
  bool mem_tc_valid[TC_WAY_NUM][TC_ENTRY_NUM];
  uint8_t mem_tc_useful[TC_WAY_NUM][TC_ENTRY_NUM];

  // Pipeline Registers
  State state;
  bool do_pred_latch;
  bool do_upd_latch;
  uint32_t upd_pc_latch;
  uint32_t upd_actual_addr_latch;
  uint8_t upd_br_type_latch;
  bool upd_actual_dir_latch;

  // Pipeline Regs (S1 to S2)
  uint32_t pred_calc_pc_latch;
  uint32_t pred_calc_btb_tag_latch;
  uint32_t pred_calc_btb_idx_latch;
  uint32_t pred_calc_type_idx_latch;
  uint32_t pred_calc_bht_idx_latch;
  // uint32_t pred_calc_tc_idx_latch;

  // For Update Writeback (S1 calc result):
  uint32_t upd_calc_next_bht_val_latch;
  HitCheckOut upd_calc_hit_info_latch;
  int upd_calc_victim_way_latch;
  int upd_calc_w_target_way_latch;
  uint8_t upd_calc_next_useful_val_latch;
  bool upd_calc_writes_btb_latch;

  // Outputs Registers
  OutputPayload out_regs;

  // SRAM延迟模拟相关变量（用于BTB和TC表项）
  bool sram_delay_active;           // 是否正在进行延迟
  int sram_delay_counter;            // 剩余延迟周期数
  MemReadResult sram_delayed_data;  // 延迟期间保存的数据（包含BTB和TC）
  bool sram_new_req_this_cycle;      // 本周期是否有新的读请求（在step_pipeline中设置，step_seq中使用）
  uint32_t sram_prng_state;          // 固定种子伪随机状态

public:
  BTB_TOP() { reset(); }

  void reset() {
    std::memset(mem_btb_tag, 0, sizeof(mem_btb_tag));
    std::memset(mem_btb_bta, 0, sizeof(mem_btb_bta));
    std::memset(mem_btb_valid, 0, sizeof(mem_btb_valid));
    std::memset(mem_btb_useful, 0, sizeof(mem_btb_useful));
    std::memset(mem_type, 0, sizeof(mem_type));
    std::memset(mem_bht, 0, sizeof(mem_bht));
    std::memset(mem_tc_target, 0, sizeof(mem_tc_target));
    std::memset(mem_tc_tag, 0, sizeof(mem_tc_tag));
    std::memset(mem_tc_valid, 0, sizeof(mem_tc_valid));
    std::memset(mem_tc_useful, 0, sizeof(mem_tc_useful));

    state = S_IDLE;
    do_pred_latch = false;
    do_upd_latch = false;
    upd_pc_latch = 0;
    upd_actual_addr_latch = 0;
    upd_br_type_latch = 0;
    upd_actual_dir_latch = false;

    pred_calc_pc_latch = 0;
    pred_calc_btb_tag_latch = 0;
    pred_calc_btb_idx_latch = 0;
    pred_calc_type_idx_latch = 0;
    pred_calc_bht_idx_latch = 0;
    // pred_calc_tc_idx_latch = 0;

    upd_calc_next_bht_val_latch = 0;
    memset(&upd_calc_hit_info_latch, 0, sizeof(HitCheckOut));
    upd_calc_victim_way_latch = 0;
    upd_calc_w_target_way_latch = 0;
    upd_calc_next_useful_val_latch = 0;
    upd_calc_writes_btb_latch = false;

    memset(&out_regs, 0, sizeof(OutputPayload));
    
    // Init SRAM delay simulation
    sram_delay_active = false;
    sram_delay_counter = 0;
    sram_new_req_this_cycle = false;
    memset(&sram_delayed_data, 0, sizeof(MemReadResult));
    sram_prng_state = 0x2468ace1u;
  }

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 第一次生成Index（不包含 tc_idx）
  // ------------------------------------------------------------------------
  void btb_gen_index_pre_comb(const BtbGenIndexPreCombIn &in,
                              BtbGenIndexPreCombOut &out) const {
    const InputPayload &inp = in.inp;
    const StateInput &state_in = in.state_in;
    IndexResult &idx = out.idx;
    memset(&idx, 0, sizeof(IndexResult));

    bool read_upd_in = (state_in.state == S_IDLE && inp.upd_valid);
    bool read_upd_wait = (state_in.state == S_IDLE_WAIT_DATA && state_in.do_upd_latch);
    bool read_pred = (state_in.state == S_STAGE2 && state_in.do_pred_latch) || (state_in.state == S_STAGE2_WAIT_DATA && state_in.do_pred_latch);

    // Table Address Mux Logic (类似 TAGE)
    if (read_pred) {
      // Stage 2: 使用 pipeline registers 中的地址
      idx.btb_idx = state_in.pred_calc_btb_idx_latch;
      idx.type_idx = state_in.pred_calc_type_idx_latch;
      idx.bht_idx = state_in.pred_calc_bht_idx_latch;
      // gen_index_1 not set tc index
      idx.tag = state_in.pred_calc_btb_tag_latch;
      idx.read_address_valid = true;

    } else if (read_upd_in || read_upd_wait) {
      // Update path: 使用 update PC
      uint32_t upd_pc = read_upd_in ? inp.upd_pc : state_in.upd_pc_latch;
      idx.btb_idx = btb_get_idx_value(upd_pc);
      idx.type_idx = btb_get_type_idx_value(upd_pc);
      idx.bht_idx = bht_get_idx_value(upd_pc);
      idx.tag = btb_get_tag_value(upd_pc);
      // gen_index_1 not set tc index
      idx.read_address_valid = true;
    } else {
      idx.read_address_valid = false;
    }

  }

  // ------------------------------------------------------------------------
  // 第一次内存读取 (TABLE READ) - 读取 BHT、Type
  // ------------------------------------------------------------------------
  void btb_mem_read_pre_comb(const BtbMemReadPreCombIn &in,
                             BtbMemReadPreCombOut &out) const {
    const IndexResult &idx_1 = in.idx_1;
    MemReadResult &mem = out.mem;
    memset(&mem, 0, sizeof(MemReadResult));

    // Memory Read data comes from seq_read snapshot.
    if (idx_1.read_address_valid) {
      mem.r_type = in.pre_type_data;
      mem.r_bht = in.pre_bht_data;
      // BTB 和 TC 不在这里读取，需要在第二次 mem_read 中读取
      mem.read_data_valid = true; // reg file read, always valid
    } else {
      mem.read_data_valid = false;
    }
  }

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 第二次生成Index（使用第一次读取的 BHT 值计算 tc_idx）
  // ------------------------------------------------------------------------
  void btb_gen_index_post_comb(const BtbGenIndexPostCombIn &in,
                               BtbGenIndexPostCombOut &out) const {
    const InputPayload &inp = in.inp;
    const StateInput &state_in = in.state_in;
    const IndexResult &idx_1 = in.idx_1;
    const MemReadResult &mem_1 = in.mem_1;
    IndexResult &idx_2 = out.idx_2;
    memset(&idx_2, 0, sizeof(IndexResult));

    // 复制第一次的索引结果
    idx_2.btb_idx = idx_1.btb_idx;
    idx_2.type_idx = idx_1.type_idx;
    idx_2.bht_idx = idx_1.bht_idx;
    idx_2.tag = idx_1.tag;

    // 计算 tc_idx（使用第一次读取的 BHT 值）
    if (state_in.state == S_STAGE2 && state_in.do_pred_latch) {
      // Stage 2: 使用本周期刚刚从 mem_1 读出的有效 BHT，和 latch 下来的 PC 进行计算
      // mem_1.r_bht 在 Stage 2 是有效的，因为 gen_index_1 在 Stage 2 开启了读取
      idx_2.tc_idx = tc_get_idx_value(state_in.pred_calc_pc_latch, mem_1.r_bht);
      idx_2.read_address_valid = true;
    } else if (state_in.state == S_IDLE && inp.upd_valid) {
      // Update path: 使用第一次读取的 BHT 值计算 tc_idx
      idx_2.tc_idx = tc_get_idx_value(inp.upd_pc, mem_1.r_bht);
      idx_2.read_address_valid = true;
    } else {
      idx_2.read_address_valid = false;
    }

  }

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 计算部分
  // ------------------------------------------------------------------------
  void btb_core_comb(const BtbCoreCombIn &in, BtbCoreCombOut &out) const {
    const InputPayload &inp = in.inp;
    const StateInput &state_in = in.state_in;
    const IndexResult &idx_2 = in.idx_2;
    const MemReadResult &mem_2 = in.mem_2;
    CombResult &comb = out.result;
    memset(&comb, 0, sizeof(CombResult));
    comb.do_pred_latch_next = state_in.do_pred_latch;
    comb.do_upd_latch_next = state_in.do_upd_latch;
    comb.upd_pc_latch_next = state_in.upd_pc_latch;
    comb.upd_actual_addr_latch_next = state_in.upd_actual_addr_latch;
    comb.upd_br_type_latch_next = state_in.upd_br_type_latch;
    comb.upd_actual_dir_latch_next = state_in.upd_actual_dir_latch;
    comb.pred_calc_pc_latch_next = state_in.pred_calc_pc_latch;
    comb.pred_calc_btb_tag_latch_next = state_in.pred_calc_btb_tag_latch;
    comb.pred_calc_btb_idx_latch_next = state_in.pred_calc_btb_idx_latch;
    comb.pred_calc_type_idx_latch_next = state_in.pred_calc_type_idx_latch;
    comb.pred_calc_bht_idx_latch_next = state_in.pred_calc_bht_idx_latch;
    comb.upd_calc_next_bht_val_latch_next = state_in.upd_calc_next_bht_val_latch;
    comb.upd_calc_hit_info_latch_next = state_in.upd_calc_hit_info_latch;
    comb.upd_calc_victim_way_latch_next = state_in.upd_calc_victim_way_latch;
    comb.upd_calc_w_target_way_latch_next = state_in.upd_calc_w_target_way_latch;
    comb.upd_calc_next_useful_val_latch_next = state_in.upd_calc_next_useful_val_latch;
    comb.upd_calc_writes_btb_latch_next = state_in.upd_calc_writes_btb_latch;

    // 复制index结果
    // comb.btb_idx = idx_2.btb_idx;
    // comb.type_idx = idx_2.type_idx;
    // comb.bht_idx = idx_2.bht_idx;
    // comb.tc_idx = idx_2.tc_idx;
    // comb.tag = idx_2.tag;

    DEBUG_LOG_SMALL("[BTB_TOP] state=%d\n", state_in.state);

    // 1.1 Next State Logic
    switch (state_in.state) {
      case S_IDLE:
        if (inp.pred_req || inp.upd_valid) {
          if (!inp.upd_valid)
            comb.next_state = S_STAGE2; // no update req, go straight to stage 2
          else if (mem_2.read_data_valid)
            comb.next_state = S_STAGE2; // data is ready, go straight to stage 2
          else
            comb.next_state =
                S_IDLE_WAIT_DATA; // data is not ready, wait for data
        } else {
          comb.next_state = S_IDLE;
        }
        break;
      case S_STAGE2:
        if (!state_in.do_pred_latch)
          comb.next_state = S_IDLE; // no pred req, go straight to idle
        else if (mem_2.read_data_valid)
          comb.next_state = S_IDLE; // data is ready, go straight to idle
        else
          comb.next_state =
              S_STAGE2_WAIT_DATA; // data is not ready, wait for data
        break;
      case S_IDLE_WAIT_DATA:
        if (mem_2.read_data_valid)
          comb.next_state = S_STAGE2;
        else
          comb.next_state = S_IDLE_WAIT_DATA;
        break;
      case S_STAGE2_WAIT_DATA:
        if (mem_2.read_data_valid)
          comb.next_state = S_IDLE;
        else
          comb.next_state = S_STAGE2_WAIT_DATA;
        break;
      default:
        comb.next_state = state_in.state;
        break;
    }

    // 1.2 Stage 1 Calculation (预测路径的 index 计算)
    if (state_in.state == S_IDLE && inp.pred_req) {
      comb.s1_pred_tag = btb_get_tag_value(inp.pred_pc);
      comb.s1_pred_btb_idx = btb_get_idx_value(inp.pred_pc);
      comb.s1_pred_type_idx = btb_get_type_idx_value(inp.pred_pc);
      comb.s1_pred_bht_idx = bht_get_idx_value(inp.pred_pc);
      // comb.s1_pred_tc_idx = tc_get_idx_comb(
          // inp.pred_pc, mem_2.r_bht); // this not depends on mem_valid
    }

    // 1.3 Stage 2 Calculation (预测输出逻辑)
    if ((state_in.state == S_STAGE2 || state_in.state == S_STAGE2_WAIT_DATA) && state_in.do_pred_latch && mem_2.read_data_valid) {
      BtbHitCheckCombOut hit_out{};
      btb_hit_check_comb(
          BtbHitCheckCombIn{mem_2.r_btb_set, state_in.pred_calc_btb_tag_latch},
          hit_out);
      comb.hit_info = hit_out.hit_info;
      BtbPredOutputCombOut pred_out{};
      btb_pred_output_comb(BtbPredOutputCombIn{state_in.pred_calc_pc_latch, mem_2.r_type,
                                               comb.hit_info, mem_2.r_btb_set, mem_2.r_tc_set},
                           pred_out);
      comb.pred_target = pred_out.pred_target;
    }

    // 1.4 Update Path Calculation
    if (comb.next_state == S_STAGE2) {
      bool upd_actual_dir;
      uint32_t upd_actual_addr;
      uint8_t upd_br_type_in;
      upd_actual_dir = state_in.state == S_IDLE ? inp.upd_actual_dir : state_in.upd_actual_dir_latch;
      upd_actual_addr = state_in.state == S_IDLE ? inp.upd_actual_addr : state_in.upd_actual_addr_latch;
      upd_br_type_in = state_in.state == S_IDLE ? inp.upd_br_type_in : state_in.upd_br_type_latch;

      comb.next_bht_val =
          bht_next_state_value(mem_2.r_bht, upd_br_type_in, upd_actual_dir, upd_actual_addr);

      BtbHitCheckCombOut upd_hit_out{};
      btb_hit_check_comb(BtbHitCheckCombIn{mem_2.r_btb_set, idx_2.tag}, upd_hit_out);
      comb.upd_hit_info = upd_hit_out.hit_info;
      BtbVictimSelectCombOut victim_out{};
      btb_victim_select_comb(BtbVictimSelectCombIn{mem_2.r_btb_set}, victim_out);
      comb.victim_way = victim_out.victim_way;
      comb.w_target_way =
          comb.upd_hit_info.hit ? comb.upd_hit_info.hit_way : comb.victim_way;

      uint32_t current_target_bta = mem_2.r_btb_set.bta[comb.w_target_way];
      uint8_t current_useful = mem_2.r_btb_set.useful[comb.w_target_way];
      bool correct_pred = (current_target_bta == upd_actual_addr); 

      uint8_t calc_useful_val = useful_next_state_value(current_useful, correct_pred);
      comb.next_useful_val = comb.upd_hit_info.hit ? calc_useful_val : 1;

      comb.upd_writes_btb =
          (upd_br_type_in == BR_DIRECT || upd_br_type_in == BR_CALL ||
           upd_br_type_in == BR_RET || upd_br_type_in == BR_JAL
#if ENABLE_INDIRECT_BTB_TRAIN
           || upd_br_type_in == BR_IDIRECT
#endif
          );
    }

    // 1.5 Next Latch and Commit Request Calculation
    const bool enter_pipeline = (state_in.state == S_IDLE && comb.next_state != S_IDLE);
    if (enter_pipeline) {
      comb.do_pred_latch_next = inp.pred_req;
      comb.do_upd_latch_next = inp.upd_valid;
      comb.upd_pc_latch_next = inp.upd_pc;
      comb.upd_actual_addr_latch_next = inp.upd_actual_addr;
      comb.upd_br_type_latch_next = inp.upd_br_type_in;
      comb.upd_actual_dir_latch_next = inp.upd_actual_dir;
      comb.pred_calc_pc_latch_next = inp.pred_pc;
      comb.pred_calc_btb_tag_latch_next = comb.s1_pred_tag;
      comb.pred_calc_btb_idx_latch_next = comb.s1_pred_btb_idx;
      comb.pred_calc_type_idx_latch_next = comb.s1_pred_type_idx;
      comb.pred_calc_bht_idx_latch_next = comb.s1_pred_bht_idx;
    }

    if (comb.next_state == S_STAGE2) {
      comb.upd_calc_next_bht_val_latch_next = comb.next_bht_val;
      comb.upd_calc_hit_info_latch_next = comb.upd_hit_info;
      comb.upd_calc_victim_way_latch_next = comb.victim_way;
      comb.upd_calc_w_target_way_latch_next = comb.w_target_way;
      comb.upd_calc_next_useful_val_latch_next = comb.next_useful_val;
      comb.upd_calc_writes_btb_latch_next = comb.upd_writes_btb;
    }

    const bool do_commit_update =
        (state_in.state != S_IDLE && comb.next_state == S_IDLE &&
         state_in.do_upd_latch);
    if (do_commit_update) {
      comb.type_we_commit = true;
      comb.type_wr_idx = btb_get_type_idx_value(state_in.upd_pc_latch);
      comb.type_wdata_commit = state_in.upd_br_type_latch;

      if (state_in.upd_br_type_latch != BR_NONCTL) {
        comb.bht_we_commit = true;
        comb.bht_wr_idx = bht_get_idx_value(state_in.upd_pc_latch);
        comb.bht_wdata_commit = state_in.upd_calc_next_bht_val_latch;
      }

      if (state_in.upd_actual_dir_latch) {
        if (state_in.upd_br_type_latch == BR_IDIRECT) {
          const uint32_t tc_write_idx =
              tc_get_idx_value(state_in.upd_pc_latch, state_in.upd_calc_next_bht_val_latch);
          const uint32_t tc_write_tag = tc_get_tag_value(state_in.upd_pc_latch);
          TcHitCheckCombOut tc_hit_out{};
          tc_hit_check_comb(TcHitCheckCombIn{mem_2.r_tc_set, tc_write_tag}, tc_hit_out);
          TcVictimSelectCombOut tc_victim_out{};
          tc_victim_select_comb(TcVictimSelectCombIn{mem_2.r_tc_set}, tc_victim_out);
          int tc_write_way =
              tc_hit_out.hit_info.hit ? tc_hit_out.hit_info.hit_way : tc_victim_out.victim_way;
          uint8_t current_tc_useful = mem_2.r_tc_set.useful[tc_write_way];
          uint32_t current_tc_target = mem_2.r_tc_set.target[tc_write_way];
          bool tc_correct =
              tc_hit_out.hit_info.hit && (current_tc_target == state_in.upd_actual_addr_latch);
          uint8_t next_tc_useful =
              tc_hit_out.hit_info.hit ? useful_next_state_value(current_tc_useful, tc_correct)
                                      : static_cast<uint8_t>(INDIRECT_TC_INIT_USEFUL);
          comb.tc_we_commit = true;
          comb.tc_wr_way = tc_write_way;
          comb.tc_wr_idx = tc_write_idx;
          comb.tc_wdata_commit = state_in.upd_actual_addr_latch;
          comb.tc_wtag_commit = tc_write_tag;
          comb.tc_wvalid_commit = true;
          comb.tc_wuseful_commit = next_tc_useful;
        }
        if (state_in.upd_calc_writes_btb_latch) {
          comb.btb_we_commit = true;
          comb.btb_wr_way = state_in.upd_calc_w_target_way_latch;
          comb.btb_wr_idx = btb_get_idx_value(state_in.upd_pc_latch);
          comb.btb_wr_tag = btb_get_tag_value(state_in.upd_pc_latch);
          comb.btb_wr_bta = state_in.upd_actual_addr_latch;
          comb.btb_wr_valid = true;
          if (state_in.upd_br_type_latch == BR_IDIRECT) {
            comb.btb_wr_useful = static_cast<uint8_t>(INDIRECT_BTB_INIT_USEFUL);
          } else {
            comb.btb_wr_useful = state_in.upd_calc_next_useful_val_latch;
          }
        }
      }
    }

    // 1.6 Output Logic
    // comb.out_regs.busy = (state_in.state != S_IDLE);

    if (state_in.state != S_IDLE && comb.next_state == S_IDLE) { // moving to idle
      if (state_in.do_upd_latch) {
        comb.out_regs.btb_update_done = true;
      }
      if (state_in.do_pred_latch) {
        comb.out_regs.pred_target = comb.pred_target;
        comb.out_regs.btb_pred_out_valid = true;
      }
    }

  }

  // ------------------------------------------------------------------------
  // 三阶段接口
  // ------------------------------------------------------------------------
  void btb_seq_read(const InputPayload &inp, ReadData &rd) const {
    std::memset(&rd, 0, sizeof(ReadData));

    rd.state_in.state = state;
    rd.state_in.do_pred_latch = do_pred_latch;
    rd.state_in.do_upd_latch = do_upd_latch;
    rd.state_in.upd_pc_latch = upd_pc_latch;
    rd.state_in.upd_actual_addr_latch = upd_actual_addr_latch;
    rd.state_in.upd_br_type_latch = upd_br_type_latch;
    rd.state_in.upd_actual_dir_latch = upd_actual_dir_latch;
    rd.state_in.pred_calc_pc_latch = pred_calc_pc_latch;
    rd.state_in.pred_calc_btb_tag_latch = pred_calc_btb_tag_latch;
    rd.state_in.pred_calc_btb_idx_latch = pred_calc_btb_idx_latch;
    rd.state_in.pred_calc_type_idx_latch = pred_calc_type_idx_latch;
    rd.state_in.pred_calc_bht_idx_latch = pred_calc_bht_idx_latch;
    rd.state_in.upd_calc_next_bht_val_latch = upd_calc_next_bht_val_latch;
    rd.state_in.upd_calc_hit_info_latch = upd_calc_hit_info_latch;
    rd.state_in.upd_calc_victim_way_latch = upd_calc_victim_way_latch;
    rd.state_in.upd_calc_w_target_way_latch = upd_calc_w_target_way_latch;
    rd.state_in.upd_calc_next_useful_val_latch = upd_calc_next_useful_val_latch;
    rd.state_in.upd_calc_writes_btb_latch = upd_calc_writes_btb_latch;

    rd.sram_delay_active = sram_delay_active;
    rd.sram_delay_counter = sram_delay_counter;
    rd.sram_delayed_data = sram_delayed_data;
    rd.sram_prng_state = sram_prng_state;
    btb_prepare_comb_read(inp, rd);
  }

  void btb_prepare_comb_read(const InputPayload &inp, ReadData &rd) const {
    rd.new_read_valid = false;
    rd.pred_read_valid = false;
    rd.pred_btb_idx = 0;
    rd.pred_type_idx = 0;
    rd.pred_bht_idx = 0;
    rd.pred_tc_idx = 0;
    rd.pred_tag = 0;
    rd.pred_type_data = 0;
    rd.pred_bht_data = 0;
    std::memset(&rd.pred_btb_set, 0, sizeof(rd.pred_btb_set));
    std::memset(&rd.pred_tc_set, 0, sizeof(rd.pred_tc_set));
    rd.upd_read_valid = false;
    rd.upd_btb_idx = 0;
    rd.upd_type_idx = 0;
    rd.upd_bht_idx = 0;
    rd.upd_tag = 0;
    rd.upd_bht_data = 0;
    rd.upd_next_bht_data = 0;
    std::memset(&rd.upd_btb_set, 0, sizeof(rd.upd_btb_set));
    rd.upd_tc_read_valid = false;
    rd.upd_tc_write_idx = 0;
    rd.upd_tc_write_tag = 0;
    std::memset(&rd.upd_tc_set, 0, sizeof(rd.upd_tc_set));

    if (inp.pred_req) {
      rd.pred_read_valid = true;
      rd.pred_btb_idx = btb_get_idx_value(inp.pred_pc);
      rd.pred_type_idx = btb_get_type_idx_value(inp.pred_pc);
      rd.pred_bht_idx = bht_get_idx_value(inp.pred_pc);
      rd.pred_tag = btb_get_tag_value(inp.pred_pc);
      rd.pred_type_data = mem_type[rd.pred_type_idx];
      rd.pred_bht_data = mem_bht[rd.pred_bht_idx];
      rd.pred_tc_idx = tc_get_idx_value(inp.pred_pc, rd.pred_bht_data);
      for (int w = 0; w < BTB_WAY_NUM; ++w) {
        rd.pred_btb_set.tag[w] = mem_btb_tag[w][rd.pred_btb_idx];
        rd.pred_btb_set.bta[w] = mem_btb_bta[w][rd.pred_btb_idx];
        rd.pred_btb_set.valid[w] = mem_btb_valid[w][rd.pred_btb_idx];
        rd.pred_btb_set.useful[w] = mem_btb_useful[w][rd.pred_btb_idx];
      }
      for (int w = 0; w < TC_WAY_NUM; ++w) {
        rd.pred_tc_set.target[w] = mem_tc_target[w][rd.pred_tc_idx];
        rd.pred_tc_set.tag[w] = mem_tc_tag[w][rd.pred_tc_idx];
        rd.pred_tc_set.valid[w] = mem_tc_valid[w][rd.pred_tc_idx];
        rd.pred_tc_set.useful[w] = mem_tc_useful[w][rd.pred_tc_idx];
      }
    }

    if (inp.upd_valid) {
      rd.upd_read_valid = true;
      rd.upd_btb_idx = btb_get_idx_value(inp.upd_pc);
      rd.upd_type_idx = btb_get_type_idx_value(inp.upd_pc);
      rd.upd_bht_idx = bht_get_idx_value(inp.upd_pc);
      rd.upd_tag = btb_get_tag_value(inp.upd_pc);
      rd.upd_bht_data = mem_bht[rd.upd_bht_idx];
      rd.upd_next_bht_data =
          (inp.upd_br_type_in != BR_NONCTL)
              ? bht_next_state_value(rd.upd_bht_data, inp.upd_br_type_in, inp.upd_actual_dir,
                                     inp.upd_actual_addr)
              : rd.upd_bht_data;
      for (int w = 0; w < BTB_WAY_NUM; ++w) {
        rd.upd_btb_set.tag[w] = mem_btb_tag[w][rd.upd_btb_idx];
        rd.upd_btb_set.bta[w] = mem_btb_bta[w][rd.upd_btb_idx];
        rd.upd_btb_set.valid[w] = mem_btb_valid[w][rd.upd_btb_idx];
        rd.upd_btb_set.useful[w] = mem_btb_useful[w][rd.upd_btb_idx];
      }

      if (inp.upd_actual_dir && inp.upd_br_type_in == BR_IDIRECT) {
        rd.upd_tc_read_valid = true;
        rd.upd_tc_write_idx = tc_get_idx_value(inp.upd_pc, rd.upd_next_bht_data);
        rd.upd_tc_write_tag = tc_get_tag_value(inp.upd_pc);
        for (int w = 0; w < TC_WAY_NUM; ++w) {
          rd.upd_tc_set.target[w] = mem_tc_target[w][rd.upd_tc_write_idx];
          rd.upd_tc_set.tag[w] = mem_tc_tag[w][rd.upd_tc_write_idx];
          rd.upd_tc_set.valid[w] = mem_tc_valid[w][rd.upd_tc_write_idx];
          rd.upd_tc_set.useful[w] = mem_tc_useful[w][rd.upd_tc_write_idx];
        }
      }
    }

    BtbGenIndexPreCombOut idx_pre_out{};
    btb_gen_index_pre_comb(BtbGenIndexPreCombIn{inp, rd.state_in}, idx_pre_out);
    rd.idx_1 = idx_pre_out.idx;

    BtbMemReadPreCombOut mem_pre_out{};
    uint8_t pre_type_data = 0;
    uint32_t pre_bht_data = 0;
    if (rd.idx_1.read_address_valid) {
      pre_type_data = mem_type[rd.idx_1.type_idx];
      pre_bht_data = mem_bht[rd.idx_1.bht_idx];
    }
    btb_mem_read_pre_comb(BtbMemReadPreCombIn{rd.idx_1, rd.state_in, pre_type_data,
                                               pre_bht_data},
                          mem_pre_out);
    rd.mem_1 = mem_pre_out.mem;

    BtbGenIndexPostCombOut idx_post_out{};
    btb_gen_index_post_comb(
        BtbGenIndexPostCombIn{inp, rd.state_in, rd.idx_1, rd.mem_1}, idx_post_out);
    rd.idx_2 = idx_post_out.idx_2;

    rd.mem_2.r_type = rd.mem_1.r_type;
    rd.mem_2.r_bht = rd.mem_1.r_bht;

    if (rd.sram_delay_active) {
      rd.mem_2.r_btb_set = rd.sram_delayed_data.r_btb_set;
      rd.mem_2.r_tc_set = rd.sram_delayed_data.r_tc_set;
      rd.mem_2.read_data_valid = (rd.sram_delay_counter == 0);
      rd.new_read_valid = false;
      return;
    }

    if (!rd.idx_2.read_address_valid) {
      rd.mem_2.read_data_valid = false;
      rd.new_read_valid = false;
      return;
    }

    for (int w = 0; w < BTB_WAY_NUM; w++) {
      rd.mem_2.r_btb_set.tag[w] = mem_btb_tag[w][rd.idx_2.btb_idx];
      rd.mem_2.r_btb_set.bta[w] = mem_btb_bta[w][rd.idx_2.btb_idx];
      rd.mem_2.r_btb_set.valid[w] = mem_btb_valid[w][rd.idx_2.btb_idx];
      rd.mem_2.r_btb_set.useful[w] = mem_btb_useful[w][rd.idx_2.btb_idx];
    }
    for (int w = 0; w < TC_WAY_NUM; w++) {
      rd.mem_2.r_tc_set.target[w] = mem_tc_target[w][rd.idx_2.tc_idx];
      rd.mem_2.r_tc_set.tag[w] = mem_tc_tag[w][rd.idx_2.tc_idx];
      rd.mem_2.r_tc_set.valid[w] = mem_tc_valid[w][rd.idx_2.tc_idx];
      rd.mem_2.r_tc_set.useful[w] = mem_tc_useful[w][rd.idx_2.tc_idx];
    }
    rd.new_read_valid = true;
    rd.new_read_data = rd.mem_2;

#ifdef SRAM_DELAY_ENABLE
    rd.mem_2.read_data_valid = false;
#else
    rd.mem_2.read_data_valid = true;
#endif
  }

  void btb_comb(const BtbCombIn &in, BtbCombOut &out_bundle) const {
    const InputPayload &inp = in.inp;
    const ReadData &rd = in.rd;
    OutputPayload &out = out_bundle.out_regs;
    CombResult &req = out_bundle.req;
    std::memset(&out, 0, sizeof(OutputPayload));
    std::memset(&req, 0, sizeof(CombResult));

    req.next_state = S_IDLE;
    req.sram_delay_active_next = false;
    req.sram_delay_counter_next = 0;
    req.sram_delayed_data_next = rd.sram_delayed_data;
    req.sram_prng_state_next = rd.sram_prng_state;

    if (inp.pred_req && rd.pred_read_valid) {
      BtbHitCheckCombOut pred_hit_out{};
      btb_hit_check_comb(BtbHitCheckCombIn{rd.pred_btb_set, rd.pred_tag}, pred_hit_out);
      BtbPredOutputCombOut pred_out{};
      btb_pred_output_comb(
          BtbPredOutputCombIn{inp.pred_pc, rd.pred_type_data, pred_hit_out.hit_info,
                              rd.pred_btb_set, rd.pred_tc_set},
          pred_out);
      out.pred_target = pred_out.pred_target;
      out.btb_pred_out_valid = true;
    }

    if (inp.upd_valid && rd.upd_read_valid) {
      req.type_we_commit = true;
      req.type_wr_idx = rd.upd_type_idx;
      req.type_wdata_commit = inp.upd_br_type_in;

      if (inp.upd_br_type_in != BR_NONCTL) {
        req.bht_we_commit = true;
        req.bht_wr_idx = rd.upd_bht_idx;
        req.bht_wdata_commit = rd.upd_next_bht_data;
      }

      BtbHitCheckCombOut upd_hit_out{};
      btb_hit_check_comb(BtbHitCheckCombIn{rd.upd_btb_set, rd.upd_tag}, upd_hit_out);
      BtbVictimSelectCombOut victim_out{};
      btb_victim_select_comb(BtbVictimSelectCombIn{rd.upd_btb_set}, victim_out);
      const int write_way =
          upd_hit_out.hit_info.hit ? upd_hit_out.hit_info.hit_way : victim_out.victim_way;

      const bool writes_btb =
          (inp.upd_br_type_in == BR_DIRECT || inp.upd_br_type_in == BR_CALL ||
           inp.upd_br_type_in == BR_RET || inp.upd_br_type_in == BR_JAL
#if ENABLE_INDIRECT_BTB_TRAIN
           || inp.upd_br_type_in == BR_IDIRECT
#endif
          );

      if (inp.upd_actual_dir && writes_btb) {
        const uint8_t current_useful = rd.upd_btb_set.useful[write_way];
        const uint32_t current_target_bta = rd.upd_btb_set.bta[write_way];
        const bool correct_pred = upd_hit_out.hit_info.hit &&
                                  (current_target_bta == inp.upd_actual_addr);
        const uint8_t next_useful =
            upd_hit_out.hit_info.hit ? useful_next_state_value(current_useful, correct_pred) : 1;
        req.btb_we_commit = true;
        req.btb_wr_way = write_way;
        req.btb_wr_idx = rd.upd_btb_idx;
        req.btb_wr_tag = rd.upd_tag;
        req.btb_wr_bta = inp.upd_actual_addr;
        req.btb_wr_valid = true;
        if (inp.upd_br_type_in == BR_IDIRECT) {
          req.btb_wr_useful = static_cast<uint8_t>(INDIRECT_BTB_INIT_USEFUL);
        } else {
          req.btb_wr_useful = next_useful;
        }
      }

      if (rd.upd_tc_read_valid) {
        TcHitCheckCombOut tc_hit_out{};
        tc_hit_check_comb(TcHitCheckCombIn{rd.upd_tc_set, rd.upd_tc_write_tag}, tc_hit_out);
        TcVictimSelectCombOut tc_victim_out{};
        tc_victim_select_comb(TcVictimSelectCombIn{rd.upd_tc_set}, tc_victim_out);
        const int tc_write_way =
            tc_hit_out.hit_info.hit ? tc_hit_out.hit_info.hit_way : tc_victim_out.victim_way;
        const uint8_t current_tc_useful = rd.upd_tc_set.useful[tc_write_way];
        const uint32_t current_tc_target = rd.upd_tc_set.target[tc_write_way];
        const bool tc_correct =
            tc_hit_out.hit_info.hit && (current_tc_target == inp.upd_actual_addr);
        const uint8_t next_tc_useful =
            tc_hit_out.hit_info.hit
                ? useful_next_state_value(current_tc_useful, tc_correct)
                : static_cast<uint8_t>(INDIRECT_TC_INIT_USEFUL);
        req.tc_we_commit = true;
        req.tc_wr_way = tc_write_way;
        req.tc_wr_idx = rd.upd_tc_write_idx;
        req.tc_wdata_commit = inp.upd_actual_addr;
        req.tc_wtag_commit = rd.upd_tc_write_tag;
        req.tc_wvalid_commit = true;
        req.tc_wuseful_commit = next_tc_useful;
      }
      out.btb_update_done = true;
    }

    out.busy = false;
    req.out_regs = out;
  }

  void btb_seq_write(const InputPayload &inp, const CombResult &req, bool reset) {
    if (reset) {
      this->reset();
      return;
    }
    (void)inp;
    do_pred_latch = req.do_pred_latch_next;
    do_upd_latch = req.do_upd_latch_next;
    upd_pc_latch = req.upd_pc_latch_next;
    upd_actual_addr_latch = req.upd_actual_addr_latch_next;
    upd_br_type_latch = req.upd_br_type_latch_next;
    upd_actual_dir_latch = req.upd_actual_dir_latch_next;
    pred_calc_pc_latch = req.pred_calc_pc_latch_next;
    pred_calc_btb_tag_latch = req.pred_calc_btb_tag_latch_next;
    pred_calc_btb_idx_latch = req.pred_calc_btb_idx_latch_next;
    pred_calc_type_idx_latch = req.pred_calc_type_idx_latch_next;
    pred_calc_bht_idx_latch = req.pred_calc_bht_idx_latch_next;
    upd_calc_next_bht_val_latch = req.upd_calc_next_bht_val_latch_next;
    upd_calc_hit_info_latch = req.upd_calc_hit_info_latch_next;
    upd_calc_victim_way_latch = req.upd_calc_victim_way_latch_next;
    upd_calc_w_target_way_latch = req.upd_calc_w_target_way_latch_next;
    upd_calc_next_useful_val_latch = req.upd_calc_next_useful_val_latch_next;
    upd_calc_writes_btb_latch = req.upd_calc_writes_btb_latch_next;

    if (req.type_we_commit) {
      mem_type[req.type_wr_idx] = req.type_wdata_commit;
    }
    if (req.bht_we_commit) {
      mem_bht[req.bht_wr_idx] = req.bht_wdata_commit;
    }
    if (req.tc_we_commit) {
      mem_tc_target[req.tc_wr_way][req.tc_wr_idx] = req.tc_wdata_commit;
      mem_tc_tag[req.tc_wr_way][req.tc_wr_idx] = req.tc_wtag_commit;
      mem_tc_valid[req.tc_wr_way][req.tc_wr_idx] = req.tc_wvalid_commit;
      mem_tc_useful[req.tc_wr_way][req.tc_wr_idx] = req.tc_wuseful_commit;
    }
    if (req.btb_we_commit) {
      mem_btb_tag[req.btb_wr_way][req.btb_wr_idx] = req.btb_wr_tag;
      mem_btb_bta[req.btb_wr_way][req.btb_wr_idx] = req.btb_wr_bta;
      mem_btb_valid[req.btb_wr_way][req.btb_wr_idx] = req.btb_wr_valid;
      mem_btb_useful[req.btb_wr_way][req.btb_wr_idx] = req.btb_wr_useful;
    }

    sram_delay_active = req.sram_delay_active_next;
    sram_delay_counter = req.sram_delay_counter_next;
    sram_delayed_data = req.sram_delayed_data_next;
    sram_prng_state = req.sram_prng_state_next;
    sram_new_req_this_cycle = false;

    state = req.next_state;
  }

  // ------------------------------------------------------------------------
  // 兼容接口
  // ------------------------------------------------------------------------
  CombResult step_pipeline(const InputPayload &inp) {
    ReadData rd;
    BtbCombOut comb_out{};
    btb_seq_read(inp, rd);
    btb_comb(BtbCombIn{inp, rd}, comb_out);
    return comb_out.req;
  }

  void step_seq(bool rst_n, const InputPayload &inp, const CombResult &comb) {
    btb_seq_write(inp, comb, rst_n);
  }

  OutputPayload step(bool rst_n, const InputPayload &inp) {
    if (rst_n) {
      reset();
      DEBUG_LOG("[BTB_TOP] reset\n");
      OutputPayload out_reg_reset;
      std::memset(&out_reg_reset, 0, sizeof(OutputPayload));
      return out_reg_reset;
    }
    ReadData rd;
    BtbCombOut comb_out{};
    btb_seq_read(inp, rd);
    btb_comb(BtbCombIn{inp, rd}, comb_out);
    btb_seq_write(inp, comb_out.req, false);
    return comb_out.out_regs;
  }

private:
  // ============================================================
  // 组合逻辑函数实现 (Internal Implementation)
  // ============================================================

  static void btb_xorshift32_comb(const BtbXorshift32CombIn &in,
                                  BtbXorshift32CombOut &out) {
    out = BtbXorshift32CombOut{};
    uint32_t value = (in.state == 0) ? 0x2468ace1u : in.state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    out.next_state = value;
  }

  static constexpr int calc_index_width(uint32_t mask) {
    int width = 0;
    while (mask > 0) {
      ++width;
      mask >>= 1;
    }
    return width;
  }

  static uint32_t hash_index_value(uint32_t pc_word, uint32_t mask, int width,
                                   uint32_t salt) {
    if (width <= 0) {
      return pc_word & mask;
    }
    uint32_t mixed = pc_word;
    if (width < 32) {
      mixed ^= (pc_word >> width);
    }
    if ((2 * width) < 32) {
      mixed ^= (pc_word >> (2 * width));
    }
    if ((3 * width) < 32) {
      mixed ^= (pc_word >> (3 * width));
    }
    mixed ^= salt;
    return mixed & mask;
  }

  // [Comb] Index Calculation
  static void btb_get_tag_comb(const BtbGetTagCombIn &in, BtbGetTagCombOut &out) {
    out = BtbGetTagCombOut{};
    out.tag = ((in.pc >> 2) >> BTB_IDX_LEN) & BTB_TAG_MASK;
  }

  static void btb_get_idx_comb(const BtbGetIdxCombIn &in, BtbGetIdxCombOut &out) {
    out = BtbGetIdxCombOut{};
#if ENABLE_BTB_ALIAS_HASH
    constexpr int kBtbIdxWidth = calc_index_width(BTB_IDX_MASK);
    const uint32_t pc_word = in.pc >> 2;
    out.idx = hash_index_value(pc_word, BTB_IDX_MASK, kBtbIdxWidth, 0x9e3779b9u);
#else
    out.idx = (in.pc >> 2) & BTB_IDX_MASK;
#endif
  }

  static void btb_get_type_idx_comb(const BtbGetTypeIdxCombIn &in,
                                    BtbGetTypeIdxCombOut &out) {
    out = BtbGetTypeIdxCombOut{};
#if ENABLE_BTB_ALIAS_HASH
    constexpr int kTypeIdxWidth = calc_index_width(BTB_TYPE_IDX_MASK);
    const uint32_t pc_word = in.pc >> 2;
    out.idx =
        hash_index_value(pc_word, BTB_TYPE_IDX_MASK, kTypeIdxWidth, 0x7f4a7c15u);
#else
    out.idx = (in.pc >> 2) & BTB_TYPE_IDX_MASK;
#endif
  }

  static void bht_get_idx_comb(const BhtGetIdxCombIn &in, BhtGetIdxCombOut &out) {
    out = BhtGetIdxCombOut{};
#if ENABLE_BTB_ALIAS_HASH
    constexpr int kBhtIdxWidth = calc_index_width(BHT_IDX_MASK);
    const uint32_t pc_word = in.pc >> 2;
    out.idx = hash_index_value(pc_word, BHT_IDX_MASK, kBhtIdxWidth, 0x6a5d39e1u);
#else
    out.idx = (in.pc >> 2) & BHT_IDX_MASK;
#endif
  }

  static void tc_get_idx_comb(const TcGetIdxCombIn &in, TcGetIdxCombOut &out) {
    out = TcGetIdxCombOut{};
#if ENABLE_BTB_ALIAS_HASH
    constexpr int kTcIdxWidth = calc_index_width(TC_ENTRY_MASK);
    const uint32_t pc_word = in.pc >> 2;
    const uint32_t hist_mix =
        in.bht_value ^ (in.bht_value >> 7) ^ (in.bht_value >> 13) ^ (in.bht_value << 9);
    out.idx = hash_index_value(pc_word ^ hist_mix ^ (in.bht_value >> 19), TC_ENTRY_MASK,
                               kTcIdxWidth,
                               0x51ed270bu);
#else
    out.idx = (in.bht_value ^ in.pc) & TC_ENTRY_MASK;
#endif
  }

  static void tc_get_tag_comb(const TcGetTagCombIn &in, TcGetTagCombOut &out) {
    out = TcGetTagCombOut{};
#if ENABLE_BTB_ALIAS_HASH
    constexpr int kTcTagWidth = calc_index_width(TC_TAG_MASK);
    const uint32_t pc_word = in.pc >> 2;
    out.tag =
        hash_index_value(pc_word, TC_TAG_MASK, kTcTagWidth, 0x3c6ef372u);
#else
    out.tag = (in.pc >> 2) & TC_TAG_MASK;
#endif
  }

  // [Comb] Logic
  static void bht_next_state_comb(const BhtNextStateCombIn &in,
                                  BhtNextStateCombOut &out) {
    out = BhtNextStateCombOut{};
#if ENABLE_TC_TARGET_SIGNATURE
    if (in.br_type == BR_IDIRECT) {
      uint32_t target_word = in.actual_target >> 2;
      uint32_t target_folded = target_word ^ (target_word >> 11) ^ (target_word >> 19);
      uint32_t hist_folded =
          in.current_bht ^ (in.current_bht << 5) ^ (in.current_bht >> 2);
      out.next_bht = hist_folded ^ target_folded ^ 0x9e3779b9u;
      return;
    }
#endif
    out.next_bht = (in.current_bht << 1) | (in.pc_dir ? 1 : 0);
  }

  static void useful_next_state_comb(const UsefulNextStateCombIn &in,
                                     UsefulNextStateCombOut &out) {
    out = UsefulNextStateCombOut{};
    out.next_val = in.current_val;
    if (in.correct) {
      if (out.next_val < 7) {
        out.next_val = static_cast<uint8_t>(out.next_val + 1);
      }
    } else {
      if (out.next_val > 0) {
        out.next_val = static_cast<uint8_t>(out.next_val - 1);
      }
    }
  }

  static void btb_hit_check_comb(const BtbHitCheckCombIn &in,
                                 BtbHitCheckCombOut &out) {
    std::memset(&out, 0, sizeof(BtbHitCheckCombOut));
    out.hit_info.hit = false;
    out.hit_info.hit_way = 0;
    for (int way = 0; way < BTB_WAY_NUM; way++) {
      if (in.set_data.valid[way] && in.set_data.tag[way] == in.tag) {
        out.hit_info.hit_way = way;
        out.hit_info.hit = true;
        return;
      }
    }
  }

  static void tc_hit_check_comb(const TcHitCheckCombIn &in, TcHitCheckCombOut &out) {
    std::memset(&out, 0, sizeof(TcHitCheckCombOut));
    out.hit_info.hit = false;
    out.hit_info.hit_way = 0;
    for (int way = 0; way < TC_WAY_NUM; way++) {
      if (in.set_data.valid[way] && in.set_data.tag[way] == in.tag) {
        out.hit_info.hit_way = way;
        out.hit_info.hit = true;
        return;
      }
    }
  }

  static void btb_pred_output_comb(const BtbPredOutputCombIn &in,
                                   BtbPredOutputCombOut &out) {
    out = BtbPredOutputCombOut{};
    uint8_t type = in.br_type;
    if (type == BR_IDIRECT) {
      uint32_t expected_tc_tag = tc_get_tag_value(in.pc);
      bool tc_hit = false;
      uint32_t tc_target = in.pc + 4;
      for (int way = 0; way < TC_WAY_NUM; way++) {
        if (in.tc_set.valid[way] && in.tc_set.tag[way] == expected_tc_tag) {
          tc_hit = true;
          tc_target = in.tc_set.target[way];
          break;
        }
      }
      if (tc_hit) {
        out.pred_target = tc_target;
#if ENABLE_INDIRECT_BTB_FALLBACK
      } else if (in.hit_info.hit) {
        out.pred_target = in.set_data.bta[in.hit_info.hit_way];
#endif
      } else {
        out.pred_target = in.pc + 4;
      }
      return;
    } else if (type == BR_DIRECT || type == BR_CALL || type == BR_JAL ||
               type == BR_RET) {
      if (in.hit_info.hit) {
        out.pred_target = in.set_data.bta[in.hit_info.hit_way];
        return;
      }
      out.pred_target = in.pc + 4;
      return;
    }
    out.pred_target = in.pc + 4;
  }

  static void btb_victim_select_comb(const BtbVictimSelectCombIn &in,
                                     BtbVictimSelectCombOut &out) {
    out = BtbVictimSelectCombOut{};
    // 1. Empty Way
    for (int way = 0; way < BTB_WAY_NUM; way++) {
      if (!in.set_data.valid[way]) {
        out.victim_way = way;
        return;
      }
    }
    // 2. Min Useful
    int min_useful = 255;
    int min_useful_way = 0;
    for (int way = 0; way < BTB_WAY_NUM; way++) {
      if (in.set_data.useful[way] < min_useful) {
        min_useful = in.set_data.useful[way];
        min_useful_way = way;
      }
    }
    out.victim_way = min_useful_way;
  }

  static void tc_victim_select_comb(const TcVictimSelectCombIn &in,
                                    TcVictimSelectCombOut &out) {
    out = TcVictimSelectCombOut{};
    for (int way = 0; way < TC_WAY_NUM; way++) {
      if (!in.set_data.valid[way]) {
        out.victim_way = way;
        return;
      }
    }
    int min_useful = 255;
    int min_useful_way = 0;
    for (int way = 0; way < TC_WAY_NUM; way++) {
      if (in.set_data.useful[way] < min_useful) {
        min_useful = in.set_data.useful[way];
        min_useful_way = way;
      }
    }
    out.victim_way = min_useful_way;
  }

  static uint32_t btb_get_tag_value(uint32_t pc) {
    BtbGetTagCombOut out{};
    btb_get_tag_comb(BtbGetTagCombIn{pc}, out);
    return out.tag;
  }

  static uint32_t btb_get_idx_value(uint32_t pc) {
    BtbGetIdxCombOut out{};
    btb_get_idx_comb(BtbGetIdxCombIn{pc}, out);
    return out.idx;
  }

  static uint32_t btb_get_type_idx_value(uint32_t pc) {
    BtbGetTypeIdxCombOut out{};
    btb_get_type_idx_comb(BtbGetTypeIdxCombIn{pc}, out);
    return out.idx;
  }

  static uint32_t bht_get_idx_value(uint32_t pc) {
    BhtGetIdxCombOut out{};
    bht_get_idx_comb(BhtGetIdxCombIn{pc}, out);
    return out.idx;
  }

  static uint32_t tc_get_idx_value(uint32_t pc, uint32_t bht_value) {
    TcGetIdxCombOut out{};
    tc_get_idx_comb(TcGetIdxCombIn{pc, bht_value}, out);
    return out.idx;
  }

  static uint32_t tc_get_tag_value(uint32_t pc) {
    TcGetTagCombOut out{};
    tc_get_tag_comb(TcGetTagCombIn{pc}, out);
    return out.tag;
  }

  static uint32_t bht_next_state_value(uint32_t current_bht, uint8_t br_type,
                                       bool pc_dir, uint32_t actual_target) {
    BhtNextStateCombOut out{};
    bht_next_state_comb(BhtNextStateCombIn{current_bht, br_type, pc_dir, actual_target},
                        out);
    return out.next_bht;
  }

  static uint8_t useful_next_state_value(uint8_t current_val, bool correct) {
    UsefulNextStateCombOut out{};
    useful_next_state_comb(UsefulNextStateCombIn{current_val, correct}, out);
    return out.next_val;
  }
};

#endif // BTB_TOP_H
