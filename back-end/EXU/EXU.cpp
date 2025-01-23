#include <EXU.h>
#include <config.h>
#include <vector>

void alu(Inst_info *inst);
void bru(Inst_info *inst);

vector<FU_TYPE> fu_config1 = {FU_ALU};
vector<FU_TYPE> fu_config2 = {FU_ALU};
vector<FU_TYPE> fu_config3 = {FU_ALU};
vector<FU_TYPE> fu_config4 = {FU_ALU};
vector<FU_TYPE> fu_config5 = {FU_AGU};
vector<FU_TYPE> fu_config6 = {FU_CSR};
vector<vector<FU_TYPE>> fu_config = {fu_config1, fu_config2, fu_config3,
                                     fu_config4, fu_config5, fu_config6};

void (*fu_func[FU_NUM])(Inst_info *) = {alu, bru};

void EXU::init() {
  for (auto config : fu_config) {
    vector<FU> a(config.size());
    for (int i = 0; i < config.size(); i++) {
      a[i].type = config[i];
      /*a[i].fu_exec = fu_func[a[i].type];*/
      a[i].fu_exec = alu;
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
        fu[i][j].fu_exec(&io.exe2prf->entry[i].inst);
        if (fu[i][j].latency == 0) {
          io.exe2prf->entry[i].valid = true;
        }
      }

      io.exe2prf->ready[i][j] =
          !inst_r[i][j].valid ||
          io.exe2prf->entry[i].valid && io.prf2exe->ready[i];
    }
  }
}

void EXU::seq() {
  for (int i = 0; i < EXU_NUM; i++) {
    for (int j = 0; j < fu[i].size(); j++) {
      if (io.prf2exe->iss_pack[i][j].valid && io.exe2prf->ready[i][j]) {
        inst_r[i][j] = io.prf2exe->iss_pack[i][j];
      } else if (inst_r[i][j].valid && fu[i][j].latency == 0) {
        inst_r[i][j].valid = false;
      }
    }
  }
}
