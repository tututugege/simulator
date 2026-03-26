#ifndef TYPE_PREDICTOR_H
#define TYPE_PREDICTOR_H

#include "../../frontend.h"
#include "../BPU_configs.h"

#include <cstring>

class TypePredictor {
public:
  struct InputPayload {
    wire1_t pred_valid[FETCH_WIDTH];
    pc_t pred_pc[FETCH_WIDTH];
    wire1_t upd_valid[COMMIT_WIDTH];
    pc_t upd_pc[COMMIT_WIDTH];
    br_type_t upd_br_type[COMMIT_WIDTH];
  };

  struct Entry {
    wire1_t valid;
    type_pred_tag_t tag;
    br_type_t type;
    type_pred_conf_t conf;
    type_pred_age_t age;
  };

  struct OutputPayload {
    br_type_t pred_type[FETCH_WIDTH];
    wire1_t pred_hit[FETCH_WIDTH];
    wire1_t pred_confident[FETCH_WIDTH];
  };

  struct ReadData {
    Entry pred_entries[FETCH_WIDTH][TYPE_PRED_WAY_NUM];
    Entry upd_entries[COMMIT_WIDTH][TYPE_PRED_WAY_NUM];
  };

  struct CombResult {
    wire1_t write_en[COMMIT_WIDTH];
    bpu_bank_sel_t write_bank[COMMIT_WIDTH];
    type_pred_set_idx_t write_set[COMMIT_WIDTH];
    type_pred_way_t write_way[COMMIT_WIDTH];
    Entry write_entry[COMMIT_WIDTH];
  };

  struct TypePredBankSelCombIn {
    pc_t pc;
  };

  struct TypePredBankSelCombOut {
    bpu_bank_sel_t bank_sel;
  };

  struct TypePredBankPcCombIn {
    pc_t pc;
  };

  struct TypePredBankPcCombOut {
    pc_t bank_pc;
  };

  struct TypePredReqCombIn {
    pc_t bank_pc;
  };

  struct TypePredReqCombOut {
    type_pred_set_idx_t set_idx;
    type_pred_tag_t tag;
  };

  struct PredReadReqCombOut {
    wire1_t read_enable[FETCH_WIDTH];
    bpu_bank_sel_t bank[FETCH_WIDTH];
    type_pred_set_idx_t set_idx[FETCH_WIDTH];
    type_pred_tag_t tag[FETCH_WIDTH];
  };

  struct UpdReadReqCombOut {
    wire1_t read_enable[COMMIT_WIDTH];
    bpu_bank_sel_t bank[COMMIT_WIDTH];
    type_pred_set_idx_t set_idx[COMMIT_WIDTH];
    type_pred_tag_t tag[COMMIT_WIDTH];
  };

  struct PreReadCombOut {
    PredReadReqCombOut pred_req;
    UpdReadReqCombOut upd_req;
  };

  struct TypePredCombIn {
    InputPayload inp;
    PreReadCombOut pre_read;
    ReadData rd;
  };

  struct TypePredCombOut {
    OutputPayload out_regs;
    CombResult req;
  };

  struct TypePredHitCombIn {
    Entry entries[TYPE_PRED_WAY_NUM];
    type_pred_tag_t tag;
  };

  struct TypePredHitCombOut {
    wire1_t hit;
    type_pred_way_t hit_way;
  };

  struct TypePredSelectCombIn {
    TypePredHitCombOut hit_info;
    Entry entries[TYPE_PRED_WAY_NUM];
  };

  struct TypePredSelectCombOut {
    br_type_t pred_type;
    wire1_t pred_confident;
  };

  struct TypePredVictimCombIn {
    Entry entries[TYPE_PRED_WAY_NUM];
  };

  struct TypePredVictimCombOut {
    type_pred_way_t victim_way;
  };

  struct TypePredUpdateCombIn {
    Entry entries[TYPE_PRED_WAY_NUM];
    type_pred_tag_t tag;
    br_type_t actual_type;
  };

