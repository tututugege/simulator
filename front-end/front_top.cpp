#include "BPU/BPU.h"
#include "front_IO.h"
#include "front_module.h"
#include "predecode.h"
#include "predecode_checker.h"
#include <RISCV.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ============================================================================
// 全局状态（寄存器锁存值）
// ============================================================================
static bool predecode_refetch = false;
static uint32_t predecode_refetch_address = 0;
static uint32_t front_sim_time = 0;

// FIFO 状态锁存
static bool fetch_addr_fifo_full_latch = false;
static bool fetch_addr_fifo_empty_latch = true;
static bool fifo_full_latch = false;
static bool fifo_empty_latch = true;
static bool ptab_full_latch = false;
static bool ptab_empty_latch = true;
static bool front2back_fifo_full_latch = false;
static bool front2back_fifo_empty_latch = true;

struct FrontRuntimeStats {
  uint64_t cycles = 0;
  uint64_t reset_cycles = 0;
  uint64_t ext_refetch_cycles = 0;
  uint64_t delayed_refetch_cycles = 0;
  uint64_t global_refetch_cycles = 0;

  uint64_t bpu_can_run_cycles = 0;
  uint64_t bpu_stall_cycles = 0;
  uint64_t bpu_stall_fetch_addr_full_cycles = 0;
  uint64_t bpu_stall_ptab_full_cycles = 0;
  uint64_t bpu_issue_cycles = 0;

  uint64_t fetch_addr_read_slot0_cycles = 0;
  uint64_t fetch_addr_read_slot1_cycles = 0;
  uint64_t fetch_addr_write_normal_cycles = 0;
  uint64_t fetch_addr_write_twoahead_cycles = 0;
  uint64_t fetch_addr_write_skip_by_mini_flush_correct_cycles = 0;

  uint64_t icache_req_slot0_cycles = 0;
  uint64_t icache_req_slot1_cycles = 0;
  uint64_t icache_resp_slot0_cycles = 0;
  uint64_t icache_resp_slot1_cycles = 0;

  uint64_t inst_fifo_write_slot0_cycles = 0;
  uint64_t inst_fifo_write_slot1_cycles = 0;
  uint64_t ptab_write_cycles = 0;

  uint64_t predecode_run_cycles = 0;
  uint64_t predecode_block_fifo_empty_cycles = 0;
  uint64_t predecode_block_ptab_empty_cycles = 0;
  uint64_t predecode_block_front2back_full_cycles = 0;
  uint64_t predecode_block_dummy_ptab_cycles = 0;
  uint64_t checker_run_cycles = 0;
  uint64_t checker_flush_cycles = 0;
  uint64_t mini_flush_req_cycles = 0;
  uint64_t mini_flush_correct_cycles = 0;

  uint64_t front2back_write_cycles = 0;
  uint64_t front2back_read_req_cycles = 0;
  uint64_t front2back_valid_out_cycles = 0;

  uint64_t bypass_fetch_to_icache_opportunity_cycles = 0;
  uint64_t bypass_icache_to_predecode_opportunity_cycles = 0;
  uint64_t bypass_front2back_to_output_opportunity_cycles = 0;
  uint64_t bypass_front2back_to_output_hit_cycles = 0;
};

static FrontRuntimeStats front_stats;

static void front_stats_print_summary() {
#if !FRONTEND_ENABLE_RUNTIME_STATS_SUMMARY
  return;
#else
  if (front_stats.cycles == 0) {
    return;
  }
  std::printf(
      "\n[FRONT-STATS] cycles=%llu reset=%llu ext_refetch=%llu delayed_refetch=%llu global_refetch=%llu\n",
      static_cast<unsigned long long>(front_stats.cycles),
      static_cast<unsigned long long>(front_stats.reset_cycles),
      static_cast<unsigned long long>(front_stats.ext_refetch_cycles),
      static_cast<unsigned long long>(front_stats.delayed_refetch_cycles),
      static_cast<unsigned long long>(front_stats.global_refetch_cycles));
  std::printf(
      "[FRONT-STATS] bpu can_run=%llu stall=%llu stall_fetch_addr_full=%llu stall_ptab_full=%llu issue=%llu\n",
      static_cast<unsigned long long>(front_stats.bpu_can_run_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_fetch_addr_full_cycles),
      static_cast<unsigned long long>(front_stats.bpu_stall_ptab_full_cycles),
      static_cast<unsigned long long>(front_stats.bpu_issue_cycles));
  std::printf(
      "[FRONT-STATS] fetch_addr rd0=%llu rd1=%llu wr_normal=%llu wr_2ahead=%llu skip_mini_flush_correct=%llu\n",
      static_cast<unsigned long long>(front_stats.fetch_addr_read_slot0_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_read_slot1_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_normal_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_twoahead_cycles),
      static_cast<unsigned long long>(front_stats.fetch_addr_write_skip_by_mini_flush_correct_cycles));
  std::printf(
      "[FRONT-STATS] icache req0=%llu req1=%llu resp0=%llu resp1=%llu inst_fifo_wr0=%llu inst_fifo_wr1=%llu ptab_wr=%llu\n",
      static_cast<unsigned long long>(front_stats.icache_req_slot0_cycles),
      static_cast<unsigned long long>(front_stats.icache_req_slot1_cycles),
      static_cast<unsigned long long>(front_stats.icache_resp_slot0_cycles),
      static_cast<unsigned long long>(front_stats.icache_resp_slot1_cycles),
      static_cast<unsigned long long>(front_stats.inst_fifo_write_slot0_cycles),
      static_cast<unsigned long long>(front_stats.inst_fifo_write_slot1_cycles),
      static_cast<unsigned long long>(front_stats.ptab_write_cycles));
  std::printf(
      "[FRONT-STATS] predecode run=%llu block_fifo_empty=%llu block_ptab_empty=%llu block_f2b_full=%llu block_ptab_dummy=%llu checker_run=%llu checker_flush=%llu mini_req=%llu mini_correct=%llu\n",
      static_cast<unsigned long long>(front_stats.predecode_run_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_fifo_empty_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_ptab_empty_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_front2back_full_cycles),
      static_cast<unsigned long long>(front_stats.predecode_block_dummy_ptab_cycles),
      static_cast<unsigned long long>(front_stats.checker_run_cycles),
      static_cast<unsigned long long>(front_stats.checker_flush_cycles),
      static_cast<unsigned long long>(front_stats.mini_flush_req_cycles),
      static_cast<unsigned long long>(front_stats.mini_flush_correct_cycles));
  std::printf(
      "[FRONT-STATS] front2back write=%llu read_req=%llu valid_out=%llu\n",
      static_cast<unsigned long long>(front_stats.front2back_write_cycles),
      static_cast<unsigned long long>(front_stats.front2back_read_req_cycles),
      static_cast<unsigned long long>(front_stats.front2back_valid_out_cycles));
  std::printf(
      "[FRONT-STATS] bypass_opp fetch_to_icache=%llu icache_to_predecode=%llu f2b_to_output=%llu f2b_hit=%llu\n",
      static_cast<unsigned long long>(front_stats.bypass_fetch_to_icache_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_icache_to_predecode_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_front2back_to_output_opportunity_cycles),
      static_cast<unsigned long long>(front_stats.bypass_front2back_to_output_hit_cycles));
#endif
}

