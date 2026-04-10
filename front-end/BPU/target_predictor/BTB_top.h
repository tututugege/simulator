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
  btb_tag_t tag[BTB_WAY_NUM];
  target_addr_t bta[BTB_WAY_NUM];
  wire1_t valid[BTB_WAY_NUM];
  wire3_t useful[BTB_WAY_NUM];
};

struct TcSetData {
  target_addr_t target[TC_WAY_NUM];
  tc_tag_t tag[TC_WAY_NUM];
  wire1_t valid[TC_WAY_NUM];
  wire3_t useful[TC_WAY_NUM];
};

struct HitCheckOut {
  btb_way_sel_t hit_way;
  wire1_t hit;
};

class BTB_TOP {
public:
  // ------------------------------------------------------------------------
  // 状态枚举定义（需要在结构体之前定义）
  // ------------------------------------------------------------------------
  static constexpr btb_state_t S_IDLE = 0;
  static constexpr btb_state_t S_STAGE2 = 1;
  static constexpr btb_state_t S_IDLE_WAIT_DATA = 2;
  static constexpr btb_state_t S_STAGE2_WAIT_DATA = 3;

  struct InputPayload {
    pc_t pred_pc;
    wire1_t pred_req;
    br_type_t pred_type_in;
    wire1_t upd_valid;
    pc_t upd_pc;
    target_addr_t upd_actual_addr;
    br_type_t upd_br_type_in;
    wire1_t upd_actual_dir;
  };

  struct OutputPayload {
    target_addr_t pred_target;
    wire1_t btb_pred_out_valid;
    wire1_t btb_update_done;
    wire1_t busy;
  };

  // 状态输入结构体（包含所有寄存器）
  struct StateInput {
    btb_state_t state;
    // input latches
    wire1_t do_pred_latch;
    wire1_t do_upd_latch;
    pc_t upd_pc_latch;
    target_addr_t upd_actual_addr_latch;
    br_type_t upd_br_type_latch;
    wire1_t upd_actual_dir_latch;
    // pipeline latches
    pc_t pred_calc_pc_latch;
    btb_tag_t pred_calc_btb_tag_latch;
    btb_idx_t pred_calc_btb_idx_latch;
    btb_type_idx_t pred_calc_type_idx_latch;
    bht_idx_t pred_calc_bht_idx_latch;
    // uint32_t pred_calc_tc_idx_latch;
    bht_hist_t upd_calc_next_bht_val_latch;
    HitCheckOut upd_calc_hit_info_latch;
    btb_way_sel_t upd_calc_victim_way_latch;
    tc_way_sel_t upd_calc_w_target_way_latch;
    wire3_t upd_calc_next_useful_val_latch;
    wire1_t upd_calc_writes_btb_latch;
  };

  // Index生成结果
  struct IndexResult {
    btb_idx_t btb_idx;
    btb_type_idx_t type_idx;
    bht_idx_t bht_idx;
    tc_idx_t tc_idx;
    btb_tag_t tag;
    wire1_t read_address_valid;
  };

  // 内存读取结果
  struct MemReadResult {
    BtbSetData r_btb_set;
    TcSetData r_tc_set;
    br_type_t r_type;
    bht_hist_t r_bht;
    wire1_t read_data_valid;
  };

  // 三阶段 Read 阶段输出
  struct ReadData {
    StateInput state_in;
    IndexResult idx_1;
    MemReadResult mem_1;
    IndexResult idx_2;
    MemReadResult mem_2;

    wire1_t sram_delay_active;
    wire32_t sram_delay_counter;
    MemReadResult sram_delayed_data;
    wire1_t new_read_valid;
    MemReadResult new_read_data;
    wire32_t sram_prng_state;

    wire1_t pred_read_valid;
    btb_idx_t pred_btb_idx;
    btb_type_idx_t pred_type_idx;
    bht_idx_t pred_bht_idx;
    tc_idx_t pred_tc_idx;
    btb_tag_t pred_tag;
    br_type_t pred_type_data;
    bht_hist_t pred_bht_data;
    BtbSetData pred_btb_set;
    TcSetData pred_tc_set;