  struct TypePredUpdateCombOut {
    wire1_t write_enable;
    type_pred_way_t write_way;
    Entry write_entry;
  };

private:
  Entry table[BPU_BANK_NUM][TYPE_PRED_SET_NUM][TYPE_PRED_WAY_NUM];

  static type_pred_conf_t conf_inc(type_pred_conf_t value) {
    if (value >= static_cast<type_pred_conf_t>(TYPE_PRED_CONF_MAX)) {
      return static_cast<type_pred_conf_t>(TYPE_PRED_CONF_MAX);
    }
    return static_cast<type_pred_conf_t>(value + 1);
  }

  static type_pred_age_t age_inc(type_pred_age_t value) {
    if (value >= static_cast<type_pred_age_t>(TYPE_PRED_AGE_MAX)) {
      return static_cast<type_pred_age_t>(TYPE_PRED_AGE_MAX);
    }
    return static_cast<type_pred_age_t>(value + 1);
  }

  static void type_pred_bank_sel_comb(const TypePredBankSelCombIn &in,
                                      TypePredBankSelCombOut &out) {
    out = TypePredBankSelCombOut{};
    out.bank_sel = static_cast<bpu_bank_sel_t>((in.pc >> 2) % BPU_BANK_NUM);
  }

  static void type_pred_bank_pc_comb(const TypePredBankPcCombIn &in,
                                     TypePredBankPcCombOut &out) {
    out = TypePredBankPcCombOut{};
    if ((BPU_BANK_NUM & (BPU_BANK_NUM - 1)) != 0) {
      uint32_t bank_pc = in.pc >> 2;
      bank_pc = bank_pc / BPU_BANK_NUM;
      bank_pc = bank_pc << 2;
      out.bank_pc = bank_pc;
      return;
    }

    uint32_t n = BPU_BANK_NUM;
    int highest_bit_pos = 0;
    while (n > 1) {
      n >>= 1;
      highest_bit_pos++;
    }
    out.bank_pc = in.pc >> highest_bit_pos;
  }

  static void type_pred_req_comb(const TypePredReqCombIn &in, TypePredReqCombOut &out) {
    out = TypePredReqCombOut{};
    const uint32_t bank_pc_word = in.bank_pc >> 2;
    out.set_idx = static_cast<type_pred_set_idx_t>(bank_pc_word & TYPE_PRED_SET_MASK);
    const uint32_t raw_tag =
        (bank_pc_word >> ceil_log2_u32(TYPE_PRED_SET_NUM)) & TYPE_PRED_TAG_MASK;
    out.tag = static_cast<type_pred_tag_t>(raw_tag);
  }

  static void pred_read_req_comb(const InputPayload &in, PredReadReqCombOut &out) {
    std::memset(&out, 0, sizeof(out));
    for (int i = 0; i < FETCH_WIDTH; ++i) {
      if (!in.pred_valid[i]) {
        continue;
      }
      TypePredBankSelCombOut bank_sel_out{};
      TypePredBankPcCombOut bank_pc_out{};
      TypePredReqCombOut req{};
      type_pred_bank_sel_comb(TypePredBankSelCombIn{in.pred_pc[i]}, bank_sel_out);
      type_pred_bank_pc_comb(TypePredBankPcCombIn{in.pred_pc[i]}, bank_pc_out);
      type_pred_req_comb(TypePredReqCombIn{bank_pc_out.bank_pc}, req);
      out.read_enable[i] = true;
      out.bank[i] = bank_sel_out.bank_sel;
      out.set_idx[i] = req.set_idx;
      out.tag[i] = req.tag;
    }
  }