struct FrontStatsAtExit {
  ~FrontStatsAtExit() { front_stats_print_summary(); }
};

static FrontStatsAtExit front_stats_at_exit;

static BPU_TOP bpu_instance;
struct PendingBpuSeqTxn {
  bool valid = false;
  bool reset = false;
  BPU_TOP::InputPayload inp;
  BPU_TOP::UpdateRequest req;
};
struct PendingFrontState {
  bool valid = false;
  uint32_t next_front_sim_time = 0;
  FrontRuntimeStats next_front_stats{};
  bool next_predecode_refetch = false;
  uint32_t next_predecode_refetch_address = 0;
  bool next_fetch_addr_fifo_full = false;
  bool next_fetch_addr_fifo_empty = true;
  bool next_fifo_full = false;
  bool next_fifo_empty = true;
  bool next_ptab_full = false;
  bool next_ptab_empty = true;
  bool next_front2back_fifo_full = false;
  bool next_front2back_fifo_empty = true;
};

struct FrontReadData {
  bool predecode_refetch_snapshot = false;
  uint32_t predecode_refetch_address_snapshot = 0;
  uint32_t front_sim_time_snapshot = 0;
  FrontRuntimeStats front_stats_snapshot{};
  bool fetch_addr_fifo_full_latch_snapshot = false;
  bool fetch_addr_fifo_empty_latch_snapshot = true;
  bool fifo_full_latch_snapshot = false;
  bool fifo_empty_latch_snapshot = true;
  bool ptab_full_latch_snapshot = false;
  bool ptab_empty_latch_snapshot = true;
  bool front2back_fifo_full_latch_snapshot = false;
  bool front2back_fifo_empty_latch_snapshot = true;
};

// 定义全局指针，供TAGE访问BPU的GHR/FH
BPU_TOP *g_bpu_top = &bpu_instance;
const bool* BPU_get_Arch_GHR() {
  assert(g_bpu_top && "g_bpu_top must be initialized before BPU_get_Arch_GHR");
  return g_bpu_top->Arch_GHR;
}

const bool* BPU_get_Spec_GHR() {
  assert(g_bpu_top && "g_bpu_top must be initialized before BPU_get_Spec_GHR");
  return g_bpu_top->Spec_GHR;
}

const uint32_t (*BPU_get_Arch_FH())[TN_MAX] {
  assert(g_bpu_top && "g_bpu_top must be initialized before BPU_get_Arch_FH");
  return g_bpu_top->Arch_FH;
}

const uint32_t (*BPU_get_Spec_FH())[TN_MAX] {
  assert(g_bpu_top && "g_bpu_top must be initialized before BPU_get_Spec_FH");
  return g_bpu_top->Spec_FH;
}

// ============================================================================
// 辅助函数
// ============================================================================

// 准备 BPU 输入
static void prepare_bpu_input(struct front_top_in *in, 
                               struct BPU_in *bpu_in,
                               bool do_refetch,
                               uint32_t refetch_addr,
                               bool icache_ready) {
    bpu_in->reset = in->reset;
    bpu_in->refetch = do_refetch;
    bpu_in->refetch_address = refetch_addr;
    bpu_in->icache_read_ready = icache_ready;
    
    for (int i = 0; i < COMMIT_WIDTH; i++) {
        bpu_in->back2front_valid[i] = in->back2front_valid[i];
        bpu_in->predict_base_pc[i] = in->predict_base_pc[i];
        bpu_in->actual_dir[i] = in->actual_dir[i];
        bpu_in->actual_br_type[i] = in->actual_br_type[i];
        bpu_in->actual_target[i] = in->actual_target[i];
        bpu_in->predict_dir[i] = in->predict_dir[i];
        bpu_in->alt_pred[i] = in->alt_pred[i];
        bpu_in->altpcpn[i] = in->altpcpn[i];
        bpu_in->pcpn[i] = in->pcpn[i];
        for (int j = 0; j < 4; j++) {
            bpu_in->tage_idx[i][j] = in->tage_idx[i][j];
            bpu_in->tage_tag[i][j] = in->tage_tag[i][j];
        }
    }
}

// 运行 BPU 组合阶段并转换输出（时序提交延后到 front_seq_write）
static void run_bpu(struct BPU_in *bpu_in, struct BPU_out *bpu_out,
                    PendingBpuSeqTxn &bpu_seq_txn) {
    bpu_seq_txn.valid = false;
    bpu_seq_txn.reset = bpu_in->reset;
    BPU_TOP::InputPayload bpu_input;
    std::memset(&bpu_input, 0, sizeof(bpu_input));
    bpu_input.refetch = bpu_in->refetch;
    bpu_input.refetch_address = bpu_in->refetch_address;
    bpu_input.icache_read_ready = bpu_in->icache_read_ready;
    
    for (int i = 0; i < COMMIT_WIDTH; i++) {
        bpu_input.in_update_base_pc[i] = bpu_in->predict_base_pc[i];
        bpu_input.in_upd_valid[i] = bpu_in->back2front_valid[i];
        bpu_input.in_actual_dir[i] = bpu_in->actual_dir[i];
        bpu_input.in_actual_br_type[i] = bpu_in->actual_br_type[i];
        bpu_input.in_actual_targets[i] = bpu_in->actual_target[i];
        bpu_input.in_pred_dir[i] = bpu_in->predict_dir[i];
        bpu_input.in_alt_pred[i] = bpu_in->alt_pred[i];
        bpu_input.in_pcpn[i] = bpu_in->pcpn[i];
        bpu_input.in_altpcpn[i] = bpu_in->altpcpn[i];
        for (int j = 0; j < 4; j++) {
            bpu_input.in_tage_tags[i][j] = bpu_in->tage_tag[i][j];
            bpu_input.in_tage_idxs[i][j] = bpu_in->tage_idx[i][j];
        }
    }
    
    BPU_TOP::OutputPayload bpu_output;
    std::memset(&bpu_output, 0, sizeof(BPU_TOP::OutputPayload));
    if (bpu_in->reset) {
        bpu_seq_txn.valid = true;
        bpu_seq_txn.inp = bpu_input;
        bpu_seq_txn.req = BPU_TOP::UpdateRequest{};
        bpu_seq_txn.reset = true;
        bpu_output.fetch_address = RESET_PC;
        bpu_output.two_ahead_target = bpu_output.fetch_address + (FETCH_WIDTH * 4);
    } else {
        BPU_TOP::ReadData bpu_rd;
        BPU_TOP::UpdateRequest bpu_req;
        bpu_instance.bpu_seq_read(bpu_input, bpu_rd);
        bpu_instance.bpu_comb_calc(bpu_input, bpu_rd, bpu_output, bpu_req);
        bpu_seq_txn.valid = true;
        bpu_seq_txn.inp = bpu_input;
        bpu_seq_txn.req = bpu_req;
        bpu_seq_txn.reset = false;
    }
    
    bpu_out->icache_read_valid = bpu_output.icache_read_valid;
    bpu_out->fetch_address = bpu_output.fetch_address;
    bpu_out->PTAB_write_enable = bpu_output.PTAB_write_enable;
    bpu_out->predict_next_fetch_address = bpu_output.predict_next_fetch_address;
    
    // 2-Ahead Predictor outputs
    bpu_out->two_ahead_valid = bpu_output.two_ahead_valid;
    bpu_out->two_ahead_target = bpu_output.two_ahead_target;
    bpu_out->mini_flush_req = bpu_output.mini_flush_req;
    bpu_out->mini_flush_target = bpu_output.mini_flush_target;
    bpu_out->mini_flush_correct = bpu_output.mini_flush_correct;
    
    for (int i = 0; i < FETCH_WIDTH; i++) {
        bpu_out->predict_dir[i] = bpu_output.out_pred_dir[i];
        bpu_out->predict_base_pc[i] = bpu_output.out_pred_base_pc + (i * 4);
        bpu_out->alt_pred[i] = bpu_output.out_alt_pred[i];
        bpu_out->altpcpn[i] = bpu_output.out_altpcpn[i];
        bpu_out->pcpn[i] = bpu_output.out_pcpn[i];
        for (int j = 0; j < 4; j++) {
            bpu_out->tage_idx[i][j] = bpu_output.out_tage_idxs[i][j];
            bpu_out->tage_tag[i][j] = bpu_output.out_tage_tags[i][j];
        }
    }
}