    wire1_t upd_read_valid;
    btb_idx_t upd_btb_idx;
    btb_type_idx_t upd_type_idx;
    bht_idx_t upd_bht_idx;
    btb_tag_t upd_tag;
    bht_hist_t upd_bht_data;
    bht_hist_t upd_next_bht_data;
    BtbSetData upd_btb_set;

    wire1_t upd_tc_read_valid;
    tc_idx_t upd_tc_write_idx;
    tc_tag_t upd_tc_write_tag;
    TcSetData upd_tc_set;
  };

  // 组合逻辑计算结果结构体
  struct CombResult {
    btb_state_t next_state;
    btb_idx_t btb_idx;
    btb_type_idx_t type_idx;
    bht_idx_t bht_idx;
    tc_idx_t tc_idx;
    btb_tag_t tag;
    BtbSetData r_btb_set;
    br_type_t r_type;
    bht_hist_t r_bht;
    HitCheckOut hit_info;
    target_addr_t pred_target;

    // Update Path Calculation
    bht_hist_t next_bht_val;
    HitCheckOut upd_hit_info;
    btb_way_sel_t victim_way;
    tc_way_sel_t w_target_way;
    wire3_t next_useful_val;
    wire1_t upd_writes_btb;

    // Stage 1 calculation results (for pipeline)
    btb_tag_t s1_pred_tag;
    btb_idx_t s1_pred_btb_idx;
    btb_type_idx_t s1_pred_type_idx;
    bht_idx_t s1_pred_bht_idx;
    tc_idx_t s1_pred_tc_idx;

    OutputPayload out_regs;

    wire1_t sram_delay_active_next;
    wire32_t sram_delay_counter_next;
    MemReadResult sram_delayed_data_next;
    wire32_t sram_prng_state_next;

    wire1_t do_pred_latch_next;
    wire1_t do_upd_latch_next;
    pc_t upd_pc_latch_next;
    target_addr_t upd_actual_addr_latch_next;
    br_type_t upd_br_type_latch_next;
    wire1_t upd_actual_dir_latch_next;
    pc_t pred_calc_pc_latch_next;
    btb_tag_t pred_calc_btb_tag_latch_next;
    btb_idx_t pred_calc_btb_idx_latch_next;
    btb_type_idx_t pred_calc_type_idx_latch_next;
    bht_idx_t pred_calc_bht_idx_latch_next;
    bht_hist_t upd_calc_next_bht_val_latch_next;
    HitCheckOut upd_calc_hit_info_latch_next;
    btb_way_sel_t upd_calc_victim_way_latch_next;
    tc_way_sel_t upd_calc_w_target_way_latch_next;
    wire3_t upd_calc_next_useful_val_latch_next;
    wire1_t upd_calc_writes_btb_latch_next;
    wire1_t type_we_commit;
    btb_type_idx_t type_wr_idx;
    br_type_t type_wdata_commit;
    wire1_t bht_we_commit;
    bht_idx_t bht_wr_idx;
    bht_hist_t bht_wdata_commit;
    wire1_t tc_we_commit;
    tc_way_sel_t tc_wr_way;
    tc_idx_t tc_wr_idx;
    target_addr_t tc_wdata_commit;
    tc_tag_t tc_wtag_commit;
    wire1_t tc_wvalid_commit;
    wire3_t tc_wuseful_commit;
    wire1_t btb_we_commit;
    btb_way_sel_t btb_wr_way;
    btb_idx_t btb_wr_idx;
    btb_tag_t btb_wr_tag;
    target_addr_t btb_wr_bta;
    wire1_t btb_wr_valid;
    wire3_t btb_wr_useful;
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
    br_type_t pre_type_data;
    bht_hist_t pre_bht_data;
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

  struct BtbPredReadReqCombIn {
    InputPayload inp;
  };

  struct BtbPredReadReqCombOut {
    wire1_t pred_read_valid;
    pc_t pred_pc;
    btb_idx_t pred_btb_idx;
    btb_type_idx_t pred_type_idx;
    bht_idx_t pred_bht_idx;
    btb_tag_t pred_tag;
    tc_idx_t pred_tc_idx;
  };

