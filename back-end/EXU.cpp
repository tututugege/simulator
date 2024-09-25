#include <EXU.h>
#include <config.h>
#include <cstdint>
#include <cstdio>
#include <cvt.h>

void ALU::cycle() {

  switch (in.op) {
  case ADD:
  case LUI:
  case JAL:     // jal
  case JALR:    // jalr
  case AUIPC: { // auipc
    out.res = in.src1 + in.src2;
    break;
  }
  case SUB: {
    out.res = in.src1 - in.src2;
    break;
  }
  case SLL:
    out.res = in.src1 << in.src2;
    break;
  case SLT:
    out.res = ((signed)in.src1 < (signed)in.src2);
    break;
  case SLTU:
    out.res = ((unsigned)in.src1 < (unsigned)in.src2);
    break;
  case XOR:
    out.res = in.src1 ^ in.src2;
    break;
  case SRL:
    out.res = (unsigned)in.src1 >> in.src2;
    break;
  case SRA:
    out.res = (signed)in.src1 >> in.src2;
    break;
  case OR:
    out.res = in.src1 | in.src2;
    break;
  default: {
    /*cerr << "error" << endl;*/
    /*assert(0);*/
    /*exit(-1);*/
    break;
  }
  }
}

void AGU::cycle() { out.addr = in.base + in.off; }