// ============================================================================
// 主函数
// ============================================================================
static void front_comb_calc_impl(const FrontReadData &rd, struct front_top_in *in, struct front_top_out *out,
                                 PendingBpuSeqTxn &bpu_seq_txn_req,
                                 PendingFrontState &front_state_req) {
    uint32_t front_sim_time = rd.front_sim_time_snapshot + 1;
    FrontRuntimeStats front_stats = rd.front_stats_snapshot;
    DEBUG_LOG_SMALL("--------front_top sim_time: %d----------------\n", front_sim_time);
    front_stats.cycles++;
    front_state_req.valid = false;
    
    // ========================================================================
    // 阶段 0: 初始化所有模块的输入输出结构
    // ========================================================================
    struct BPU_in bpu_in;
    struct BPU_out bpu_out;
    struct fetch_address_FIFO_in fetch_addr_fifo_in;
    struct fetch_address_FIFO_out fetch_addr_fifo_out;
    struct icache_in icache_in;
    struct icache_out icache_out;
    struct instruction_FIFO_in fifo_in;
    struct instruction_FIFO_out fifo_out;
    struct PTAB_in ptab_in;
    struct PTAB_out ptab_out;
    struct front2back_FIFO_in front2back_fifo_in;
    struct front2back_FIFO_out front2back_fifo_out;
    
    memset(&bpu_in, 0, sizeof(bpu_in));
    memset(&bpu_out, 0, sizeof(bpu_out));
    memset(&fetch_addr_fifo_in, 0, sizeof(fetch_addr_fifo_in));
    memset(&fetch_addr_fifo_out, 0, sizeof(fetch_addr_fifo_out));
    memset(&icache_in, 0, sizeof(icache_in));
    memset(&icache_out, 0, sizeof(icache_out));
    memset(&fifo_in, 0, sizeof(fifo_in));
    memset(&fifo_out, 0, sizeof(fifo_out));
    memset(&ptab_in, 0, sizeof(ptab_in));
    memset(&ptab_out, 0, sizeof(ptab_out));
    memset(&front2back_fifo_in, 0, sizeof(front2back_fifo_in));
    memset(&front2back_fifo_out, 0, sizeof(front2back_fifo_out));
    
    // ========================================================================
    // 阶段 1: 计算全局 flush/refetch 信号
    // ========================================================================
    bool global_reset = in->reset;
    // predecode refetch is delayed for 1 cycle
    bool global_refetch = in->refetch || rd.predecode_refetch_snapshot;
    // only BPU use this address
    uint32_t refetch_address = in->refetch ? in->refetch_address : rd.predecode_refetch_address_snapshot;
    if (global_reset) {
        front_stats.reset_cycles++;
    }
    if (in->refetch) {
        front_stats.ext_refetch_cycles++;
    }
    if (rd.predecode_refetch_snapshot) {
        front_stats.delayed_refetch_cycles++;
    }
    if (global_refetch) {
        front_stats.global_refetch_cycles++;
    }
    
    // ========================================================================
    // 阶段 2: 确定各 FIFO 的读使能（在实际读取前先决策）
    // ========================================================================
    
    // fetch_address_FIFO 读使能：icache 准备好接收 且 FIFO 非空
    // 需要先获取 icache 的 ready 状态
#ifdef USE_TRUE_ICACHE
    icache_in.reset = global_reset;
    icache_in.refetch = global_refetch;
    icache_in.csr_status = in->csr_status;
    icache_in.run_comb_only = true;
    icache_comb_calc(&icache_in, &icache_out);
#endif
    bool icache_ready = icache_out.icache_read_ready;
    bool icache_ready_2 = icache_out.icache_read_ready_2;
#ifdef USE_IDEAL_ICACHE
    icache_ready = true;
    icache_ready_2 = true;
#endif
    DEBUG_LOG_SMALL_4("icache_ready: %d, icache_ready_2: %d\n", icache_ready, icache_ready_2);
    bool fetch_addr_fifo_read_enable_slot0 =
        icache_ready && !rd.fetch_addr_fifo_empty_latch_snapshot && !global_reset && !global_refetch;
    bool fetch_addr_fifo_read_enable_slot1_candidate = false;
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
    fetch_addr_fifo_read_enable_slot1_candidate =
        fetch_addr_fifo_read_enable_slot0 && icache_ready_2;
#else
    (void)fetch_addr_fifo_read_enable_slot1_candidate;
#endif
#ifdef USE_TRUE_ICACHE
    assert(!fetch_addr_fifo_read_enable_slot1_candidate);
#endif
    
    // instruction_FIFO 和 PTAB 读使能：predecode checker 可以工作
    // 条件：两个 FIFO 都非空 且 front2back_fifo 未满
    bool predecode_can_run_old = !rd.fifo_empty_latch_snapshot && !rd.ptab_empty_latch_snapshot &&
                              !rd.front2back_fifo_full_latch_snapshot && !global_reset && !global_refetch;
    bool inst_fifo_read_enable = predecode_can_run_old;
    // bool ptab_read_enable = predecode_can_run;
    bool ptab_read_enable = predecode_can_run_old;
    // if(predecode_can_run_old) {
    //     bool ptab_stay_more = ptab_peek_mini_flush();
    //     if(!ptab_stay_more){
    //         ptab_read_enable = true;
    //     }else {
    //         DEBUG_LOG_SMALL_4("peek-ptab\n");
    //         ptab_read_enable = false;
    //     }
    // }
    // bool predecode_can_run = ptab_read_enable && inst_fifo_read_enable;
    
    // front2back_FIFO 读使能：后端请求读取
    // refetch and reset deal when running
    bool front2back_read_enable = in->FIFO_read_enable;
    if (front2back_read_enable) {
        front_stats.front2back_read_req_cycles++;
    }
    
    // ========================================================================
    // 阶段 3: 执行所有 FIFO 的读操作（获取输出数据）
    // read last cycle's data
    // ========================================================================
    
    // 3.1 读取 fetch_address_FIFO
    fetch_addr_fifo_in.reset = global_reset;
    fetch_addr_fifo_in.refetch = global_refetch;
    fetch_addr_fifo_in.read_enable = fetch_addr_fifo_read_enable_slot0;
    fetch_addr_fifo_in.write_enable = false;  // 写操作稍后处理
    fetch_addr_fifo_in.fetch_address = 0;
    fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_out);

    struct fetch_address_FIFO_out saved_fetch_addr_fifo_out_0 = fetch_addr_fifo_out;
    struct fetch_address_FIFO_out saved_fetch_addr_fifo_out_1;
    memset(&saved_fetch_addr_fifo_out_1, 0, sizeof(saved_fetch_addr_fifo_out_1));

    bool fetch_addr_fifo_read_enable_slot1 = false;
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
    fetch_addr_fifo_read_enable_slot1 =
        fetch_addr_fifo_read_enable_slot1_candidate && !fetch_addr_fifo_out.empty;