  struct BtbUpdReadReqCombIn {
    InputPayload inp;
  };

  struct BtbUpdReadReqCombOut {
    wire1_t upd_read_valid;
    pc_t upd_pc;
    target_addr_t upd_actual_addr;
    br_type_t upd_br_type_in;
    wire1_t upd_actual_dir;
    btb_idx_t upd_btb_idx;
    btb_type_idx_t upd_type_idx;
    bht_idx_t upd_bht_idx;
    btb_tag_t upd_tag;
    bht_hist_t upd_next_bht_data;
    wire1_t upd_tc_read_valid;
    tc_idx_t upd_tc_write_idx;
    tc_tag_t upd_tc_write_tag;
  };

  struct BtbDataSeqReadIn {
    BtbPredReadReqCombOut pred_req;
    BtbUpdReadReqCombOut upd_req;
  };

  struct BtbPostReadReqCombIn {
    InputPayload inp;
    ReadData rd;
  };

  struct BtbPostReadReqCombOut {
    wire1_t pred_tc_read_valid;
    tc_idx_t pred_tc_idx;
    bht_hist_t upd_next_bht_data;
    wire1_t upd_tc_read_valid;
    tc_idx_t upd_tc_write_idx;
    tc_tag_t upd_tc_write_tag;
  };

  struct BtbTcDataSeqReadIn {
    wire1_t pred_tc_read_valid;
    tc_idx_t pred_tc_idx;
    wire1_t upd_tc_read_valid;
    tc_idx_t upd_tc_write_idx;
  };

  struct BtbPreReadCombIn {
    InputPayload inp;
  };

  struct BtbPreReadCombOut {
    BtbPredReadReqCombOut pred_req;
    BtbUpdReadReqCombOut upd_req;
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
    wire32_t state;
  };

  struct BtbXorshift32CombOut {
    wire32_t next_state;
  };

  struct BtbGetTagCombIn {
    pc_t pc;
  };

  struct BtbGetTagCombOut {
    btb_tag_t tag;
  };

  struct BtbGetIdxCombIn {
    pc_t pc;
  };

  struct BtbGetIdxCombOut {
    btb_idx_t idx;
  };

  struct BtbGetTypeIdxCombIn {
    pc_t pc;
  };

  struct BtbGetTypeIdxCombOut {
    btb_type_idx_t idx;
  };

  struct BhtGetIdxCombIn {
    pc_t pc;
  };

  struct BhtGetIdxCombOut {
    bht_idx_t idx;
  };

  struct TcGetIdxCombIn {
    pc_t pc;
    bht_hist_t bht_value;
  };

  struct TcGetIdxCombOut {
    tc_idx_t idx;
  };

  struct TcGetTagCombIn {
    pc_t pc;
  };

  struct TcGetTagCombOut {
    tc_tag_t tag;
  };

  struct BhtNextStateCombIn {
    bht_hist_t current_bht;
    br_type_t br_type;
    wire1_t pc_dir;
    target_addr_t actual_target;
  };

  struct BhtNextStateCombOut {
    bht_hist_t next_bht;
  };

  struct UsefulNextStateCombIn {
    wire3_t current_val;
    wire1_t correct;
  };

  struct UsefulNextStateCombOut {
    wire3_t next_val;
  };

  struct BtbHitCheckCombIn {
    BtbSetData set_data;
    btb_tag_t tag;
  };

  struct BtbHitCheckCombOut {
    HitCheckOut hit_info;
  };

  struct TcHitCheckCombIn {
    TcSetData set_data;
    tc_tag_t tag;
  };

  struct TcHitCheckCombOut {
    HitCheckOut hit_info;
  };

  struct BtbPredOutputCombIn {
    pc_t pc;
    br_type_t br_type;
    HitCheckOut hit_info;
    BtbSetData set_data;
    TcSetData tc_set;
  };

  struct BtbPredOutputCombOut {
    target_addr_t pred_target;
  };

