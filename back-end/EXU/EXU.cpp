#include <EXU.h>
#include <config.h>

void alu(Inst_info *inst, FU &fu);
void bru(Inst_info *inst, FU &fu);
void ldu_comb(Inst_info *inst, FU &fu);
void ldu_seq(Inst_info *inst, FU &fu);
void stu_comb(Inst_info *inst, FU &fu);

FU_TYPE fu_config[ISSUE_WAY] = {FU_ALU, FU_ALU, FU_ALU, FU_ALU,
                                FU_LDU, FU_STU, FU_BRU};

void (*fu_comb[FU_NUM])(Inst_info *, FU &) = {alu, ldu_comb, stu_comb, bru};
void (*fu_seq[FU_NUM])(Inst_info *, FU &) = {nullptr, ldu_seq, nullptr,
                                             nullptr};

void EXU::init() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    fu[i].type = fu_config[i];
    fu[i].comb = fu_comb[fu[i].type];
    fu[i].seq = fu_seq[fu[i].type];
  }
}

void EXU::comb() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    io.exe2prf->entry[i].valid = false;
    io.exe2prf->entry[i].inst = inst_r[i].inst;
    if (inst_r[i].valid && inst_r[i].inst.op != LOAD && inst_r[i].inst.op != STORE) {
      fu[i].comb(&io.exe2prf->entry[i].inst, fu[i]);
      if (fu[i].complete) {
        io.exe2prf->entry[i].valid = true;
      } else {
        io.exe2prf->entry[i].valid = false;
      }
      io.exe2prf->ready[i] =
        !inst_r[i].valid || io.exe2prf->entry[i].valid && io.prf2exe->ready[i];
      io.exe2iss->ready[i] =
        !inst_r[i].valid || io.exe2prf->entry[i].valid && io.prf2exe->ready[i];
    }
    else if (inst_r[i].valid && (inst_r[i].inst.op == LOAD || inst_r[i].inst.op == STORE)) {
      uint32_t addr = io.exe2prf->entry[i].inst->src1_rdata + io.exe2prf->entry[i].inst->imm;
      lsu_req->m.valid_out     = true;
      lsu_req->m.op_out        = inst_r[i].inst.op == LOAD ? op_t::LD : op_t::ST;
      lsu_req->m.vtag_out      = addr >> 12;
      lsu_req->m.index_out     = (addr << 20) >> 24;
      lsu_req->m.word_out      = (addr << 28) >> 30;
      lsu_req->m.offset_out    = addr & 0x3;
      lsu_req->m.lsq_entry_out = io.exe2prf->entry[i].inst.lsu_idx;  
      for (int byte = 0; byte < 4; byte++)
        lsu_req->m.wdata_b4_sft_out[byte] = ((io.exe2prf->entry[i].inst.src2_rdata) >> (8 * i)) & (0xFF);
      mem->comb();
      if (mem->bcast_bus->valid_out) {
        io.exe2prf->entry[i].valid  = true;
        io.exe2prf->entry[i].result = mem->bcast_bus->data_out[3] << 24 | mem->bcast_bus->data_out[2] << 16 | mem->bcast_bus->data_out[1] << 8 | mem->bcast_bus->data_out[0]; 
      }
      io.exe2prf->ready[i] = lsu_req->m.ready_in;
      io.exe2iss->ready[i] = lsu_req->m.ready_in;
    }
  }
}

void EXU::seq() {
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (io.prf2exe->iss_entry[i].valid && io.exe2prf->ready[i]) {
      inst_r[i] = io.prf2exe->iss_entry[i];
    } else if (inst_r[i].valid && fu[i].complete) {
      inst_r[i].valid = false;
    }

    if (fu[i].seq)
      fu[i].seq(&io.exe2prf->entry[i].inst, fu[i]);
  }

  if (io.id_bc->mispred) {
    for (int i = 0; i < ISSUE_WAY; i++) {
      if (inst_r[i].valid && (io.id_bc->br_mask & (1 << inst_r[i].inst.tag))) {
        inst_r[i].valid = false;
      }
    }
  }
}