#endif
    if (fetch_addr_fifo_read_enable_slot1) {
        fetch_addr_fifo_in.read_enable = true;
        fetch_addr_fifo_in.write_enable = false;
        fetch_addr_fifo_in.fetch_address = 0;
        fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_out);
        saved_fetch_addr_fifo_out_1 = fetch_addr_fifo_out;
    }
    if (saved_fetch_addr_fifo_out_0.read_valid) {
        front_stats.fetch_addr_read_slot0_cycles++;
    }
    if (saved_fetch_addr_fifo_out_1.read_valid) {
        front_stats.fetch_addr_read_slot1_cycles++;
    }
    
    // 3.2 读取 instruction_FIFO
    fifo_in.reset = global_reset;
    fifo_in.refetch = global_refetch;
    fifo_in.read_enable = inst_fifo_read_enable;
    fifo_in.write_enable = false;  // 写操作稍后处理
    instruction_FIFO_comb_calc(&fifo_in, &fifo_out);
    
    // 3.3 读取 PTAB
    ptab_in.reset = global_reset;
    ptab_in.refetch = global_refetch;
    ptab_in.read_enable = ptab_read_enable;
    ptab_in.write_enable = false;  // 写操作稍后处理
    PTAB_comb_calc(&ptab_in, &ptab_out);
    
    // 3.4 读取 front2back_FIFO
    front2back_fifo_in.reset = global_reset;
    // f2b不用关心预解码flush
    front2back_fifo_in.refetch = in->refetch;
    front2back_fifo_in.read_enable = front2back_read_enable;
    front2back_fifo_in.write_enable = false;  // 写操作稍后处理
    front2back_FIFO_comb_calc(&front2back_fifo_in, &front2back_fifo_out);
    
    // 保存读出的数据用于后续处理
    struct instruction_FIFO_out saved_fifo_out = fifo_out;
    struct PTAB_out saved_ptab_out = ptab_out;
    struct front2back_FIFO_out saved_front2back_fifo_out = front2back_fifo_out;
    struct front2back_FIFO_out bypass_front2back_fifo_out;
    memset(&bypass_front2back_fifo_out, 0, sizeof(bypass_front2back_fifo_out));
    bool use_front2back_output_bypass = false;

    // 当前ptab的出队为dummy entry，则不能进行预解码
    bool predecode_can_run = !saved_ptab_out.dummy_entry && inst_fifo_read_enable;
    if (predecode_can_run) {
        front_stats.predecode_run_cycles++;
    } else {
        if (rd.fifo_empty_latch_snapshot) {
            front_stats.predecode_block_fifo_empty_cycles++;
        }
        if (rd.ptab_empty_latch_snapshot) {
            front_stats.predecode_block_ptab_empty_cycles++;
        }
        if (rd.front2back_fifo_full_latch_snapshot) {
            front_stats.predecode_block_front2back_full_cycles++;
        }
        if (inst_fifo_read_enable && saved_ptab_out.dummy_entry) {
            front_stats.predecode_block_dummy_ptab_cycles++;
        }
    }
    
    // ========================================================================
    // 阶段 4: BPU 控制逻辑
    // ========================================================================
    // BPU 阻塞条件：fetch_address_FIFO 满 或 PTAB 满
    bool bpu_stall = rd.fetch_addr_fifo_full_latch_snapshot || rd.ptab_full_latch_snapshot;
    bool bpu_can_run = !bpu_stall || global_reset || global_refetch;
    if (bpu_can_run) {
        front_stats.bpu_can_run_cycles++;
    }
    if (bpu_stall) {
        front_stats.bpu_stall_cycles++;
        if (rd.fetch_addr_fifo_full_latch_snapshot) {
            front_stats.bpu_stall_fetch_addr_full_cycles++;
        }
        if (rd.ptab_full_latch_snapshot) {
            front_stats.bpu_stall_ptab_full_cycles++;
        }
    }
    
    // BPU 看到的 icache_ready 是 fetch_address_FIFO 是否有空位
    bool bpu_icache_ready = !rd.fetch_addr_fifo_full_latch_snapshot;
    
    prepare_bpu_input(in, &bpu_in, global_refetch, refetch_address, bpu_icache_ready);
    if (!bpu_can_run) {
        bpu_in.icache_read_ready = false;
    }
    run_bpu(&bpu_in, &bpu_out, bpu_seq_txn_req);
    if (bpu_out.mini_flush_req) {
        front_stats.mini_flush_req_cycles++;
    }
    if (bpu_out.mini_flush_correct) {
        front_stats.mini_flush_correct_cycles++;
    }
    
    if (bpu_out.icache_read_valid && bpu_can_run) {
        front_stats.bpu_issue_cycles++;
        DEBUG_LOG_SMALL("[front_top] sim_time: %d, bpu_out.fetch_address: %x\n",
                        front_sim_time, bpu_out.fetch_address);
    }
    
    // ========================================================================
    // 阶段 5: fetch_address_FIFO 写控制（支持双写和Mini Flush）
    // ========================================================================
    fetch_addr_fifo_in.reset = false;
    fetch_addr_fifo_in.refetch = false;
    fetch_addr_fifo_in.read_enable = false;  // 读已经做过了
    // 相对于当周期写入新值，但最早下周期才会消费
    bool normal_write_enable = bpu_out.icache_read_valid && bpu_can_run && !global_reset;
    // 1. 刚好在BPUfire的当拍来了一个refetch，需要写，不然会掉指令
    // 2. cause BPU takes at least 1 cycle to finish refetch, no 2-write problem
    // fetch_addr_fifo_in.write_enable = normal_write_enable || refetch_write_enable;
    // fetch_addr_fifo_in.fetch_address = normal_write_enable ? bpu_out.fetch_address : refetch_address;
    
    // if (fetch_addr_fifo_in.write_enable) {
    //     // 读的时候也会处理一波reset和refetch
    //     fetch_address_FIFO_top(&fetch_addr_fifo_in, &fetch_addr_fifo_out);
    // }
    // if (refetch_write_enable) {
    //     fetch_addr_fifo_in.write_enable = true;
    //     fetch_addr_fifo_in.fetch_address = refetch_address;
    //     DEBUG_LOG_SMALL_4("refetch write enable: %x\n", refetch_address);
    //     fetch_address_FIFO_top(&fetch_addr_fifo_in, &fetch_addr_fifo_out);
    // } else 
    DEBUG_LOG_SMALL_4("normal_write_enable: %d, bpu_out.mini_flush_correct: %d\n", normal_write_enable, bpu_out.mini_flush_correct);
    bool can_bypass_fetch_to_icache =
        !saved_fetch_addr_fifo_out_0.read_valid && normal_write_enable &&
        !bpu_out.mini_flush_correct && icache_ready && !global_refetch;
    if (can_bypass_fetch_to_icache) {
        front_stats.bypass_fetch_to_icache_opportunity_cycles++;
    }
    // 首先看能不能节省一个写
    // 未开启2-Ahead模式下correct永远为0，保证正常写入
    if (normal_write_enable && !bpu_out.mini_flush_correct) {
        front_stats.fetch_addr_write_normal_cycles++;
        fetch_addr_fifo_in.write_enable = true;
        fetch_addr_fifo_in.fetch_address = bpu_out.fetch_address;
        DEBUG_LOG_SMALL_4("normal write enable: %x\n", bpu_out.fetch_address);
        fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_out);
    } else if (normal_write_enable && bpu_out.mini_flush_correct) {
        front_stats.fetch_addr_write_skip_by_mini_flush_correct_cycles++;
    }