  struct BtbVictimSelectCombIn {
    BtbSetData set_data;
  };

  struct BtbVictimSelectCombOut {
    btb_way_sel_t victim_way;
  };

  struct TcVictimSelectCombIn {
    TcSetData set_data;
  };

  struct TcVictimSelectCombOut {
    tc_way_sel_t victim_way;
  };

private:
  // 内存存储
  btb_tag_t mem_btb_tag[BTB_WAY_NUM][BTB_ENTRY_NUM];
  target_addr_t mem_btb_bta[BTB_WAY_NUM][BTB_ENTRY_NUM];
  wire1_t mem_btb_valid[BTB_WAY_NUM][BTB_ENTRY_NUM];
  wire3_t mem_btb_useful[BTB_WAY_NUM][BTB_ENTRY_NUM];

  bht_hist_t mem_bht[BHT_ENTRY_NUM];
  target_addr_t mem_tc_target[TC_WAY_NUM][TC_ENTRY_NUM];
  tc_tag_t mem_tc_tag[TC_WAY_NUM][TC_ENTRY_NUM];
  wire1_t mem_tc_valid[TC_WAY_NUM][TC_ENTRY_NUM];
  wire3_t mem_tc_useful[TC_WAY_NUM][TC_ENTRY_NUM];

  // Pipeline Registers
  btb_state_t state;
  wire1_t do_pred_latch;
  wire1_t do_upd_latch;
  pc_t upd_pc_latch;
  target_addr_t upd_actual_addr_latch;
  br_type_t upd_br_type_latch;
  wire1_t upd_actual_dir_latch;

  // Pipeline Regs (S1 to S2)
  pc_t pred_calc_pc_latch;
  btb_tag_t pred_calc_btb_tag_latch;
  btb_idx_t pred_calc_btb_idx_latch;
  btb_type_idx_t pred_calc_type_idx_latch;
  bht_idx_t pred_calc_bht_idx_latch;
  // uint32_t pred_calc_tc_idx_latch;

  // For Update Writeback (S1 calc result):
  bht_hist_t upd_calc_next_bht_val_latch;
  HitCheckOut upd_calc_hit_info_latch;
  btb_way_sel_t upd_calc_victim_way_latch;
  tc_way_sel_t upd_calc_w_target_way_latch;
  wire3_t upd_calc_next_useful_val_latch;
  wire1_t upd_calc_writes_btb_latch;

  // Outputs Registers
  OutputPayload out_regs;

  // SRAM延迟模拟相关变量（用于BTB和TC表项）
  wire1_t sram_delay_active;           // 是否正在进行延迟
  wire32_t sram_delay_counter;            // 剩余延迟周期数
  MemReadResult sram_delayed_data;  // 延迟期间保存的数据（包含BTB和TC）
  wire32_t sram_prng_state;          // 固定种子伪随机状态

public:
  BTB_TOP() { reset(); }

