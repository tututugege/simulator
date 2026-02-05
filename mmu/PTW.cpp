#include "include/PTW.h"
#include <iostream>
#include <cstring>

using namespace std;
using namespace ptw_n;

extern TLB_to_PTW tlb2ptw;
extern PTW_to_TLB ptw2tlb;
extern uint32_t *p_memory;

uint32_t pte_mem_count = 0; // 统计PTW访问内存（Cache miss）的页表项次数
const int PTW_EMU_MEM_CYCLES = 0; // 模拟访问内存的周期数

PTW::PTW(TLB_to_PTW *tlb2ptw_ptr, PTW_to_TLB *ptw2tlb_ptr,
         dcache_req_master_t *req_m, dcache_req_slave_t *req_s,
         dcache_resp_master_t *resp_m, dcache_resp_slave_t *resp_s,
         mmu_state_t *mmu_state_ptr)
    : in{.tlb2ptw = tlb2ptw_ptr, .dcache_resp = resp_m, .dcache_req = req_s},
      out{.ptw2tlb = ptw2tlb_ptr, .dcache_req = req_m, .dcache_resp = resp_s},
      mmu_state{mmu_state_ptr}, pte1{}, pte2{} {
  // reset registers/wires
  ptw_state = ptw_state_next = IDLE;
  dcache_state = dcache_state_next = DCACHE_IDLE;
}

void PTW::reset() {
  // reset PTW state
  ptw_state = ptw_state_next = IDLE;
  dcache_state = dcache_state_next = DCACHE_IDLE;
}