#ifdef ENABLE_2AHEAD
    // refetch的时候也要写2ahead target
    if (normal_write_enable) {
        front_stats.fetch_addr_write_twoahead_cycles++;
        fetch_addr_fifo_in.write_enable = true;
        fetch_addr_fifo_in.fetch_address = bpu_out.two_ahead_target;
        DEBUG_LOG_SMALL_4("2ahead write enable: %x\n", bpu_out.two_ahead_target);
        fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_out);
    }
#endif

    // ========================================================================
    // 阶段 6: icache 控制逻辑
    // ========================================================================
    icache_in.reset = global_reset;
    icache_in.refetch = global_refetch;
    icache_in.csr_status = in->csr_status;
    icache_in.run_comb_only = false;
    
    // icache 从 fetch_address_FIFO 获取地址
    // 注意用的是阶段3读取的旧值
    if (saved_fetch_addr_fifo_out_0.read_valid) {
        icache_in.icache_read_valid = true;
        icache_in.fetch_address = saved_fetch_addr_fifo_out_0.fetch_address;
        DEBUG_LOG_SMALL_4("icache_in.fetch_address: %x\n", icache_in.fetch_address);
    } else {
        icache_in.icache_read_valid = false;
        icache_in.fetch_address = 0;
    }
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
    if (saved_fetch_addr_fifo_out_1.read_valid) {
        icache_in.icache_read_valid_2 = true;
        icache_in.fetch_address_2 = saved_fetch_addr_fifo_out_1.fetch_address;
        DEBUG_LOG_SMALL_4("icache_in.fetch_address_2: %x\n", icache_in.fetch_address_2);
    } else {
        icache_in.icache_read_valid_2 = false;
        icache_in.fetch_address_2 = 0;
    }
#else
    icache_in.icache_read_valid_2 = false;
    icache_in.fetch_address_2 = 0;
#endif
#ifdef USE_TRUE_ICACHE
    assert(!icache_in.icache_read_valid_2);
#endif
    if (icache_in.icache_read_valid) {
        front_stats.icache_req_slot0_cycles++;
    }
    if (icache_in.icache_read_valid_2) {
        front_stats.icache_req_slot1_cycles++;
    }
    
    icache_comb_calc(&icache_in, &icache_out);
    
    // ========================================================================
    // 阶段 7: instruction_FIFO 写控制（写入 icache 返回的数据）
    // ========================================================================
    fifo_in.reset = global_reset;
    fifo_in.refetch = global_refetch;
    fifo_in.read_enable = false;  // 读已经做过了
    fifo_in.write_enable = false;

    bool icache_slot0_data_valid = icache_out.icache_read_complete;
    bool icache_slot1_data_valid = false;
#ifdef USE_IDEAL_ICACHE
    icache_slot0_data_valid &= icache_in.icache_read_valid;
#endif
#if FRONTEND_IDEAL_ICACHE_DUAL_REQ_ACTIVE
    icache_slot1_data_valid = icache_out.icache_read_complete_2 && icache_in.icache_read_valid_2;
#endif
#ifdef USE_TRUE_ICACHE
    assert(!icache_slot1_data_valid);