  void reset() {
    std::memset(mem_btb_tag, 0, sizeof(mem_btb_tag));
    std::memset(mem_btb_bta, 0, sizeof(mem_btb_bta));
    std::memset(mem_btb_valid, 0, sizeof(mem_btb_valid));
    std::memset(mem_btb_useful, 0, sizeof(mem_btb_useful));
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

  void btb_pred_read_req_comb(const BtbPredReadReqCombIn &in,
                              BtbPredReadReqCombOut &out) const {
    std::memset(&out, 0, sizeof(BtbPredReadReqCombOut));

    if (!in.inp.pred_req) {
      return;
    }

    out.pred_read_valid = true;
    out.pred_pc = in.inp.pred_pc;
    out.pred_btb_idx = btb_get_idx_value(in.inp.pred_pc);
    out.pred_bht_idx = bht_get_idx_value(in.inp.pred_pc);
    out.pred_tag = btb_get_tag_value(in.inp.pred_pc);
    out.pred_tc_idx = 0;
  }

  void btb_upd_read_req_comb(const BtbUpdReadReqCombIn &in,
                             BtbUpdReadReqCombOut &out) const {
    std::memset(&out, 0, sizeof(BtbUpdReadReqCombOut));

    if (!in.inp.upd_valid) {
      return;
    }

    out.upd_read_valid = true;
    out.upd_pc = in.inp.upd_pc;
    out.upd_actual_addr = in.inp.upd_actual_addr;
    out.upd_br_type_in = in.inp.upd_br_type_in;
    out.upd_actual_dir = in.inp.upd_actual_dir;
    out.upd_btb_idx = btb_get_idx_value(in.inp.upd_pc);
    out.upd_bht_idx = bht_get_idx_value(in.inp.upd_pc);
    out.upd_tag = btb_get_tag_value(in.inp.upd_pc);
    out.upd_next_bht_data = 0;
    out.upd_tc_read_valid = false;
    out.upd_tc_write_idx = 0;
    out.upd_tc_write_tag = 0;
  }

  void btb_pre_read_comb(const BtbPreReadCombIn &in,
                         BtbPreReadCombOut &out) const {
    std::memset(&out, 0, sizeof(BtbPreReadCombOut));
    btb_pred_read_req_comb(BtbPredReadReqCombIn{in.inp}, out.pred_req);
    btb_upd_read_req_comb(BtbUpdReadReqCombIn{in.inp}, out.upd_req);
  }

  void btb_post_read_req_comb(const BtbPostReadReqCombIn &in,
                              BtbPostReadReqCombOut &out) const {
    std::memset(&out, 0, sizeof(BtbPostReadReqCombOut));

    if (in.rd.pred_read_valid) {
      out.pred_tc_read_valid = true;
      out.pred_tc_idx = tc_get_idx_value(in.inp.pred_pc, in.rd.pred_bht_data);
    }

    if (!in.rd.upd_read_valid) {
      return;
    }

    out.upd_next_bht_data =
        (in.inp.upd_br_type_in != BR_NONCTL)
            ? bht_next_state_value(in.rd.upd_bht_data, in.inp.upd_br_type_in,
                                   in.inp.upd_actual_dir, in.inp.upd_actual_addr)
            : in.rd.upd_bht_data;

    if (in.inp.upd_actual_dir && in.inp.upd_br_type_in == BR_IDIRECT) {
      out.upd_tc_read_valid = true;
      out.upd_tc_write_idx = tc_get_idx_value(in.inp.upd_pc, out.upd_next_bht_data);
      out.upd_tc_write_tag = tc_get_tag_value(in.inp.upd_pc);
    }
  }

  // ------------------------------------------------------------------------
  // 组合逻辑函数 - 计算部分
  // ------------------------------------------------------------------------
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
          BtbPredOutputCombIn{inp.pred_pc, inp.pred_type_in, pred_hit_out.hit_info,
                              rd.pred_btb_set, rd.pred_tc_set},
          pred_out);
      out.pred_target = pred_out.pred_target;
      out.btb_pred_out_valid = true;
    }

    if (inp.upd_valid && rd.upd_read_valid) {
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
  }