void PTW::comb() {
  // default assignments
  // out.ptw2tlb->write_valid = false;
  // out.ptw2tlb->entry = {};
  *out.ptw2tlb = {};
  static int ptw_mem_access_cycles = 0; // 模拟内存访问周期

  switch (ptw_state) {
  case IDLE:
    if (in.tlb2ptw->tlb_miss) {
      // start page table walk
      ptw_state_next = CACHE_1;
    } else {
      ptw_state_next = IDLE;
    }
    break;

  case CACHE_1:
    // deal with first level page table entry
    // printf("[PTW::comb] CACHE_1 start vpn1=0x%03x sim_time=%lld\n",
    // in.tlb2ptw->vpn1, sim_time);
    ptw_state_next = CACHE_1; // cache not responded yet
    out.dcache_req->valid = false;
    out.dcache_resp->ready = false;
    if (dcache_state == DCACHE_IDLE) {
      // 如果 D-Cache 空闲，准备发起读请求
      out.dcache_req->valid = true;
      uint32_t ptag = mmu_state->satp.ppn & 0xFFFFF;
      uint32_t vindex = (vpn1 & 0x3FF)
                        << 2; // 12 bits 虚拟索引 (实际上是实地址低位索引)
      out.dcache_req->paddr = (ptag << 12) | vindex;

      // check handshake
      bool read_req_hs = out.dcache_req->valid && in.dcache_req->ready;
      if (read_req_hs) {
        dcache_state_next = DCACHE_BUSY; // request has been sent
      }
    } else {
      // wait for dcache response
      out.dcache_resp->ready = true;
      // check handshake
      bool read_resp_hs = in.dcache_resp->valid && out.dcache_resp->ready;
      if (read_resp_hs) {
        // read response received
        dcache_state_next = DCACHE_IDLE;
        if (in.dcache_resp->miss) {
          // D-Cache Miss, enter MEM_1
          ptw_state_next = MEM_1;
        } else {
          // D-Cache Hit, process PTE
          uint32_t pte_number = in.dcache_resp->data;
          // pte1 = *reinterpret_cast<pte_t *>(&pte_number);
          std::memcpy(&pte1, &pte_number, sizeof(pte_t));
          // decide next step based on PTE (refill or next level)
          bool is_megapage = false;
          if (is_page_fault(pte1, op_type, true, &is_megapage)) {
            ptw_state_next = IDLE;
            // refill tlb
            out.ptw2tlb->entry.set_valid_pte(pte1, mmu_state->satp.asid, vpn1,
                                             vpn0, is_megapage);
            out.ptw2tlb->write_valid = true;
          } else if (is_megapage) {
            ptw_state_next = IDLE;
            // refill tlb
            out.ptw2tlb->entry.set_valid_pte(pte1, mmu_state->satp.asid, vpn1,
                                             vpn0, is_megapage);
            out.ptw2tlb->write_valid = true;
          } else {
            ptw_state_next = CACHE_2;
          }
          // cout << "[PTW::comb] CACHE_1 completed, ";
          // cout << "since dcache not implemented, exit now." << endl;
          // exit(1);
        }
      }
    }
    break;

  case MEM_1:
    // wait for memory response for first level page table entry
    ptw_state_next = MEM_1; // memory not responded yet
    // 模拟访问内存的延迟，用变量记录 cycle
    if (ptw_mem_access_cycles < PTW_EMU_MEM_CYCLES) {
      ptw_state_next = MEM_1; // 继续保持在 MEM_1 状态，等待内存访问完成
      ptw_mem_access_cycles++; // 模拟内存访问延迟
    } else {
      // 访问内存，加载一级页表项
      // uint32_t pte1_addr =
      //     ((mmu_state->satp.ppn & 0xFFFFF) << 10) | (vpn1 & 0x3FF);
      uint32_t pte1_number =
          p_memory[((mmu_state->satp.ppn & 0xFFFFF) << 10) |
                   (vpn1 & 0x3FF)]; // 计算一级页表项的物理地址
      // pte1 = *reinterpret_cast<pte_t *>(&pte1_number);
      std::memcpy(&pte1, &pte1_number, sizeof(pte_t));
      // 根据是否 page fault 来决定下一个状态
      bool is_megapage = false;
      if (is_page_fault(pte1, op_type, true, &is_megapage)) {
        ptw_state_next = IDLE;
        // refill tlb
        out.ptw2tlb->entry.set_valid_pte(pte1, mmu_state->satp.asid, vpn1, vpn0,
                                         is_megapage);
        out.ptw2tlb->write_valid = true;
      } else if (is_megapage) {
        ptw_state_next = IDLE;
        // refill tlb
        out.ptw2tlb->entry.set_valid_pte(pte1, mmu_state->satp.asid, vpn1, vpn0,
                                         is_megapage);
        out.ptw2tlb->write_valid = true;
      } else {
        ptw_state_next = CACHE_2;
      }
      ptw_mem_access_cycles = 0; // 重置内存访问周期计数器
      pte_mem_count++;           // 统计访问内存的页表项次数
    }
    break;

  case CACHE_2:
    // printf("[PTW::comb] CACHE_2 start vpn0=0x%03x sim_time=%lld\n", vpn0,
    // sim_time); deal with second level page table entry
    ptw_state_next = CACHE_2; // cache not responded yet
    out.dcache_req->valid = false;
    out.dcache_resp->ready = false;
    if (dcache_state == DCACHE_IDLE) {
      // 如果 D-Cache 空闲，准备发起读请求
      out.dcache_req->valid = true;
      uint32_t ptag = (pte1.ppn1 << 10) | pte1.ppn0; // 20 bits 物理页号
      uint32_t vindex = (vpn0 & 0x3FF)
                        << 2; // 12 bits 虚拟索引 (实际上是实地址低位索引)
      out.dcache_req->paddr = (ptag << 12) | vindex;

      // check handshake
      bool read_req_hs = out.dcache_req->valid && in.dcache_req->ready;
      if (read_req_hs) {
        dcache_state_next = DCACHE_BUSY; // request has been sent
      }
    } else {
      // wait for dcache response
      out.dcache_resp->ready = true;
      // check handshake
      bool read_resp_hs = in.dcache_resp->valid && out.dcache_resp->ready;
      if (read_resp_hs) {
        // read response received
        dcache_state_next = DCACHE_IDLE;
        if (in.dcache_resp->miss) {
          // D-Cache Miss, enter MEM_2
          ptw_state_next = MEM_2;
        } else {
          // D-Cache Hit, process PTE
          uint32_t pte2_number = in.dcache_resp->data;
          // pte2 = *reinterpret_cast<pte_t *>(&pte2_number);
          std::memcpy(&pte2, &pte2_number, sizeof(pte_t));
          // decide next step based on PTE (refill or next level)
          bool is_megapage = false;
          if (is_page_fault(pte2, op_type, false, &is_megapage)) {
            ptw_state_next = IDLE;
            // refill tlb
            out.ptw2tlb->entry.set_valid_pte(pte2, mmu_state->satp.asid, vpn1,
                                             vpn0, is_megapage);
            out.ptw2tlb->write_valid = true;
          } else if (is_megapage) {
            ptw_state_next = IDLE;
            // refill tlb
            // out.ptw2tlb->entry.set_valid_pte(pte2, mmu_state->satp.asid,
            // vpn1, vpn0, is_megapage); out.ptw2tlb->write_valid = true;
            cout << "[PTW::comb] MEM_2 megapage refill phase: MEM_2 -> IDLE"
                 << endl;
            cout << "[PTW::comb] since megapage isn't expected here, ";
            cout << "exit now." << endl;
            exit(1);
          } else {
            ptw_state_next = IDLE;
            out.ptw2tlb->entry.set_valid_pte(pte2, mmu_state->satp.asid, vpn1,
                                             vpn0, is_megapage);
            out.ptw2tlb->write_valid = true;
          }
          // cout << "[PTW::comb] CACHE_2 completed, ";
          // cout << "Since dcache has not been implemented yet, ";
          // cout << "exit now." << endl;
          // exit(1);
        }
      }
    }
    break;

  case MEM_2:
    // wait for memory response for first level page table entry
    ptw_state_next = MEM_2; // memory not responded yet
    // 模拟访问内存的延迟，用变量记录 cycle
    if (ptw_mem_access_cycles < PTW_EMU_MEM_CYCLES) {
      ptw_state_next = MEM_2; // 继续保持在 MEM_2 状态，等待内存访问完成
      ptw_mem_access_cycles++; // 模拟内存访问延迟
    } else {
      // 访问内存，加载二级页表项
      uint32_t pte2_ppn = (pte1.ppn1 << 10) | pte1.ppn0; // 20 bits 物理页号
      uint32_t pte2_number =
          p_memory[((pte2_ppn) & 0xFFFFF) << 10 |
                   (vpn0 & 0x3FF)]; // 计算二级页表项的物理地址
      // pte2 = *reinterpret_cast<pte_t *>(&pte2_number);
      std::memcpy(&pte2, &pte2_number, sizeof(pte_t));
      // 根据是否 page fault 来决定下一个状态
      bool is_megapage = false;
      if (is_page_fault(pte2, op_type, false, &is_megapage)) {
        ptw_state_next = IDLE;
        // refill tlb
        out.ptw2tlb->entry.set_valid_pte(pte2, mmu_state->satp.asid, vpn1, vpn0,
                                         is_megapage);
        out.ptw2tlb->write_valid = true;
      } else if (is_megapage) {
        // should not happen, since megapage handled in level 1
        cout << "[PTW::comb] MEM_2 megapage refill phase: MEM_2 -> IDLE"
             << endl;
        cout << "[PTW::comb] since megapage isn't expected here, ";
        cout << "exit now." << endl;
        exit(1);
      } else {
        ptw_state_next = IDLE; // normally refill TLB
        out.ptw2tlb->entry.set_valid_pte(pte2, mmu_state->satp.asid, vpn1, vpn0,
                                         is_megapage);
        out.ptw2tlb->write_valid = true;
      }
      ptw_mem_access_cycles = 0; // 重置内存访问周期计数器
      pte_mem_count++;           // 统计访问内存的页表项次数
    }
    break;

  default:
    break;
  }
  // if (sim_time > 436826097)
  // {
  //   printf("\nPTW::comb state=%s next=%s dcache_state=%s next=%s
  //   sim_time=%lld\n",
  //          ptw_state_to_string(ptw_state).c_str(),
  //          ptw_state_to_string(ptw_state_next).c_str(),
  //          (dcache_state == DCACHE_IDLE) ? "IDLE" : "BUSY",
  //          (dcache_state_next == DCACHE_IDLE) ? "IDLE" : "BUSY",
  //          sim_time);
  //   printf("out.ptw2dcache_req->valid=%d out.ptw2dcache_req->paddr=0x%08X
  //   sim_time:%lld\n", out.dcache_req->valid, out.dcache_req->paddr,
  //   sim_time); printf("out.ptw2dcache_resp->ready=%d sim_time:%lld\n",
  //   out.dcache_resp->ready, sim_time); printf("in.dcache2ptw_resp->valid=%d
  //   in.dcache2ptw_resp->miss=%d in.dcache2ptw_resp->data=0x%08X
  //   sim_time:%lld\n", in.dcache_resp->valid, in.dcache_resp->miss,
  //   in.dcache_resp->data, sim_time); printf("in.dcache2ptw_resp->valid=%d
  //   sim_time:%lld\n", in.dcache_resp->valid, sim_time);
  // }
}