#endif
    if (icache_slot0_data_valid) {
        front_stats.icache_resp_slot0_cycles++;
    }
    if (icache_slot1_data_valid) {
        front_stats.icache_resp_slot1_cycles++;
    }

    bool inst_fifo_full_for_write = rd.fifo_full_latch_snapshot;
    bool can_bypass_icache_to_predecode =
        !predecode_can_run && rd.fifo_empty_latch_snapshot && !rd.ptab_empty_latch_snapshot &&
        !rd.front2back_fifo_full_latch_snapshot && !global_reset && !global_refetch &&
        icache_slot0_data_valid;
    if (can_bypass_icache_to_predecode) {
        front_stats.bypass_icache_to_predecode_opportunity_cycles++;
    }
    auto write_inst_fifo_entry = [&](const uint32_t *fetch_group, uint32_t fetch_pc,
                                     const bool *page_fault_inst, const bool *inst_valid) {
        fifo_in.write_enable = true;
        for (int i = 0; i < FETCH_WIDTH; i++) {
            fifo_in.fetch_group[i] = fetch_group[i];
            fifo_in.pc[i] = fetch_pc + (i * 4);
            fifo_in.page_fault_inst[i] = page_fault_inst[i];
            fifo_in.inst_valid[i] = inst_valid[i];

            if (inst_valid[i]) {
                uint32_t current_pc = fetch_pc + (i * 4);
                PredecodeResult result = predecode_instruction(fetch_group[i], current_pc);
                fifo_in.predecode_type[i] = result.type;
                fifo_in.predecode_target_address[i] = result.target_address;
                DEBUG_LOG_SMALL_4("[icache_out] sim_time: %d, pc: %x, inst: %x\n",
                                  front_sim_time, current_pc, fetch_group[i]);
            } else {
                fifo_in.predecode_type[i] = PREDECODE_NON_BRANCH;
                fifo_in.predecode_target_address[i] = 0;
            }
        }

        uint32_t mask = ~(ICACHE_LINE_SIZE - 1);
        fifo_in.seq_next_pc = fetch_pc + (FETCH_WIDTH * 4);
        if ((fifo_in.seq_next_pc & mask) != (fetch_pc & mask)) {
            fifo_in.seq_next_pc &= mask;
        }

        instruction_FIFO_comb_calc(&fifo_in, &fifo_out);
        inst_fifo_full_for_write = fifo_out.full;
    };

    if (!inst_fifo_full_for_write && icache_slot0_data_valid && !global_reset && !global_refetch) {
        front_stats.inst_fifo_write_slot0_cycles++;
        write_inst_fifo_entry(icache_out.fetch_group, icache_out.fetch_pc,
                              icache_out.page_fault_inst, icache_out.inst_valid);
    }

    bool can_write_slot1 = !inst_fifo_full_for_write && icache_slot1_data_valid
                           && !global_reset && !global_refetch;
    if (can_write_slot1) {
        front_stats.inst_fifo_write_slot1_cycles++;
        write_inst_fifo_entry(icache_out.fetch_group_2, icache_out.fetch_pc_2,
                              icache_out.page_fault_inst_2, icache_out.inst_valid_2);
    }
    
    // ========================================================================
    // 阶段 8: PTAB 写控制
    // ========================================================================
    bool ptab_can_write = bpu_out.PTAB_write_enable && !rd.ptab_full_latch_snapshot && !global_reset && !global_refetch;
    if (ptab_can_write) {
        front_stats.ptab_write_cycles++;
    }
    
    ptab_in.reset = global_reset;
    ptab_in.refetch = global_refetch;
    ptab_in.read_enable = false;  // 读已经做过了
    ptab_in.write_enable = ptab_can_write;
    
    if (ptab_can_write) {
        for (int i = 0; i < FETCH_WIDTH; i++) {
            ptab_in.predict_dir[i] = bpu_out.predict_dir[i];
            ptab_in.predict_base_pc[i] = bpu_out.predict_base_pc[i];
            ptab_in.alt_pred[i] = bpu_out.alt_pred[i];
            ptab_in.altpcpn[i] = bpu_out.altpcpn[i];
            ptab_in.pcpn[i] = bpu_out.pcpn[i];
            for (int j = 0; j < 4; j++) {
                ptab_in.tage_idx[i][j] = bpu_out.tage_idx[i][j];
                ptab_in.tage_tag[i][j] = bpu_out.tage_tag[i][j];
            }
        }
        ptab_in.predict_next_fetch_address = bpu_out.predict_next_fetch_address;
        // 未开启2-Ahead模式下mini_flush_req永远为0，保证正常写入
        ptab_in.need_mini_flush = bpu_out.mini_flush_req;
        
        DEBUG_LOG_SMALL_3("bpu_out.predict_next_fetch_address: %x\n",
                          bpu_out.predict_next_fetch_address);
        
        PTAB_comb_calc(&ptab_in, &ptab_out);
    }
    
    // ========================================================================
    // 阶段 9: Predecode Checker 逻辑
    // ========================================================================
    bool do_predecode_flush = false;
    uint32_t predecode_flush_address = 0;
    
    struct predecode_checker_out checker_out;
    memset(&checker_out, 0, sizeof(checker_out));
    
    if (predecode_can_run) {
        front_stats.checker_run_cycles++;
        // 验证 PC 匹配
        for (int i = 0; i < FETCH_WIDTH; i++) {
            if (saved_fifo_out.pc[i] != saved_ptab_out.predict_base_pc[i]) {
                printf("ERROR: fifo pc[%d]: %x != ptab pc[%d]: %x\n",
                       i, saved_fifo_out.pc[i], i, saved_ptab_out.predict_base_pc[i]);
                exit(1);
            }
        }
        
        // 运行 predecode checker
        struct predecode_checker_in checker_in;
        for (int i = 0; i < FETCH_WIDTH; i++) {
            checker_in.predict_dir[i] = saved_ptab_out.predict_dir[i];
            checker_in.predecode_type[i] = saved_fifo_out.predecode_type[i];
            checker_in.predecode_target_address[i] = saved_fifo_out.predecode_target_address[i];
        }
        checker_in.seq_next_pc = saved_fifo_out.seq_next_pc;
        checker_in.predict_next_fetch_address = saved_ptab_out.predict_next_fetch_address;
        
        predecode_checker_comb_calc(&checker_in, &checker_out);
        
        DEBUG_LOG_SMALL_4("[predecode on] seq_next_pc: %x, predict_next: %x\n",
                          saved_fifo_out.seq_next_pc, saved_ptab_out.predict_next_fetch_address);
        
        if (checker_out.predecode_flush_enable) {
            front_stats.checker_flush_cycles++;
            do_predecode_flush = true;
            predecode_flush_address = checker_out.predict_next_fetch_address_corrected;
        }
    }
    
    // ========================================================================
    // 阶段 10: front2back_FIFO 写控制
    // ========================================================================
    bool front2back_can_write = predecode_can_run  
                                && !rd.front2back_fifo_full_latch_snapshot
                                && !global_reset; 
    bool can_bypass_front2back_to_output =
        front2back_read_enable && rd.front2back_fifo_empty_latch_snapshot &&
        !saved_front2back_fifo_out.front2back_FIFO_valid && front2back_can_write;
    if (can_bypass_front2back_to_output) {
        front_stats.bypass_front2back_to_output_opportunity_cycles++;
        front_stats.bypass_front2back_to_output_hit_cycles++;
        use_front2back_output_bypass = true;
    }
    
    front2back_fifo_in.reset = global_reset;
    front2back_fifo_in.refetch = in->refetch;
    front2back_fifo_in.read_enable = false;  // 读已经做过了
    front2back_fifo_in.write_enable = front2back_can_write && !can_bypass_front2back_to_output;
    if (front2back_fifo_in.write_enable) {
        front_stats.front2back_write_cycles++;
    }
    
    if (front2back_can_write) {
        constexpr uint32_t kPcpnMask = (1u << pcpn_t_BITS) - 1u;
        constexpr uint32_t kTageIdxMask = (1u << tage_idx_t_BITS) - 1u;
        constexpr uint32_t kTageTagMask = (1u << tage_tag_t_BITS) - 1u;
        for (int i = 0; i < FETCH_WIDTH; i++) {
            front2back_fifo_in.fetch_group[i] = saved_fifo_out.instructions[i];
            front2back_fifo_in.page_fault_inst[i] = saved_fifo_out.page_fault_inst[i];
            front2back_fifo_in.inst_valid[i] = saved_fifo_out.inst_valid[i];
            front2back_fifo_in.predict_dir_corrected[i] = checker_out.predict_dir_corrected[i];
            front2back_fifo_in.predict_base_pc[i] = saved_ptab_out.predict_base_pc[i];
            front2back_fifo_in.alt_pred[i] = saved_ptab_out.alt_pred[i];
            front2back_fifo_in.altpcpn[i] =
                static_cast<uint8_t>(saved_ptab_out.altpcpn[i] & kPcpnMask);
            front2back_fifo_in.pcpn[i] =
                static_cast<uint8_t>(saved_ptab_out.pcpn[i] & kPcpnMask);
            for (int j = 0; j < 4; j++) {
                front2back_fifo_in.tage_idx[i][j] =
                    saved_ptab_out.tage_idx[i][j] & kTageIdxMask;
                front2back_fifo_in.tage_tag[i][j] =
                    saved_ptab_out.tage_tag[i][j] & kTageTagMask;
            }
            if (use_front2back_output_bypass) {
                bypass_front2back_fifo_out.fetch_group[i] = saved_fifo_out.instructions[i];
                bypass_front2back_fifo_out.page_fault_inst[i] = saved_fifo_out.page_fault_inst[i];
                bypass_front2back_fifo_out.inst_valid[i] = saved_fifo_out.inst_valid[i];
                bypass_front2back_fifo_out.predict_dir_corrected[i] =
                    checker_out.predict_dir_corrected[i];
                bypass_front2back_fifo_out.predict_base_pc[i] =
                    saved_ptab_out.predict_base_pc[i];
                bypass_front2back_fifo_out.alt_pred[i] = saved_ptab_out.alt_pred[i];
                bypass_front2back_fifo_out.altpcpn[i] = saved_ptab_out.altpcpn[i];
                bypass_front2back_fifo_out.pcpn[i] = saved_ptab_out.pcpn[i];
                for (int j = 0; j < 4; j++) {
                    bypass_front2back_fifo_out.tage_idx[i][j] =
                        saved_ptab_out.tage_idx[i][j];
                    bypass_front2back_fifo_out.tage_tag[i][j] =
                        saved_ptab_out.tage_tag[i][j];
                }
            }
        }
        front2back_fifo_in.predict_next_fetch_address_corrected = 
            checker_out.predict_next_fetch_address_corrected;
        if (use_front2back_output_bypass) {
            bypass_front2back_fifo_out.predict_next_fetch_address_corrected =
                checker_out.predict_next_fetch_address_corrected;
            bypass_front2back_fifo_out.front2back_FIFO_valid = true;
        }
        
        if (front2back_fifo_in.write_enable) {
            front2back_FIFO_comb_calc(&front2back_fifo_in, &front2back_fifo_out);
        }
    }
    
    // ========================================================================
    // 阶段 11: 处理 predecode flush
    // ========================================================================
    front_state_req.valid = true;
    if (do_predecode_flush) {
        // Predecode correction only needs a narrow frontend replay, not a full
        // icache reset. Use a comb-only refetch to flush the in-flight lookup.
        icache_in.reset = false;
        icache_in.refetch = true;
        icache_in.icache_read_valid = false;
        icache_in.fetch_address = predecode_flush_address;
        icache_in.run_comb_only = true;
        icache_top(&icache_in, &icache_out);
        
        front_state_req.next_predecode_refetch = true;
        front_state_req.next_predecode_refetch_address = predecode_flush_address;
        
        DEBUG_LOG_SMALL("[front_top] predecode flush to: %x\n", predecode_flush_address);
    } else {
        front_state_req.next_predecode_refetch = false;
        front_state_req.next_predecode_refetch_address = 0;
    }
    
    // ========================================================================
    // 阶段 12: 获取最终 FIFO 状态并更新锁存值
    // ========================================================================
    // 重新获取各 FIFO 的状态（不进行读写，只获取状态）
    fetch_addr_fifo_in.reset = false;
    fetch_addr_fifo_in.refetch = false;
    fetch_addr_fifo_in.read_enable = false;
    fetch_addr_fifo_in.write_enable = false;
    fetch_address_FIFO_comb_calc(&fetch_addr_fifo_in, &fetch_addr_fifo_out);
    
    fifo_in.reset = false;
    fifo_in.refetch = false;
    fifo_in.read_enable = false;
    fifo_in.write_enable = false;
    instruction_FIFO_comb_calc(&fifo_in, &fifo_out);
    
    ptab_in.reset = false;
    ptab_in.refetch = false;
    ptab_in.read_enable = false;
    ptab_in.write_enable = false;
    PTAB_comb_calc(&ptab_in, &ptab_out);
    
    front2back_fifo_in.reset = false;
    front2back_fifo_in.refetch = false;
    front2back_fifo_in.read_enable = false;
    front2back_fifo_in.write_enable = false;
    front2back_FIFO_comb_calc(&front2back_fifo_in, &front2back_fifo_out);
    
    // 记录锁存值，延后到 front_seq_write 提交
    front_state_req.next_fetch_addr_fifo_full = fetch_addr_fifo_out.full;
    front_state_req.next_fetch_addr_fifo_empty = fetch_addr_fifo_out.empty;
    front_state_req.next_fifo_full = fifo_out.full;
    front_state_req.next_fifo_empty = fifo_out.empty;
    front_state_req.next_ptab_full = ptab_out.full;
    front_state_req.next_ptab_empty = ptab_out.empty;
    front_state_req.next_front2back_fifo_full = front2back_fifo_out.full;
    front_state_req.next_front2back_fifo_empty = front2back_fifo_out.empty;
    
    // ========================================================================
    // 阶段 13: 设置输出
    // ========================================================================
    // 注意：输出来自 front2back_FIFO 的读结果（阶段3.4的结果）
    // front2back_fifo_in.read_enable = front2back_read_enable;
    // front2back_FIFO_top(&front2back_fifo_in, &front2back_fifo_out);
    
    const struct front2back_FIFO_out *out_src = &saved_front2back_fifo_out;
    if (!saved_front2back_fifo_out.front2back_FIFO_valid && use_front2back_output_bypass) {
        out_src = &bypass_front2back_fifo_out;
    }

    out->FIFO_valid = out_src->front2back_FIFO_valid;
    for (int i = 0; i < FETCH_WIDTH; i++) {
        out->instructions[i] = out_src->fetch_group[i];
        out->page_fault_inst[i] = out_src->page_fault_inst[i];
        out->predict_dir[i] = out_src->predict_dir_corrected[i];
        out->pc[i] = out_src->predict_base_pc[i];
        out->alt_pred[i] = out_src->alt_pred[i];
        out->altpcpn[i] = out_src->altpcpn[i];
        out->pcpn[i] = out_src->pcpn[i];
        for (int j = 0; j < 4; j++) {
            out->tage_idx[i][j] = out_src->tage_idx[i][j];
            out->tage_tag[i][j] = out_src->tage_tag[i][j];
        }
        out->inst_valid[i] = out_src->inst_valid[i];
        
        if (out->inst_valid[i]) {
            DEBUG_LOG_SMALL_4("[front_top] sim_time: %d, out->pc[%d]: %x, inst: %x\n",
                              front_sim_time, i, out->pc[i], out->instructions[i]);
        }
    }
    out->predict_next_fetch_address = out_src->predict_next_fetch_address_corrected;
    
    if (out->FIFO_valid) {
        front_stats.front2back_valid_out_cycles++;
        DEBUG_LOG_SMALL("[front_top] sim_time: %d, out->pc[0]: %x\n",
                        front_sim_time, out->pc[0]);
    }

    front_state_req.next_front_sim_time = front_sim_time;
    front_state_req.next_front_stats = front_stats;
}