  void btb_data_seq_read(const BtbDataSeqReadIn &in, ReadData &rd) const {
    rd.new_read_valid = false;
    rd.pred_read_valid = false;
    rd.pred_btb_idx = 0;
    rd.pred_bht_idx = 0;
    rd.pred_tc_idx = 0;
    rd.pred_tag = 0;
    rd.pred_type_data = 0;
    rd.pred_bht_data = 0;
    std::memset(&rd.pred_btb_set, 0, sizeof(rd.pred_btb_set));
    std::memset(&rd.pred_tc_set, 0, sizeof(rd.pred_tc_set));
    rd.upd_read_valid = false;
    rd.upd_btb_idx = 0;
    rd.upd_bht_idx = 0;
    rd.upd_tag = 0;
    rd.upd_bht_data = 0;
    rd.upd_next_bht_data = 0;
    std::memset(&rd.upd_btb_set, 0, sizeof(rd.upd_btb_set));
    rd.upd_tc_read_valid = false;
    rd.upd_tc_write_idx = 0;
    rd.upd_tc_write_tag = 0;
    std::memset(&rd.upd_tc_set, 0, sizeof(rd.upd_tc_set));

    rd.pred_read_valid = in.pred_req.pred_read_valid;
    rd.pred_btb_idx = in.pred_req.pred_btb_idx;
    rd.pred_bht_idx = in.pred_req.pred_bht_idx;
    rd.pred_tag = in.pred_req.pred_tag;
    rd.pred_tc_idx = 0;
    if (in.pred_req.pred_read_valid) {
      rd.pred_bht_data = mem_bht[rd.pred_bht_idx];
      for (int w = 0; w < BTB_WAY_NUM; ++w) {
        rd.pred_btb_set.tag[w] = mem_btb_tag[w][rd.pred_btb_idx];
        rd.pred_btb_set.bta[w] = mem_btb_bta[w][rd.pred_btb_idx];
        rd.pred_btb_set.valid[w] = mem_btb_valid[w][rd.pred_btb_idx];
        rd.pred_btb_set.useful[w] = mem_btb_useful[w][rd.pred_btb_idx];
      }
    }

    rd.upd_read_valid = in.upd_req.upd_read_valid;
    rd.upd_btb_idx = in.upd_req.upd_btb_idx;
    rd.upd_bht_idx = in.upd_req.upd_bht_idx;
    rd.upd_tag = in.upd_req.upd_tag;
    rd.upd_next_bht_data = 0;
    rd.upd_tc_read_valid = false;
    rd.upd_tc_write_idx = 0;
    rd.upd_tc_write_tag = 0;
    if (in.upd_req.upd_read_valid) {
      rd.upd_bht_data = mem_bht[rd.upd_bht_idx];
      for (int w = 0; w < BTB_WAY_NUM; ++w) {
        rd.upd_btb_set.tag[w] = mem_btb_tag[w][rd.upd_btb_idx];
        rd.upd_btb_set.bta[w] = mem_btb_bta[w][rd.upd_btb_idx];
        rd.upd_btb_set.valid[w] = mem_btb_valid[w][rd.upd_btb_idx];
        rd.upd_btb_set.useful[w] = mem_btb_useful[w][rd.upd_btb_idx];
      }
    }

    std::memset(&rd.idx_1, 0, sizeof(rd.idx_1));
    std::memset(&rd.mem_1, 0, sizeof(rd.mem_1));
    std::memset(&rd.idx_2, 0, sizeof(rd.idx_2));
    std::memset(&rd.mem_2, 0, sizeof(rd.mem_2));
  }

  void btb_tc_data_seq_read(const BtbTcDataSeqReadIn &in, ReadData &rd) const {
    std::memset(&rd.pred_tc_set, 0, sizeof(rd.pred_tc_set));
    std::memset(&rd.upd_tc_set, 0, sizeof(rd.upd_tc_set));

    if (in.pred_tc_read_valid) {
      for (int w = 0; w < TC_WAY_NUM; ++w) {
        rd.pred_tc_set.target[w] = mem_tc_target[w][in.pred_tc_idx];
        rd.pred_tc_set.tag[w] = mem_tc_tag[w][in.pred_tc_idx];
        rd.pred_tc_set.valid[w] = mem_tc_valid[w][in.pred_tc_idx];
        rd.pred_tc_set.useful[w] = mem_tc_useful[w][in.pred_tc_idx];
      }
    }

    if (in.upd_tc_read_valid) {
      for (int w = 0; w < TC_WAY_NUM; ++w) {
        rd.upd_tc_set.target[w] = mem_tc_target[w][in.upd_tc_write_idx];
        rd.upd_tc_set.tag[w] = mem_tc_tag[w][in.upd_tc_write_idx];
        rd.upd_tc_set.valid[w] = mem_tc_valid[w][in.upd_tc_write_idx];
        rd.upd_tc_set.useful[w] = mem_tc_useful[w][in.upd_tc_write_idx];
      }
    }
  }

