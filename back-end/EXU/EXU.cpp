#include <EXU.h>
#include <config.h>
#include <vector>

void alu(Inst_info *inst, FU &fu);
void ldu_comb(Inst_info *inst, FU &fu);
void ldu_seq(Inst_info *inst, FU &fu);
void stu_comb(Inst_info *inst, FU &fu);

vector<FU_TYPE> fu_config1 = {FU_ALU};
vector<FU_TYPE> fu_config2 = {FU_ALU};
vector<FU_TYPE> fu_config3 = {FU_ALU};
vector<FU_TYPE> fu_config4 = {FU_ALU};
vector<FU_TYPE> fu_config5 = {FU_LDU};
vector<FU_TYPE> fu_config6 = {FU_STU};
vector<vector<FU_TYPE>> fu_config = {fu_config1, fu_config2, fu_config3,
                                     fu_config4, fu_config5, fu_config6};

void (*fu_comb[FU_NUM])(Inst_info *, FU &) = {alu, ldu_comb, stu_comb};
void (*fu_seq[FU_NUM])(Inst_info *, FU &) = {nullptr, nullptr};

void EXU::init() {
  for (auto config : fu_config) {
    vector<FU> a(config.size());
    for (int i = 0; i < config.size(); i++) {
      a[i].type = config[i];
      a[i].comb = fu_comb[a[i].type];
      a[i].seq = fu_seq[a[i].type];
    }
    fu.push_back(a);
  }

  for (auto config : fu_config) {
    vector<Inst_entry> a(config.size());
    inst_r.push_back(a);
  }
}

void EXU::comb() {
  for (int i = 0; i < EXU_NUM; i++) {
    for (int j = 0; j < fu[i].size(); j++) {
      io.exe2prf->entry[i].valid = false;
      io.exe2prf->entry[i].inst = inst_r[i][j].inst;
      if (inst_r[i][j].valid) {
        fu[i][j].comb(&io.exe2prf->entry[i].inst, fu[i][j]);
        if (fu[i][j].complete) {
          io.exe2prf->entry[i].valid = true;
        } else {
          io.exe2prf->entry[i].valid = false;
        }
      }

      io.exe2prf->ready[i][j] =
          !inst_r[i][j].valid ||
          io.exe2prf->entry[i].valid && io.prf2exe->ready[i];
    }
  }

  // store
  if (inst_r[5][0].valid) {
    io.exe2stq->entry = inst_r[5][0];
  } else {
    io.exe2stq->entry.valid = false;
  }
}

void EXU::seq() {
  for (int i = 0; i < EXU_NUM; i++) {
    for (int j = 0; j < fu[i].size(); j++) {
      if (fu[i][j].seq)
        fu[i][j].seq(&io.exe2prf->entry[i].inst, fu[i][j]);

      if (io.prf2exe->iss_pack[i][j].valid && io.exe2prf->ready[i][j]) {
        inst_r[i][j] = io.prf2exe->iss_pack[i][j];
      } else if (inst_r[i][j].valid && fu[i][j].complete) {
        inst_r[i][j].valid = false;
      }
    }
  }
}