  static void upd_read_req_comb(const InputPayload &in, UpdReadReqCombOut &out) {
    std::memset(&out, 0, sizeof(out));
    for (int i = 0; i < COMMIT_WIDTH; ++i) {
      if (!in.upd_valid[i]) {
        continue;
      }
      TypePredBankSelCombOut bank_sel_out{};
      TypePredBankPcCombOut bank_pc_out{};
      TypePredReqCombOut req{};
      type_pred_bank_sel_comb(TypePredBankSelCombIn{in.upd_pc[i]}, bank_sel_out);
      type_pred_bank_pc_comb(TypePredBankPcCombIn{in.upd_pc[i]}, bank_pc_out);
      type_pred_req_comb(TypePredReqCombIn{bank_pc_out.bank_pc}, req);
      out.read_enable[i] = true;
      out.bank[i] = bank_sel_out.bank_sel;
      out.set_idx[i] = req.set_idx;
      out.tag[i] = req.tag;
    }
  }

  static void hit_comb(const TypePredHitCombIn &in, TypePredHitCombOut &out) {
    out = TypePredHitCombOut{};
    for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
      if (in.entries[way].valid && in.entries[way].tag == in.tag) {
        out.hit = true;
        out.hit_way = static_cast<type_pred_way_t>(way);
        return;
      }
    }
  }

  static void select_comb(const TypePredSelectCombIn &in, TypePredSelectCombOut &out) {
    out = TypePredSelectCombOut{};
    out.pred_type = BR_NONCTL;
    if (!in.hit_info.hit) {
      return;
    }
    const Entry &entry = in.entries[in.hit_info.hit_way];
    out.pred_type = entry.type;
    out.pred_confident = (entry.conf >= static_cast<type_pred_conf_t>(TYPE_PRED_CONF_MAX));
  }

  static void victim_comb(const TypePredVictimCombIn &in, TypePredVictimCombOut &out) {
    out = TypePredVictimCombOut{};
    for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
      if (!in.entries[way].valid) {
        out.victim_way = static_cast<type_pred_way_t>(way);
        return;
      }
    }

    type_pred_age_t min_age = in.entries[0].age;
    out.victim_way = 0;
    for (int way = 1; way < TYPE_PRED_WAY_NUM; ++way) {
      if (in.entries[way].age < min_age) {
        min_age = in.entries[way].age;
        out.victim_way = static_cast<type_pred_way_t>(way);
      }
    }
  }

  void type_pred_update_comb(const TypePredUpdateCombIn &in,
                             TypePredUpdateCombOut &out) const {
    out = TypePredUpdateCombOut{};

    TypePredHitCombOut hit_info{};
    TypePredHitCombIn hit_in{};
    for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
      hit_in.entries[way] = in.entries[way];
    }
    hit_in.tag = in.tag;
    hit_comb(hit_in, hit_info);

    TypePredVictimCombOut victim_out{};
    TypePredVictimCombIn victim_in{};
    for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
      victim_in.entries[way] = in.entries[way];
    }
    victim_comb(victim_in, victim_out);

    if (in.actual_type == BR_NONCTL) {
      if (!hit_info.hit) {
        return;
      }
      out.write_enable = true;
      out.write_way = hit_info.hit_way;
      Entry next_entry{};
      out.write_entry = next_entry;
      return;
    }

    out.write_enable = true;
    out.write_way = hit_info.hit ? hit_info.hit_way : victim_out.victim_way;
    Entry next_entry = hit_info.hit ? in.entries[out.write_way] : Entry{};
    next_entry.valid = true;
    next_entry.tag = in.tag;

    if (hit_info.hit && next_entry.type == in.actual_type) {
      next_entry.conf = conf_inc(next_entry.conf);
      next_entry.age = age_inc(next_entry.age);
    } else {
      next_entry.type = in.actual_type;
      next_entry.conf = static_cast<type_pred_conf_t>(1);
      next_entry.age = static_cast<type_pred_age_t>(1);
    }

    out.write_entry = next_entry;
  }

  void pre_read_comb(const InputPayload &in, PreReadCombOut &out) const {
    std::memset(&out, 0, sizeof(out));
    pred_read_req_comb(in, out.pred_req);
    upd_read_req_comb(in, out.upd_req);
  }

  void data_seq_read(const PreReadCombOut &in, ReadData &rd) const {
    std::memset(&rd, 0, sizeof(rd));
    for (int i = 0; i < FETCH_WIDTH; ++i) {
      if (!in.pred_req.read_enable[i]) {
        continue;
      }
      for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
        rd.pred_entries[i][way] = table[in.pred_req.bank[i]][in.pred_req.set_idx[i]][way];
      }
    }
    for (int i = 0; i < COMMIT_WIDTH; ++i) {
      if (!in.upd_req.read_enable[i]) {
        continue;
      }
      for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
        rd.upd_entries[i][way] = table[in.upd_req.bank[i]][in.upd_req.set_idx[i]][way];
      }
    }
  }

  void type_pred_comb(const TypePredCombIn &input, TypePredCombOut &output) const {
    std::memset(&output, 0, sizeof(output));
    const InputPayload &in = input.inp;
    const PreReadCombOut &pre_read = input.pre_read;
    const ReadData &rd = input.rd;
    OutputPayload &out = output.out_regs;
    CombResult &req = output.req;

    for (int i = 0; i < FETCH_WIDTH; ++i) {
      out.pred_type[i] = BR_NONCTL;
      if (!pre_read.pred_req.read_enable[i]) {
        continue;
      }
      TypePredHitCombIn hit_in{};
      for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
        hit_in.entries[way] = rd.pred_entries[i][way];
      }
      hit_in.tag = pre_read.pred_req.tag[i];
      TypePredHitCombOut hit_out{};
      hit_comb(hit_in, hit_out);
      TypePredSelectCombIn sel_in{};
      sel_in.hit_info = hit_out;
      for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
        sel_in.entries[way] = rd.pred_entries[i][way];
      }
      TypePredSelectCombOut sel_out{};
      select_comb(sel_in, sel_out);
      out.pred_hit[i] = hit_out.hit;
      out.pred_type[i] = sel_out.pred_type;
      out.pred_confident[i] = sel_out.pred_confident;
    }

    for (int i = 0; i < COMMIT_WIDTH; ++i) {
      if (!pre_read.upd_req.read_enable[i]) {
        continue;
      }
      TypePredUpdateCombIn upd_in{};
      for (int way = 0; way < TYPE_PRED_WAY_NUM; ++way) {
        upd_in.entries[way] = rd.upd_entries[i][way];
      }
      upd_in.tag = pre_read.upd_req.tag[i];
      upd_in.actual_type = in.upd_br_type[i];
      TypePredUpdateCombOut upd_out{};
      type_pred_update_comb(upd_in, upd_out);
      req.write_en[i] = upd_out.write_enable;
      req.write_bank[i] = pre_read.upd_req.bank[i];
      req.write_set[i] = pre_read.upd_req.set_idx[i];
      req.write_way[i] = upd_out.write_way;
      req.write_entry[i] = upd_out.write_entry;
    }
  }

public:
  TypePredictor() { reset(); }

  void reset() { std::memset(table, 0, sizeof(table)); }

  void type_pred_seq_read(const InputPayload &in, ReadData &rd) const {
    (void)in;
    std::memset(&rd, 0, sizeof(rd));
  }

  void type_pred_comb_calc(const InputPayload &in, ReadData &rd,
                           OutputPayload &out, CombResult &req) const {
    PreReadCombOut pre_read{};
    pre_read_comb(in, pre_read);
    data_seq_read(pre_read, rd);
    TypePredCombOut comb_out{};
    type_pred_comb(TypePredCombIn{in, pre_read, rd}, comb_out);
    out = comb_out.out_regs;
    req = comb_out.req;
  }

  void type_pred_seq_write(const InputPayload &in, const CombResult &req, bool reset_req) {
    (void)in;
    if (reset_req) {
      reset();
      return;
    }
    for (int i = 0; i < COMMIT_WIDTH; ++i) {
      if (!req.write_en[i]) {
        continue;
      }
      table[req.write_bank[i]][req.write_set[i]][req.write_way[i]] = req.write_entry[i];
    }
  }
};

#endif