  void btb_comb_calc(const InputPayload &inp, ReadData &rd,
                     OutputPayload &out, CombResult &req) const {
    BtbPreReadCombOut pre_read_out{};
    btb_pre_read_comb(BtbPreReadCombIn{inp}, pre_read_out);
    BtbPostReadReqCombOut post_read_out{};
    BtbCombOut comb_out{};
    btb_data_seq_read(BtbDataSeqReadIn{pre_read_out.pred_req, pre_read_out.upd_req}, rd);
    btb_post_read_req_comb(BtbPostReadReqCombIn{inp, rd}, post_read_out);
    rd.pred_tc_idx = post_read_out.pred_tc_idx;
    rd.upd_next_bht_data = post_read_out.upd_next_bht_data;
    rd.upd_tc_read_valid = post_read_out.upd_tc_read_valid;
    rd.upd_tc_write_idx = post_read_out.upd_tc_write_idx;
    rd.upd_tc_write_tag = post_read_out.upd_tc_write_tag;
    btb_tc_data_seq_read(BtbTcDataSeqReadIn{post_read_out.pred_tc_read_valid,
                                            post_read_out.pred_tc_idx,
                                            post_read_out.upd_tc_read_valid,
                                            post_read_out.upd_tc_write_idx},
                         rd);
    btb_comb(BtbCombIn{inp, rd}, comb_out);
    out = comb_out.out_regs;
    req = comb_out.req;
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

    state = req.next_state;
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
    constexpr int kBtbIdxBits = ceil_log2_u32(BTB_ENTRY_NUM);
    out.tag = ((in.pc >> 2) >> kBtbIdxBits) & BTB_TAG_MASK;
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
        out.victim_way = static_cast<btb_way_sel_t>(way);
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
    out.victim_way = static_cast<btb_way_sel_t>(min_useful_way);
  }

  static void tc_victim_select_comb(const TcVictimSelectCombIn &in,
                                    TcVictimSelectCombOut &out) {
    out = TcVictimSelectCombOut{};
    for (int way = 0; way < TC_WAY_NUM; way++) {
      if (!in.set_data.valid[way]) {
        out.victim_way = static_cast<tc_way_sel_t>(way);
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
    out.victim_way = static_cast<tc_way_sel_t>(min_useful_way);
  }

  static btb_tag_t btb_get_tag_value(pc_t pc) {
    BtbGetTagCombOut out{};
    btb_get_tag_comb(BtbGetTagCombIn{pc}, out);
    return out.tag;
  }

  static btb_idx_t btb_get_idx_value(pc_t pc) {
    BtbGetIdxCombOut out{};
    btb_get_idx_comb(BtbGetIdxCombIn{pc}, out);
    return out.idx;
  }

  static btb_type_idx_t btb_get_type_idx_value(pc_t pc) {
    BtbGetTypeIdxCombOut out{};
    btb_get_type_idx_comb(BtbGetTypeIdxCombIn{pc}, out);
    return out.idx;
  }

  static bht_idx_t bht_get_idx_value(pc_t pc) {
    BhtGetIdxCombOut out{};
    bht_get_idx_comb(BhtGetIdxCombIn{pc}, out);
    return out.idx;
  }

  static tc_idx_t tc_get_idx_value(pc_t pc, bht_hist_t bht_value) {
    TcGetIdxCombOut out{};
    tc_get_idx_comb(TcGetIdxCombIn{pc, bht_value}, out);
    return out.idx;
  }

  static tc_tag_t tc_get_tag_value(pc_t pc) {
    TcGetTagCombOut out{};
    tc_get_tag_comb(TcGetTagCombIn{pc}, out);
    return out.tag;
  }

  static bht_hist_t bht_next_state_value(bht_hist_t current_bht, br_type_t br_type,
                                         wire1_t pc_dir, target_addr_t actual_target) {
    BhtNextStateCombOut out{};
    bht_next_state_comb(BhtNextStateCombIn{current_bht, br_type, pc_dir, actual_target},
                        out);
    return out.next_bht;
  }

  static wire3_t useful_next_state_value(wire3_t current_val, wire1_t correct) {
    UsefulNextStateCombOut out{};
    useful_next_state_comb(UsefulNextStateCombIn{current_val, correct}, out);
    return out.next_val;
  }
};

#endif // BTB_TOP_H