namespace {

struct FrontUpdateRequest {
  struct front_top_out out_regs;
  PendingBpuSeqTxn bpu_seq_txn;
  PendingFrontState front_state;
};

void front_seq_read(const struct front_top_in &inp, FrontReadData &rd) {
  rd.predecode_refetch_snapshot = predecode_refetch;
  rd.predecode_refetch_address_snapshot = predecode_refetch_address;
  rd.front_sim_time_snapshot = front_sim_time;
  rd.front_stats_snapshot = front_stats;
  rd.fetch_addr_fifo_full_latch_snapshot = fetch_addr_fifo_full_latch;
  rd.fetch_addr_fifo_empty_latch_snapshot = fetch_addr_fifo_empty_latch;
  rd.fifo_full_latch_snapshot = fifo_full_latch;
  rd.fifo_empty_latch_snapshot = fifo_empty_latch;
  rd.ptab_full_latch_snapshot = ptab_full_latch;
  rd.ptab_empty_latch_snapshot = ptab_empty_latch;
  rd.front2back_fifo_full_latch_snapshot = front2back_fifo_full_latch;
  rd.front2back_fifo_empty_latch_snapshot = front2back_fifo_empty_latch;

  fetch_address_FIFO_in fetch_addr_in;
  fetch_address_FIFO_out fetch_addr_out;
  instruction_FIFO_in fifo_in;
  instruction_FIFO_out fifo_out;
  PTAB_in ptab_in;
  PTAB_out ptab_out;
  front2back_FIFO_in front2back_in;
  front2back_FIFO_out front2back_out;
  predecode_checker_in checker_in;
  predecode_checker_out checker_out;
  icache_in icache_inp;
  icache_out icache_outp;
  std::memset(&fetch_addr_in, 0, sizeof(fetch_addr_in));
  std::memset(&fetch_addr_out, 0, sizeof(fetch_addr_out));
  std::memset(&fifo_in, 0, sizeof(fifo_in));
  std::memset(&fifo_out, 0, sizeof(fifo_out));
  std::memset(&ptab_in, 0, sizeof(ptab_in));
  std::memset(&ptab_out, 0, sizeof(ptab_out));
  std::memset(&front2back_in, 0, sizeof(front2back_in));
  std::memset(&front2back_out, 0, sizeof(front2back_out));
  std::memset(&checker_in, 0, sizeof(checker_in));
  std::memset(&checker_out, 0, sizeof(checker_out));
  std::memset(&icache_inp, 0, sizeof(icache_inp));
  std::memset(&icache_outp, 0, sizeof(icache_outp));

  fetch_address_FIFO_seq_read(&fetch_addr_in, &fetch_addr_out);
  instruction_FIFO_seq_read(&fifo_in, &fifo_out);
  PTAB_seq_read(&ptab_in, &ptab_out);
  front2back_FIFO_seq_read(&front2back_in, &front2back_out);
  predecode_checker_seq_read(&checker_in, &checker_out);
  icache_seq_read(&icache_inp, &icache_outp);
  (void)inp;
}

void front_comb_calc(const struct front_top_in &inp, const FrontReadData &rd,
                     struct front_top_out &out, FrontUpdateRequest &req) {
  std::memset(&out, 0, sizeof(struct front_top_out));
  front_comb_calc_impl(rd, const_cast<struct front_top_in *>(&inp), &out,
                       req.bpu_seq_txn, req.front_state);
  req.out_regs = out;
}

void front_seq_write(const struct front_top_in &inp, const FrontUpdateRequest &req,
                     bool reset) {
  (void)inp;
  (void)reset;
  if (req.bpu_seq_txn.valid) {
    bpu_instance.bpu_seq(req.bpu_seq_txn.inp, req.bpu_seq_txn.req,
                         req.bpu_seq_txn.reset);
  }
  fetch_address_FIFO_seq_write();
  instruction_FIFO_seq_write();
  PTAB_seq_write();
  front2back_FIFO_seq_write();
  predecode_checker_seq_write();
  icache_seq_write();

  if (req.front_state.valid) {
    front_sim_time = req.front_state.next_front_sim_time;
    front_stats = req.front_state.next_front_stats;
    predecode_refetch = req.front_state.next_predecode_refetch;
    predecode_refetch_address = req.front_state.next_predecode_refetch_address;
    fetch_addr_fifo_full_latch = req.front_state.next_fetch_addr_fifo_full;
    fetch_addr_fifo_empty_latch = req.front_state.next_fetch_addr_fifo_empty;
    fifo_full_latch = req.front_state.next_fifo_full;
    fifo_empty_latch = req.front_state.next_fifo_empty;
    ptab_full_latch = req.front_state.next_ptab_full;
    ptab_empty_latch = req.front_state.next_ptab_empty;
    front2back_fifo_full_latch = req.front_state.next_front2back_fifo_full;
    front2back_fifo_empty_latch = req.front_state.next_front2back_fifo_empty;
  }
}

} // namespace

void front_top(struct front_top_in *in, struct front_top_out *out) {
  assert(in);
  assert(out);

  FrontReadData rd;
  FrontUpdateRequest req;
  front_seq_read(*in, rd);
  front_comb_calc(*in, rd, *out, req);
  front_seq_write(*in, req, in->reset);
}
