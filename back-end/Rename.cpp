#include <Rename.h>
#include <config.h>
#include <cvt.h>

void Rename::init() {
  for (int i = 0; i < INST_WAY; i++)
    RAT.to_rat.we[i] = true;

  for (int i = 0; i < ARF_NUM; i += 2) {
    arch_RAT[i] = i;
    arch_RAT[i + 1] = i + 1;
    RAT.to_rat.waddr[0] = i;
    RAT.to_rat.waddr[1] = i + 1;
    RAT.to_rat.wdata[0] = i;
    RAT.to_rat.wdata[1] = i + 1;
    RAT.write();
  }

  for (int i = 0; i < PRF_NUM - ARF_NUM; i++) {

    free_list.to_fifo.wdata[0] = ARF_NUM + i;
    free_list.to_fifo.we[0] = true;
    free_list.write();
  }
}

void Rename::free_gp(int idx) { gp_v[idx] = false; }

void Rename::comb() {
  /*if (free_list_count < INST_WAY) {*/
  /*  out.full = true;*/
  /*  return;*/
  /*}*/

  // 读free_list和RAT
  for (int i = 0; i < INST_WAY; i++) {

    RAT.to_rat.raddr[3 * i] = in.src1_areg_idx[i];
    RAT.to_rat.raddr[3 * i + 1] = in.src2_areg_idx[i];
    RAT.to_rat.raddr[3 * i + 2] = in.dest_areg_idx[i];
    free_list.to_fifo.re[i] = in.valid[i];
  }

  free_list.read();
  RAT.read();

  // 无waw raw的输出
  for (int i = 0; i < INST_WAY; i++) {
    if (in.valid[i]) {
      if (in.dest_areg_en[i]) {
        int new_preg = free_list.from_fifo.rdata[i];

        out.dest_preg_idx[i] = new_preg;
        out.old_dest_preg_idx[i] = RAT.from_rat.rdata[3 * i + 2];

        /*out.src1_raw[i] = false;*/
        /*out.src2_raw[i] = false;*/
      }
      out.gp_idx[i] = RAT.alloc_gp();
      gp_v_1[out.gp_idx[i]] = true;
      out.src1_preg_idx[i] = RAT.from_rat.rdata[3 * i];
      out.src2_preg_idx[i] = RAT.from_rat.rdata[3 * i + 1];
    }
  }

  // 处理raw waw 处理RAT写信号
  for (int i = 0; i < INST_WAY; i++) {
    if (in.dest_areg_en[i] == 0)
      continue;

    // raw 修改源寄存器号
    for (int j = i + 1; j < INST_WAY; j++) {
      if (in.src1_areg_en[j] && in.src1_areg_idx[j] == in.dest_areg_idx[i]) {
        out.src1_preg_idx[j] = out.dest_preg_idx[i];
        /*out.src1_raw[j] = true;*/
      }

      if (in.src2_areg_en[j] && in.src2_areg_idx[j] == in.dest_areg_idx[i]) {
        out.src2_preg_idx[j] = out.dest_preg_idx[i];
        /*out.src2_raw[j] = true;*/
      }
    }

    // waw 同一preg的映射表写入只取最新的  写入rob的old_preg信息可能不来自RAT
    RAT.to_rat.we[i] = true;
    RAT.to_rat.waddr[i] = out.dest_preg_idx[i];
    RAT.to_rat.wdata[i] = in.dest_areg_idx[i];

    for (int j = i + 1; j < INST_WAY; j++) {
      if (in.dest_areg_en[j] && in.dest_areg_idx[j] == in.dest_areg_idx[i]) {
        out.old_dest_preg_idx[j] = out.dest_preg_idx[i];
        RAT.to_rat.we[i] = false;
        break;
      }
    }
  }

  // free_list 入队 gp释放
  for (int i = 0; i < ISSUE_WAY; i++) {
    if (in.commit_valid[i]) {

      free_list.to_fifo.we[i] = in.commit_dest_en[i];
      free_list.to_fifo.wdata[i] = in.commit_old_dest_preg_idx[i];

      gp_v_1[in.commit_gp_idx[i]] = false;
    } else
      free_list.to_fifo.we[i] = false;
  }
}

void Rename ::seq() {
  RAT.write();
  free_list.write();
}

/*void Rename ::gp_write(int idx) {*/
/*  for (int i = 0; i < PRF_NUM; i++) {*/
/*    spec_cRAT[i].gp[idx] = cRAT_valid_wdata[i];*/
/*  }*/
/*}*/

/*void Rename::print_reg() {*/
/*  int preg_idx;*/
/*  for (int i = 0; i < ARF_NUM; i++) {*/
/*    preg_idx = arch_RAT[i];*/
/*    uint32_t data = cvt_bit_to_number_unsigned(preg_base + preg_idx * 32,
 * 32);*/
/**/
/*    cout << reg_names[i] << ": " << hex << data << " ";*/
/**/
/*    if (i % 8 == 0)*/
/*      cout << endl;*/
/*  }*/
/*  cout << endl;*/
/*}*/
/**/
/*uint32_t Rename::reg(int idx) {*/
/*  int preg_idx = arch_RAT[idx];*/
/*  return cvt_bit_to_number_unsigned(preg_base + preg_idx * 32, 32);*/
/*}*/
/**/
/*void Rename::print_RAT() {*/
/*  for (int i = 0; i < ARF_NUM; i++) {*/
/*    cout << dec << i << ":" << dec << arch_RAT[i] << " ";*/
/**/
/*    if (i % 8 == 0)*/
/*      cout << endl;*/
/*  }*/
/*  cout << endl;*/
/*}*/