void PTW::seq() {
  // record the handling request
  if (ptw_state == IDLE && in.tlb2ptw->tlb_miss) {
    vpn1 = in.tlb2ptw->vpn1;
    vpn0 = in.tlb2ptw->vpn0;
    op_type = in.tlb2ptw->op_type;
  }
  // state update
  ptw_state = ptw_state_next;
  dcache_state = dcache_state_next;

  // printf("ptw2dcache_req->valid=%d ptw2dcache_req->paddr=0x%08X
  // sim_time:%lld\n",out.dcache_req->valid, out.dcache_req->paddr, sim_time);
  // printf("dcache2ptw_resp->valid=%d dcache2ptw_resp->miss=%d
  // dcache2ptw_resp->data=0x%08X sim_time:%lld\n",in.dcache_resp->valid,
  // in.dcache_resp->miss, in.dcache_resp->data, sim_time);
}

bool PTW::is_page_fault(pte_t pte, uint8_t op_type, bool stage_1,
                        bool *is_megapage) {
  if (pte.valid == false) {
    return true;
  }
  if (pte.read == false && pte.write == true) {
    return true; // 如果只允许写操作而不允许读操作，则发生缺页异常
  }

  // bool mxr = mstatus.mxr; // Make eXecutable Readable
  // bool sum = mstatus.sum; // Supervisor User Memory access
  // bool mprv = mstatus.mprv; // Modify PRiVilege
  bool mxr = (mmu_state->mstatus >> 19) & 0x1;     // bits[19]
  bool sum_m = (mmu_state->mstatus >> 18) & 0x1;   // bits[18]
  bool sum_s = (mmu_state->sstatus >> 18) & 0x1;   // bits[18]
  bool mprv = (mmu_state->mstatus >> 17) & 0x1;    // bits[17]
  uint32_t mpp = (mmu_state->mstatus >> 11) & 0x3; // bits[12:11]
  uint32_t privilege = mmu_state->privilege;       // 当前特权级

  if (pte.read == true || pte.execute == true) {
    if ((op_type == 0 && pte.execute == true) ||
        (op_type == 1 && pte.read == true) ||
        (op_type == 1 && mxr == true && pte.execute == true) ||
        (op_type == 2 && pte.write == true)) {
      ; // 允许的访问类型
    } else {
      return true;
    }

    // 检查 S 模式下的访问权限: S 模式下不允许访问 U 模式可访问的页面
    if (privilege == 1 && sum_m == false && pte.user == true &&
        sum_s == false) {
      return true;
    }

    // 非 S 模式下，MPRV 启用且 MPP 为 S 模式时不允许访问 U 模式可访问的页面
    if (privilege != 1 && mprv == true && mpp == 1 && sum_m == false &&
        pte.user == true && sum_s == false) {
      return true;
    }

    // 检查 Megapage
    // (如果还是在第一级查询，但是PTE指向的不是页表，说明一定是大页)
    if (stage_1 && pte.ppn0 != 0) {
      return true; // Megapage 的 PPN0 必须非0
    }

    if (pte.accessed == false) {
      return true;
    }
    if (op_type == 2 && pte.dirty == false) {
      return true; // 如果是写操作，脏位必须为1
    }
    *is_megapage = stage_1;
    return false;
  }
  *is_megapage = false;
  return false; // 没有发生缺页异常
}

/*
 * for debugging
 */
void PTW::log_pte(pte_t pte) {
  cout << "PTE: valid: " << pte.valid << ", read: " << pte.read
       << ", write: " << pte.write << ", execute: " << pte.execute
       << ", user: " << pte.user << ", global: " << pte.global
       << ", accessed: " << pte.accessed << ", dirty: " << pte.dirty
       << ", reserved: " << pte.reserved << ", ppn0: " << pte.ppn0
       << ", ppn1: " << pte.ppn1 << endl;
}

string PTW::ptw_state_to_string(enum ptw_state state) {
  switch (state) {
  case IDLE:
    return "IDLE";
  case CACHE_1:
    return "CACHE_1";
  case MEM_1:
    return "MEM_1";
  case CACHE_2:
    return "CACHE_2";
  case MEM_2:
    return "MEM_2";
  default:
    return "UNKNOWN_STATE";
  }
}
