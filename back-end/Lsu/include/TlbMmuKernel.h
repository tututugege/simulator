#pragma once

#include <stdint.h>

#ifndef TLB_KERNEL_PTE_R
#define TLB_KERNEL_PTE_R (1u << 1)
#endif
#ifndef TLB_KERNEL_PTE_W
#define TLB_KERNEL_PTE_W (1u << 2)
#endif
#ifndef TLB_KERNEL_PTE_X
#define TLB_KERNEL_PTE_X (1u << 3)
#endif
#ifndef TLB_KERNEL_PTE_U
#define TLB_KERNEL_PTE_U (1u << 4)
#endif
#ifndef TLB_KERNEL_PTE_G
#define TLB_KERNEL_PTE_G (1u << 5)
#endif
#ifndef TLB_KERNEL_PTE_A
#define TLB_KERNEL_PTE_A (1u << 6)
#endif
#ifndef TLB_KERNEL_PTE_D
#define TLB_KERNEL_PTE_D (1u << 7)
#endif

#define TLB_KERNEL_ASID_BITS 9
#define TLB_KERNEL_VPN_PART_BITS 10
#define TLB_KERNEL_VPN_BITS 20
#define TLB_KERNEL_PPN_BITS 22
#define TLB_KERNEL_PERM_BITS 8
#define TLB_KERNEL_PACKED_ENTRY_WIDTH 62
#define TLB_KERNEL_PACKED_LOOKUP_RESP_WIDTH 63
#define TLB_KERNEL_PACKED_WALK_REGS_WIDTH 72
#define TLB_KERNEL_PACKED_WALK_PORT_IN_WIDTH 54
#define TLB_KERNEL_PACKED_WALK_REQ_WIDTH 67
#define TLB_KERNEL_TOP_PORT_PI_WIDTH 96
#define TLB_KERNEL_TOP_PORT_PO_WIDTH 57
#define TLB_KERNEL_TOP_PORT_WITH_TYPE_PI_WIDTH 98
#define TLB_KERNEL_CORE_PERM_CHECK_PI_WIDTH 14
#define TLB_KERNEL_CORE_ENTRY_MATCH_PI_WIDTH 103
#define TLB_KERNEL_CORE_ASID_MATCH_PI_WIDTH 19
#define TLB_KERNEL_CORE_VPN1_MATCH_PI_WIDTH 20
#define TLB_KERNEL_CORE_VPN0_MATCH_PI_WIDTH 21
#define TLB_KERNEL_CORE_ENTRY_MATCH_SELECT_PI_WIDTH 4
#define TLB_KERNEL_CORE_PADDR_PI_WIDTH 94
#define TLB_KERNEL_CORE_PADDR_PO_WIDTH 32
#define TLB_KERNEL_CORE_HIT_SELECT_PI_WIDTH 127
#define TLB_KERNEL_CORE_HIT_SELECT_PO_WIDTH 63
#define TLB_KERNEL_CORE_EFF_PRIV_PI_WIDTH 7
#define TLB_KERNEL_CORE_EFF_PRIV_PO_WIDTH 2
#define TLB_KERNEL_CORE_PAGE_MATCH_PI_WIDTH 40
#define TLB_KERNEL_CORE_SATP_MATCH_PI_WIDTH 64

typedef struct {
  int pos;
} TlbKernelCursor;

typedef struct {
  uint32_t valid;
  uint32_t global;
  uint32_t asid;
  uint32_t level;
  uint32_t vpn1;
  uint32_t vpn0;
  uint32_t ppn;
  uint32_t perm;
} TlbKernelEntry;

typedef struct {
  uint32_t hit;
  TlbKernelEntry entry;
} TlbKernelLookup;

typedef struct {
  uint32_t active;
  uint32_t req_sent;
  uint32_t v_addr;
  uint32_t type;
  uint32_t satp;
  uint32_t eff_priv;
  uint32_t mxr;
  uint32_t sum;
} TlbKernelWalkRegs;

typedef struct {
  uint32_t valid;
  TlbKernelEntry entry;
  uint32_t slot;
  uint32_t next_repl_ptr;
} TlbKernelRefill;

typedef struct {
  uint32_t fault;
  uint32_t vaddr;
  uint32_t leaf_pte;
  uint32_t leaf_level;
} TlbKernelWalkResp;

typedef struct {
  uint32_t valid;
  uint32_t vaddr;
  uint32_t satp;
  uint32_t access_type;
} TlbKernelWalkReq;

typedef struct {
  TlbKernelWalkRegs walk;
  TlbKernelRefill refill;
  uint32_t walk_req_ready;
  uint32_t walk_resp_valid;
  TlbKernelWalkReq walk_req;
  uint32_t walk_resp_consumed;
} TlbKernelTopAccState;

typedef struct {
  uint32_t translate_valid;
  uint32_t v_addr;
  uint32_t type;
  uint32_t satp;
  uint32_t privilege;
  uint32_t mstatus_mxr;
  uint32_t mstatus_sum;
  uint32_t mstatus_mprv;
  uint32_t mstatus_mpp;
  TlbKernelLookup lookup;
  TlbKernelWalkRegs walk;
  TlbKernelRefill refill;
  uint32_t repl_ptr;
  uint32_t tlb_capacity;
  uint32_t walk_req_ready;
  uint32_t walk_resp_valid;
  TlbKernelWalkResp walk_resp;
} TlbKernelCoreInput;

typedef struct {
  uint32_t translate_valid;
  uint32_t v_addr;
  uint32_t type;
  uint32_t satp;
  uint32_t eff_priv;
  uint32_t mxr;
  uint32_t sum;
  TlbKernelWalkRegs walk;
  TlbKernelRefill refill;
  uint32_t walk_req_ready;
  uint32_t walk_resp_valid;
  uint32_t walk_resp_fault;
  uint32_t walk_resp_page_match;
  uint32_t walk_resp_ppn;
  uint32_t walk_resp_perm;
  uint32_t walk_resp_leaf_level;
  uint32_t hit_valid;
  uint32_t hit_perm_ok;
  uint32_t hit_paddr;
  uint32_t hit_level;
  uint32_t hit_perm;
  uint32_t walk_resp_perm_ok;
  uint32_t walk_same_request;
  uint32_t refill_slot;
  uint32_t refill_next_repl_ptr;
} TlbKernelCoreOutputMapperInput;

typedef struct {
  uint32_t resp_valid;
  uint32_t result;
  uint32_t p_addr;
  uint32_t retry_reason;
  TlbKernelWalkRegs walk_next;
  TlbKernelRefill refill_next;
  TlbKernelWalkReq walk_req;
  uint32_t walk_resp_consumed;
  uint32_t trace_source;
  uint32_t trace_level;
  uint32_t trace_perm;
} TlbKernelCoreOutput;

typedef struct {
  uint32_t valid;
  uint32_t v_addr;
  uint32_t type;
  TlbKernelLookup lookup;
} TlbKernelTopPortInput;

typedef struct {
  uint32_t resp_valid;
  uint32_t result;
  uint32_t p_addr;
  uint32_t retry_reason;
  uint32_t trace_source;
  uint32_t trace_level;
  uint32_t trace_perm;
} TlbKernelTopPortOutput;

static inline int tlb_kernel_slot_width(int entries) {
  int width = 0;
  int capacity = entries > 0 ? entries : 1;
  int value = capacity - 1;
  while (value > 0) {
    ++width;
    value >>= 1;
  }
  return width;
}

static inline uint32_t tlb_kernel_mask_u32(int width) {
  if (width >= 32) {
    return 0xffffffffu;
  }
  if (width <= 0) {
    return 0u;
  }
  return (1u << width) - 1u;
}

static inline uint32_t tlb_kernel_read_uint(const bool *bits,
                                            TlbKernelCursor *cur, int width) {
  uint32_t value = 0;
  for (int i = 0; i < width; ++i) {
    if (bits[cur->pos++]) {
      value |= (1u << i);
    }
  }
  return value;
}

static inline uint32_t tlb_kernel_read_bit(const bool *bits,
                                           TlbKernelCursor *cur) {
  return bits[cur->pos++] ? 1u : 0u;
}

static inline void tlb_kernel_write_uint(bool *bits, TlbKernelCursor *cur,
                                         uint32_t value, int width) {
  for (int i = 0; i < width; ++i) {
    bits[cur->pos++] = ((value >> i) & 1u) != 0;
  }
}

static inline void tlb_kernel_write_bit(bool *bits, TlbKernelCursor *cur,
                                        uint32_t value) {
  bits[cur->pos++] = (value & 1u) != 0;
}

static inline uint32_t tlb_kernel_pack_pte_payload(uint32_t pte) {
  return (((pte >> 10) & 0x3fffffu) << 8) | (pte & 0xffu);
}

static inline uint32_t tlb_kernel_unpack_pte_payload(uint32_t payload) {
  return ((payload >> 8) << 10) | (payload & 0xffu);
}

static inline uint32_t tlb_kernel_sv32_asid(uint32_t satp) {
  return (satp >> 22) & 0x1ffu;
}

static inline void tlb_kernel_encode_entry(bool *bits, TlbKernelCursor *cur,
                                           const TlbKernelEntry *entry) {
  tlb_kernel_write_bit(bits, cur, entry->valid);
  tlb_kernel_write_bit(bits, cur, entry->global);
  tlb_kernel_write_uint(bits, cur, entry->asid, TLB_KERNEL_ASID_BITS);
  tlb_kernel_write_bit(bits, cur, entry->level);
  tlb_kernel_write_uint(bits, cur, entry->vpn1, TLB_KERNEL_VPN_PART_BITS);
  tlb_kernel_write_uint(bits, cur, entry->vpn0, TLB_KERNEL_VPN_PART_BITS);
  tlb_kernel_write_uint(bits, cur, entry->ppn, TLB_KERNEL_PPN_BITS);
  tlb_kernel_write_uint(bits, cur, entry->perm, TLB_KERNEL_PERM_BITS);
}

static inline TlbKernelEntry tlb_kernel_decode_entry(const bool *bits,
                                                     TlbKernelCursor *cur) {
  TlbKernelEntry entry;
  entry.valid = tlb_kernel_read_bit(bits, cur);
  entry.global = tlb_kernel_read_bit(bits, cur);
  entry.asid = tlb_kernel_read_uint(bits, cur, TLB_KERNEL_ASID_BITS);
  entry.level = tlb_kernel_read_bit(bits, cur);
  entry.vpn1 = tlb_kernel_read_uint(bits, cur, TLB_KERNEL_VPN_PART_BITS);
  entry.vpn0 = tlb_kernel_read_uint(bits, cur, TLB_KERNEL_VPN_PART_BITS);
  entry.ppn = tlb_kernel_read_uint(bits, cur, TLB_KERNEL_PPN_BITS);
  entry.perm = tlb_kernel_read_uint(bits, cur, TLB_KERNEL_PERM_BITS);
  return entry;
}

static inline TlbKernelLookup
tlb_kernel_decode_lookup(const bool *bits, TlbKernelCursor *cur) {
  TlbKernelLookup lookup;
  lookup.hit = tlb_kernel_read_bit(bits, cur);
  lookup.entry = tlb_kernel_decode_entry(bits, cur);
  return lookup;
}

static inline void tlb_kernel_encode_lookup(bool *bits, TlbKernelCursor *cur,
                                            const TlbKernelLookup *lookup) {
  tlb_kernel_write_bit(bits, cur, lookup->hit);
  tlb_kernel_encode_entry(bits, cur, &lookup->entry);
}

static inline TlbKernelWalkRegs
tlb_kernel_decode_walk_regs(const bool *bits, TlbKernelCursor *cur) {
  TlbKernelWalkRegs walk;
  walk.active = tlb_kernel_read_bit(bits, cur);
  walk.req_sent = tlb_kernel_read_bit(bits, cur);
  walk.v_addr = tlb_kernel_read_uint(bits, cur, 32);
  walk.type = tlb_kernel_read_uint(bits, cur, 2);
  walk.satp = tlb_kernel_read_uint(bits, cur, 32);
  walk.eff_priv = tlb_kernel_read_uint(bits, cur, 2);
  walk.mxr = tlb_kernel_read_bit(bits, cur);
  walk.sum = tlb_kernel_read_bit(bits, cur);
  return walk;
}

static inline void tlb_kernel_encode_walk_regs(bool *bits,
                                               TlbKernelCursor *cur,
                                               const TlbKernelWalkRegs *walk) {
  tlb_kernel_write_bit(bits, cur, walk->active);
  tlb_kernel_write_bit(bits, cur, walk->req_sent);
  tlb_kernel_write_uint(bits, cur, walk->v_addr, 32);
  tlb_kernel_write_uint(bits, cur, walk->type, 2);
  tlb_kernel_write_uint(bits, cur, walk->satp, 32);
  tlb_kernel_write_uint(bits, cur, walk->eff_priv, 2);
  tlb_kernel_write_bit(bits, cur, walk->mxr);
  tlb_kernel_write_bit(bits, cur, walk->sum);
}

static inline TlbKernelRefill
tlb_kernel_decode_refill(const bool *bits, TlbKernelCursor *cur, int entries) {
  const int slot_bits = tlb_kernel_slot_width(entries);
  TlbKernelRefill refill;
  refill.valid = tlb_kernel_read_bit(bits, cur);
  refill.entry = tlb_kernel_decode_entry(bits, cur);
  refill.slot = tlb_kernel_read_uint(bits, cur, slot_bits);
  refill.next_repl_ptr = tlb_kernel_read_uint(bits, cur, slot_bits);
  return refill;
}

static inline void tlb_kernel_encode_refill(bool *bits, TlbKernelCursor *cur,
                                            const TlbKernelRefill *refill,
                                            int entries) {
  const int slot_bits = tlb_kernel_slot_width(entries);
  tlb_kernel_write_bit(bits, cur, refill->valid);
  tlb_kernel_encode_entry(bits, cur, &refill->entry);
  tlb_kernel_write_uint(bits, cur, refill->slot, slot_bits);
  tlb_kernel_write_uint(bits, cur, refill->next_repl_ptr, slot_bits);
}

static inline void tlb_kernel_decode_walk_port_view(
    const bool *bits, TlbKernelCursor *cur, uint32_t *req_ready,
    uint32_t *resp_valid, TlbKernelWalkResp *resp) {
  *req_ready = tlb_kernel_read_bit(bits, cur);
  *resp_valid = tlb_kernel_read_bit(bits, cur);
  resp->fault = tlb_kernel_read_bit(bits, cur);
  resp->vaddr = tlb_kernel_read_uint(bits, cur, TLB_KERNEL_VPN_BITS) << 12;
  resp->leaf_pte = tlb_kernel_unpack_pte_payload(
      tlb_kernel_read_uint(bits, cur,
                           TLB_KERNEL_PPN_BITS + TLB_KERNEL_PERM_BITS));
  resp->leaf_level = tlb_kernel_read_bit(bits, cur);
}

static inline void tlb_kernel_encode_walk_req(bool *bits,
                                              TlbKernelCursor *cur,
                                              const TlbKernelWalkReq *req) {
  tlb_kernel_write_bit(bits, cur, req->valid);
  tlb_kernel_write_uint(bits, cur, req->vaddr, 32);
  tlb_kernel_write_uint(bits, cur, req->satp, 32);
  tlb_kernel_write_uint(bits, cur, req->access_type, 2);
}

static inline TlbKernelWalkReq
tlb_kernel_decode_walk_req(const bool *bits, TlbKernelCursor *cur) {
  TlbKernelWalkReq req;
  req.valid = tlb_kernel_read_bit(bits, cur);
  req.vaddr = tlb_kernel_read_uint(bits, cur, 32);
  req.satp = tlb_kernel_read_uint(bits, cur, 32);
  req.access_type = tlb_kernel_read_uint(bits, cur, 2);
  return req;
}

static inline int tlb_kernel_core_pi_width(int entries) {
  const int slot_bits = tlb_kernel_slot_width(entries);
  return 1 + 32 + 2 + 32 + 2 + 1 + 1 + 1 + 2 +
         TLB_KERNEL_PACKED_LOOKUP_RESP_WIDTH +
         TLB_KERNEL_PACKED_WALK_REGS_WIDTH +
         (1 + TLB_KERNEL_PACKED_ENTRY_WIDTH + 2 * slot_bits) + slot_bits +
         TLB_KERNEL_PACKED_WALK_PORT_IN_WIDTH;
}

static inline int tlb_kernel_core_po_width(int entries) {
  return 1 + 2 + 32 + 3 + TLB_KERNEL_PACKED_WALK_REGS_WIDTH +
         (1 + TLB_KERNEL_PACKED_ENTRY_WIDTH +
          2 * tlb_kernel_slot_width(entries)) +
         TLB_KERNEL_PACKED_WALK_REQ_WIDTH + 1 + 3 + 8 + 8;
}

static inline int tlb_kernel_core_repl_next_pi_width(int entries) {
  return tlb_kernel_slot_width(entries);
}

static inline int tlb_kernel_core_repl_next_po_width(int entries) {
  return 2 * tlb_kernel_slot_width(entries);
}

static inline int tlb_kernel_core_output_mapper_pi_width(int entries) {
  const int slot_bits = tlb_kernel_slot_width(entries);
  return 1 + 32 + 2 + 32 + 2 + 1 + 1 + TLB_KERNEL_PACKED_WALK_REGS_WIDTH +
         (1 + TLB_KERNEL_PACKED_ENTRY_WIDTH + 2 * slot_bits) + 1 + 1 + 1 +
         1 + TLB_KERNEL_PPN_BITS + TLB_KERNEL_PERM_BITS + 1 + 1 + 1 + 32 +
         1 + TLB_KERNEL_PERM_BITS + 1 + 1 + 2 * slot_bits;
}

static inline int tlb_kernel_top_acc_state_width(int entries) {
  return TLB_KERNEL_PACKED_WALK_REGS_WIDTH +
         (1 + TLB_KERNEL_PACKED_ENTRY_WIDTH +
          2 * tlb_kernel_slot_width(entries)) +
         1 + 1 + TLB_KERNEL_PACKED_WALK_REQ_WIDTH + 1;
}

static inline void tlb_kernel_encode_core_input(bool *bits,
                                                const TlbKernelCoreInput *in,
                                                int entries) {
  TlbKernelCursor cur = {0};
  tlb_kernel_write_bit(bits, &cur, in->translate_valid);
  tlb_kernel_write_uint(bits, &cur, in->v_addr, 32);
  tlb_kernel_write_uint(bits, &cur, in->type, 2);
  tlb_kernel_write_uint(bits, &cur, in->satp, 32);
  tlb_kernel_write_uint(bits, &cur, in->privilege, 2);
  tlb_kernel_write_bit(bits, &cur, in->mstatus_mxr);
  tlb_kernel_write_bit(bits, &cur, in->mstatus_sum);
  tlb_kernel_write_bit(bits, &cur, in->mstatus_mprv);
  tlb_kernel_write_uint(bits, &cur, in->mstatus_mpp, 2);
  tlb_kernel_encode_lookup(bits, &cur, &in->lookup);
  tlb_kernel_encode_walk_regs(bits, &cur, &in->walk);
  tlb_kernel_encode_refill(bits, &cur, &in->refill, entries);
  tlb_kernel_write_uint(bits, &cur, in->repl_ptr,
                        tlb_kernel_slot_width(entries));
  tlb_kernel_write_bit(bits, &cur, in->walk_req_ready);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp_valid);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp.fault);
  tlb_kernel_write_uint(bits, &cur, in->walk_resp.vaddr >> 12,
                        TLB_KERNEL_VPN_BITS);
  tlb_kernel_write_uint(bits, &cur,
                        tlb_kernel_pack_pte_payload(in->walk_resp.leaf_pte),
                        TLB_KERNEL_PPN_BITS + TLB_KERNEL_PERM_BITS);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp.leaf_level);
}

static inline TlbKernelCoreInput
tlb_kernel_decode_core_input(const bool *pi, int entries) {
  TlbKernelCursor cur = {0};
  TlbKernelCoreInput in;
  in.translate_valid = tlb_kernel_read_bit(pi, &cur);
  in.v_addr = tlb_kernel_read_uint(pi, &cur, 32);
  in.type = tlb_kernel_read_uint(pi, &cur, 2);
  in.satp = tlb_kernel_read_uint(pi, &cur, 32);
  in.privilege = tlb_kernel_read_uint(pi, &cur, 2);
  in.mstatus_mxr = tlb_kernel_read_bit(pi, &cur);
  in.mstatus_sum = tlb_kernel_read_bit(pi, &cur);
  in.mstatus_mprv = tlb_kernel_read_bit(pi, &cur);
  in.mstatus_mpp = tlb_kernel_read_uint(pi, &cur, 2);
  in.lookup = tlb_kernel_decode_lookup(pi, &cur);
  in.walk = tlb_kernel_decode_walk_regs(pi, &cur);
  in.refill = tlb_kernel_decode_refill(pi, &cur, entries);
  in.repl_ptr = tlb_kernel_read_uint(pi, &cur, tlb_kernel_slot_width(entries));
  in.tlb_capacity = entries > 0 ? (uint32_t)entries : 1u;
  tlb_kernel_decode_walk_port_view(pi, &cur, &in.walk_req_ready,
                                   &in.walk_resp_valid, &in.walk_resp);
  return in;
}

static inline void tlb_kernel_encode_core_output(bool *po,
                                                 const TlbKernelCoreOutput *out,
                                                 int entries) {
  TlbKernelCursor cur = {0};
  tlb_kernel_write_bit(po, &cur, out->resp_valid);
  tlb_kernel_write_uint(po, &cur, out->result, 2);
  tlb_kernel_write_uint(po, &cur, out->p_addr, 32);
  tlb_kernel_write_uint(po, &cur, out->retry_reason, 3);
  tlb_kernel_encode_walk_regs(po, &cur, &out->walk_next);
  tlb_kernel_encode_refill(po, &cur, &out->refill_next, entries);
  tlb_kernel_encode_walk_req(po, &cur, &out->walk_req);
  tlb_kernel_write_bit(po, &cur, out->walk_resp_consumed);
  tlb_kernel_write_uint(po, &cur, out->trace_source, 3);
  tlb_kernel_write_uint(po, &cur, out->trace_level, 8);
  tlb_kernel_write_uint(po, &cur, out->trace_perm, 8);
}

static inline TlbKernelCoreOutput
tlb_kernel_decode_core_output(const bool *po, int entries) {
  TlbKernelCursor cur = {0};
  TlbKernelCoreOutput out;
  out.resp_valid = tlb_kernel_read_bit(po, &cur);
  out.result = tlb_kernel_read_uint(po, &cur, 2);
  out.p_addr = tlb_kernel_read_uint(po, &cur, 32);
  out.retry_reason = tlb_kernel_read_uint(po, &cur, 3);
  out.walk_next = tlb_kernel_decode_walk_regs(po, &cur);
  out.refill_next = tlb_kernel_decode_refill(po, &cur, entries);
  out.walk_req = tlb_kernel_decode_walk_req(po, &cur);
  out.walk_resp_consumed = tlb_kernel_read_bit(po, &cur);
  out.trace_source = tlb_kernel_read_uint(po, &cur, 3);
  out.trace_level = tlb_kernel_read_uint(po, &cur, 8);
  out.trace_perm = tlb_kernel_read_uint(po, &cur, 8);
  return out;
}

static inline void tlb_kernel_encode_core_output_mapper_input(
    bool *bits, const TlbKernelCoreOutputMapperInput *in, int entries) {
  TlbKernelCursor cur = {0};
  tlb_kernel_write_bit(bits, &cur, in->translate_valid);
  tlb_kernel_write_uint(bits, &cur, in->v_addr, 32);
  tlb_kernel_write_uint(bits, &cur, in->type, 2);
  tlb_kernel_write_uint(bits, &cur, in->satp, 32);
  tlb_kernel_write_uint(bits, &cur, in->eff_priv, 2);
  tlb_kernel_write_bit(bits, &cur, in->mxr);
  tlb_kernel_write_bit(bits, &cur, in->sum);
  tlb_kernel_encode_walk_regs(bits, &cur, &in->walk);
  tlb_kernel_encode_refill(bits, &cur, &in->refill, entries);
  tlb_kernel_write_bit(bits, &cur, in->walk_req_ready);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp_valid);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp_fault);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp_page_match);
  tlb_kernel_write_uint(bits, &cur, in->walk_resp_ppn, TLB_KERNEL_PPN_BITS);
  tlb_kernel_write_uint(bits, &cur, in->walk_resp_perm, TLB_KERNEL_PERM_BITS);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp_leaf_level);
  tlb_kernel_write_bit(bits, &cur, in->hit_valid);
  tlb_kernel_write_bit(bits, &cur, in->hit_perm_ok);
  tlb_kernel_write_uint(bits, &cur, in->hit_paddr, 32);
  tlb_kernel_write_bit(bits, &cur, in->hit_level);
  tlb_kernel_write_uint(bits, &cur, in->hit_perm, TLB_KERNEL_PERM_BITS);
  tlb_kernel_write_bit(bits, &cur, in->walk_resp_perm_ok);
  tlb_kernel_write_bit(bits, &cur, in->walk_same_request);
  tlb_kernel_write_uint(bits, &cur, in->refill_slot,
                        tlb_kernel_slot_width(entries));
  tlb_kernel_write_uint(bits, &cur, in->refill_next_repl_ptr,
                        tlb_kernel_slot_width(entries));
}

static inline TlbKernelCoreOutputMapperInput
tlb_kernel_decode_core_output_mapper_input(const bool *pi, int entries) {
  TlbKernelCursor cur = {0};
  TlbKernelCoreOutputMapperInput in;
  in.translate_valid = tlb_kernel_read_bit(pi, &cur);
  in.v_addr = tlb_kernel_read_uint(pi, &cur, 32);
  in.type = tlb_kernel_read_uint(pi, &cur, 2);
  in.satp = tlb_kernel_read_uint(pi, &cur, 32);
  in.eff_priv = tlb_kernel_read_uint(pi, &cur, 2);
  in.mxr = tlb_kernel_read_bit(pi, &cur);
  in.sum = tlb_kernel_read_bit(pi, &cur);
  in.walk = tlb_kernel_decode_walk_regs(pi, &cur);
  in.refill = tlb_kernel_decode_refill(pi, &cur, entries);
  in.walk_req_ready = tlb_kernel_read_bit(pi, &cur);
  in.walk_resp_valid = tlb_kernel_read_bit(pi, &cur);
  in.walk_resp_fault = tlb_kernel_read_bit(pi, &cur);
  in.walk_resp_page_match = tlb_kernel_read_bit(pi, &cur);
  in.walk_resp_ppn = tlb_kernel_read_uint(pi, &cur, TLB_KERNEL_PPN_BITS);
  in.walk_resp_perm = tlb_kernel_read_uint(pi, &cur, TLB_KERNEL_PERM_BITS);
  in.walk_resp_leaf_level = tlb_kernel_read_bit(pi, &cur);
  in.hit_valid = tlb_kernel_read_bit(pi, &cur);
  in.hit_perm_ok = tlb_kernel_read_bit(pi, &cur);
  in.hit_paddr = tlb_kernel_read_uint(pi, &cur, 32);
  in.hit_level = tlb_kernel_read_bit(pi, &cur);
  in.hit_perm = tlb_kernel_read_uint(pi, &cur, TLB_KERNEL_PERM_BITS);
  in.walk_resp_perm_ok = tlb_kernel_read_bit(pi, &cur);
  in.walk_same_request = tlb_kernel_read_bit(pi, &cur);
  in.refill_slot = tlb_kernel_read_uint(pi, &cur,
                                        tlb_kernel_slot_width(entries));
  in.refill_next_repl_ptr =
      tlb_kernel_read_uint(pi, &cur, tlb_kernel_slot_width(entries));
  return in;
}

static inline uint32_t tlb_kernel_compose_paddr(uint32_t v_addr,
                                                const TlbKernelEntry *entry) {
  if ((entry->level & 1u) != 0) {
    const uint32_t mask = (1u << 22) - 1u;
    return ((entry->ppn << 12) & ~mask) | (v_addr & mask);
  }
  return (entry->ppn << 12) | (v_addr & 0xfffu);
}

static inline uint32_t tlb_kernel_check_perm(uint32_t perm, uint32_t type,
                                             uint32_t eff_priv, uint32_t sum,
                                             uint32_t mxr) {
  const uint32_t r = (perm & TLB_KERNEL_PTE_R) != 0;
  const uint32_t w = (perm & TLB_KERNEL_PTE_W) != 0;
  const uint32_t x = (perm & TLB_KERNEL_PTE_X) != 0;
  const uint32_t u = (perm & TLB_KERNEL_PTE_U) != 0;
  const uint32_t a = (perm & TLB_KERNEL_PTE_A) != 0;
  const uint32_t d = (perm & TLB_KERNEL_PTE_D) != 0;

  if (type == 0u && !x) {
    return 0;
  }
  if (type == 1u && !r && !(mxr && x)) {
    return 0;
  }
  if (type == 2u && !w) {
    return 0;
  }
  if (eff_priv == 0u && !u) {
    return 0;
  }
  if (eff_priv == 1u && u) {
    if (type == 0u) {
      return 0;
    }
    if (!sum) {
      return 0;
    }
  }
  if (!a) {
    return 0;
  }
  if (type == 2u && !d) {
    return 0;
  }
  return 1;
}

static inline uint32_t tlb_kernel_effective_priv(uint32_t type,
                                                 uint32_t privilege,
                                                 uint32_t mstatus_mprv,
                                                 uint32_t mstatus_mpp) {
  if (type != 0u && mstatus_mprv) {
    return mstatus_mpp & 0x3u;
  }
  return privilege & 0x3u;
}

static inline uint32_t tlb_kernel_entry_matches(const TlbKernelEntry *entry,
                                                uint32_t v_addr,
                                                uint32_t asid) {
  if (!entry->valid) {
    return 0;
  }
  if (!(entry->global || entry->asid == asid)) {
    return 0;
  }
  const uint32_t vpn1 = (v_addr >> 22) & 0x3ffu;
  const uint32_t vpn0 = (v_addr >> 12) & 0x3ffu;
  if (entry->vpn1 != vpn1) {
    return 0;
  }
  return (entry->level == 1u) || (entry->vpn0 == vpn0);
}

static inline void
tlb_kernel_core_perm_check_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t perm = tlb_kernel_read_uint(pi, &r, 8);
  const uint32_t type = tlb_kernel_read_uint(pi, &r, 2);
  const uint32_t eff_priv = tlb_kernel_read_uint(pi, &r, 2);
  const uint32_t sum = tlb_kernel_read_bit(pi, &r);
  const uint32_t mxr = tlb_kernel_read_bit(pi, &r);
  po[0] = tlb_kernel_check_perm(perm, type, eff_priv, sum, mxr) != 0;
}

static inline void
tlb_kernel_core_eff_priv_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t type = tlb_kernel_read_uint(pi, &r, 2);
  const uint32_t privilege = tlb_kernel_read_uint(pi, &r, 2);
  const uint32_t mstatus_mprv = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_mpp = tlb_kernel_read_uint(pi, &r, 2);
  TlbKernelCursor w = {0};
  tlb_kernel_write_uint(po, &w,
                        tlb_kernel_effective_priv(type, privilege,
                                                  mstatus_mprv, mstatus_mpp),
                        TLB_KERNEL_CORE_EFF_PRIV_PO_WIDTH);
}

static inline void
tlb_kernel_core_entry_match_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const TlbKernelEntry entry = tlb_kernel_decode_entry(pi, &r);
  const uint32_t v_addr = tlb_kernel_read_uint(pi, &r, 32);
  const uint32_t asid = tlb_kernel_read_uint(pi, &r, TLB_KERNEL_ASID_BITS);
  po[0] = tlb_kernel_entry_matches(&entry, v_addr, asid) != 0;
}

static inline void
tlb_kernel_core_asid_match_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t global = tlb_kernel_read_bit(pi, &r);
  const uint32_t entry_asid =
      tlb_kernel_read_uint(pi, &r, TLB_KERNEL_ASID_BITS);
  const uint32_t query_asid =
      tlb_kernel_read_uint(pi, &r, TLB_KERNEL_ASID_BITS);
  po[0] = (global || entry_asid == query_asid) != 0;
}

static inline void
tlb_kernel_core_vpn1_match_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t entry_vpn1 =
      tlb_kernel_read_uint(pi, &r, TLB_KERNEL_VPN_PART_BITS);
  const uint32_t query_vpn1 =
      tlb_kernel_read_uint(pi, &r, TLB_KERNEL_VPN_PART_BITS);
  po[0] = (entry_vpn1 == query_vpn1) != 0;
}

static inline void
tlb_kernel_core_vpn0_match_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t level = tlb_kernel_read_bit(pi, &r);
  const uint32_t entry_vpn0 =
      tlb_kernel_read_uint(pi, &r, TLB_KERNEL_VPN_PART_BITS);
  const uint32_t query_vpn0 =
      tlb_kernel_read_uint(pi, &r, TLB_KERNEL_VPN_PART_BITS);
  po[0] = (level || entry_vpn0 == query_vpn0) != 0;
}

static inline void
tlb_kernel_core_entry_match_select_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t valid = tlb_kernel_read_bit(pi, &r);
  const uint32_t asid_match = tlb_kernel_read_bit(pi, &r);
  const uint32_t vpn1_match = tlb_kernel_read_bit(pi, &r);
  const uint32_t vpn0_match = tlb_kernel_read_bit(pi, &r);
  po[0] = (valid && asid_match && vpn1_match && vpn0_match) != 0;
}

static inline void
tlb_kernel_core_page_match_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t lhs = tlb_kernel_read_uint(pi, &r, TLB_KERNEL_VPN_BITS);
  const uint32_t rhs = tlb_kernel_read_uint(pi, &r, TLB_KERNEL_VPN_BITS);
  po[0] = (lhs == rhs) != 0;
}

static inline void
tlb_kernel_core_satp_match_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t lhs = tlb_kernel_read_uint(pi, &r, 32);
  const uint32_t rhs = tlb_kernel_read_uint(pi, &r, 32);
  po[0] = (lhs == rhs) != 0;
}

static inline void
tlb_kernel_core_repl_next_io_generator(const bool *pi, bool *po, int entries) {
  TlbKernelCursor r = {0};
  const int slot_bits = tlb_kernel_slot_width(entries);
  const uint32_t capacity = entries > 0 ? (uint32_t)entries : 1u;
  const uint32_t repl_ptr =
      tlb_kernel_read_uint(pi, &r, slot_bits) & tlb_kernel_mask_u32(slot_bits);
  const uint32_t slot = capacity > 0 ? (repl_ptr % capacity) : 0u;
  const uint32_t next = capacity > 0 ? ((slot + 1u) % capacity) : 0u;
  TlbKernelCursor w = {0};
  tlb_kernel_write_uint(po, &w, slot, slot_bits);
  tlb_kernel_write_uint(po, &w, next, slot_bits);
}

static inline void
tlb_kernel_core_paddr_io_generator(const bool *pi, bool *po) {
  TlbKernelCursor r = {0};
  const uint32_t v_addr = tlb_kernel_read_uint(pi, &r, 32);
  const TlbKernelEntry entry = tlb_kernel_decode_entry(pi, &r);
  const uint32_t paddr = tlb_kernel_compose_paddr(v_addr, &entry);
  TlbKernelCursor w = {0};
  tlb_kernel_write_uint(po, &w, paddr, 32);
}

static inline void
tlb_kernel_core_hit_select_io_generator(const bool *pi, bool *po, int entries) {
  (void)entries;
  TlbKernelCursor r = {0};
  const TlbKernelLookup lookup = tlb_kernel_decode_lookup(pi, &r);
  const uint32_t refill_valid = tlb_kernel_read_bit(pi, &r);
  const TlbKernelEntry refill_entry = tlb_kernel_decode_entry(pi, &r);
  const uint32_t refill_match = tlb_kernel_read_bit(pi, &r);
  uint32_t hit_valid = 0;
  TlbKernelEntry hit;
  hit.valid = 0;
  hit.global = 0;
  hit.asid = 0;
  hit.level = 0;
  hit.vpn1 = 0;
  hit.vpn0 = 0;
  hit.ppn = 0;
  hit.perm = 0;
  if (refill_valid && refill_match) {
    hit = refill_entry;
    hit_valid = 1;
  } else if (lookup.hit) {
    hit = lookup.entry;
    hit_valid = 1;
  }
  TlbKernelCursor w = {0};
  tlb_kernel_write_bit(po, &w, hit_valid);
  tlb_kernel_encode_entry(po, &w, &hit);
}

static inline TlbKernelCoreOutput
tlb_kernel_core_output_mapper_comb(
    const TlbKernelCoreOutputMapperInput *in) {
  TlbKernelCoreOutput out;
  out.resp_valid = 0;
  out.result = 0;
  out.p_addr = 0;
  out.retry_reason = 0;
  out.walk_next = in->walk;
  out.refill_next = in->refill;
  out.walk_req.valid = 0;
  out.walk_req.vaddr = 0;
  out.walk_req.satp = 0;
  out.walk_req.access_type = 0;
  out.walk_resp_consumed = 0;
  out.trace_source = 0;
  out.trace_level = 0xff;
  out.trace_perm = 0;

  if (!in->translate_valid) {
    return out;
  }

  out.resp_valid = 1;

  if (in->eff_priv == 3u || ((in->satp & 0x80000000u) == 0u)) {
    out.p_addr = in->v_addr;
    out.trace_source = 1;
    return out;
  }

  if (in->hit_valid) {
    if (!in->hit_perm_ok) {
      out.result = 1;
      out.trace_source = 3;
      out.trace_level = in->hit_level;
      out.trace_perm = in->hit_perm & 0xffu;
      return out;
    }
    out.p_addr = in->hit_paddr;
    out.trace_source = 2;
    out.trace_level = in->hit_level;
    out.trace_perm = in->hit_perm & 0xffu;
    return out;
  }

  TlbKernelWalkRegs walk = in->walk;
  if (!walk.active) {
    walk.active = 1;
    walk.req_sent = 0;
    walk.v_addr = in->v_addr;
    walk.type = in->type;
    walk.satp = in->satp;
    walk.eff_priv = in->eff_priv;
    walk.mxr = in->mxr;
    walk.sum = in->sum;
  } else if (!in->walk_same_request) {
    out.result = 2;
    out.retry_reason = 1;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  if (!walk.req_sent) {
    if (!in->walk_req_ready) {
      out.result = 2;
      out.retry_reason = 2;
      out.trace_source = 4;
      out.walk_next = walk;
      return out;
    }
    out.walk_req.valid = 1;
    out.walk_req.vaddr = walk.v_addr;
    out.walk_req.satp = walk.satp;
    out.walk_req.access_type = walk.type;
    walk.req_sent = 1;
    out.result = 2;
    out.retry_reason = 3;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  if (!in->walk_resp_valid) {
    out.result = 2;
    out.retry_reason = 3;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  if (!in->walk_resp_page_match) {
    out.walk_resp_consumed = 1;
    out.result = 2;
    out.retry_reason = 3;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  out.walk_resp_consumed = 1;
  out.walk_next.active = 0;
  out.walk_next.req_sent = 0;
  out.walk_next.v_addr = 0;
  out.walk_next.type = 0;
  out.walk_next.satp = 0;
  out.walk_next.eff_priv = 0;
  out.walk_next.mxr = 0;
  out.walk_next.sum = 0;
  out.trace_source = 4;
  out.trace_level = in->walk_resp_leaf_level & 1u;
  out.trace_perm = in->walk_resp_perm & 0xffu;

  if (in->walk_resp_fault) {
    out.result = 1;
    return out;
  }

  if (!in->walk_resp_perm_ok) {
    out.result = 1;
    return out;
  }

  TlbKernelRefill refill;
  refill.valid = 1;
  refill.slot = in->refill_slot;
  refill.next_repl_ptr = in->refill_next_repl_ptr;
  refill.entry.valid = 1;
  refill.entry.global = (in->walk_resp_perm & TLB_KERNEL_PTE_G) != 0;
  refill.entry.asid = tlb_kernel_sv32_asid(in->satp);
  refill.entry.level = in->walk_resp_leaf_level & 1u;
  refill.entry.vpn1 = (in->v_addr >> 22) & 0x3ffu;
  refill.entry.vpn0 = (in->v_addr >> 12) & 0x3ffu;
  refill.entry.ppn = in->walk_resp_ppn & 0x3fffffu;
  refill.entry.perm = in->walk_resp_perm & 0xffu;
  out.refill_next = refill;
  out.p_addr = tlb_kernel_compose_paddr(in->v_addr, &refill.entry);
  return out;
}

static inline void
tlb_kernel_core_output_mapper_io_generator(const bool *pi, bool *po,
                                           int entries) {
  const TlbKernelCoreOutputMapperInput in =
      tlb_kernel_decode_core_output_mapper_input(pi, entries);
  const TlbKernelCoreOutput out = tlb_kernel_core_output_mapper_comb(&in);
  tlb_kernel_encode_core_output(po, &out, entries);
}

static inline void tlb_kernel_build_core_output_mapper_input(
    const TlbKernelCoreInput *in, TlbKernelCoreOutputMapperInput *mapped) {
  const uint32_t eff_priv = tlb_kernel_effective_priv(
      in->type, in->privilege, in->mstatus_mprv, in->mstatus_mpp);
  const uint32_t asid = tlb_kernel_sv32_asid(in->satp);
  const uint32_t refill_match =
      tlb_kernel_entry_matches(&in->refill.entry, in->v_addr, asid);
  TlbKernelEntry hit;
  uint32_t hit_valid = 0;
  hit.valid = 0;
  hit.global = 0;
  hit.asid = 0;
  hit.level = 0;
  hit.vpn1 = 0;
  hit.vpn0 = 0;
  hit.ppn = 0;
  hit.perm = 0;
  if (in->refill.valid && refill_match) {
    hit = in->refill.entry;
    hit_valid = 1;
  } else if (in->lookup.hit) {
    hit = in->lookup.entry;
    hit_valid = 1;
  }

  const uint32_t capacity = in->tlb_capacity > 0 ? in->tlb_capacity : 1u;
  const uint32_t refill_slot = in->repl_ptr % capacity;

  mapped->translate_valid = in->translate_valid;
  mapped->v_addr = in->v_addr;
  mapped->type = in->type;
  mapped->satp = in->satp;
  mapped->eff_priv = eff_priv;
  mapped->mxr = in->mstatus_mxr;
  mapped->sum = in->mstatus_sum;
  mapped->walk = in->walk;
  mapped->refill = in->refill;
  mapped->walk_req_ready = in->walk_req_ready;
  mapped->walk_resp_valid = in->walk_resp_valid;
  mapped->walk_resp_fault = in->walk_resp.fault;
  mapped->walk_resp_page_match =
      ((in->walk_resp.vaddr >> 12) == (in->walk.v_addr >> 12));
  mapped->walk_resp_ppn = (in->walk_resp.leaf_pte >> 10) & 0x3fffffu;
  mapped->walk_resp_perm = in->walk_resp.leaf_pte & 0xffu;
  mapped->walk_resp_leaf_level = in->walk_resp.leaf_level & 1u;
  mapped->hit_valid = hit_valid;
  mapped->hit_perm_ok =
      hit_valid &&
      tlb_kernel_check_perm(hit.perm & 0xffu, in->type, eff_priv,
                            in->mstatus_sum, in->mstatus_mxr);
  mapped->hit_paddr = tlb_kernel_compose_paddr(in->v_addr, &hit);
  mapped->hit_level = hit.level;
  mapped->hit_perm = hit.perm & 0xffu;
  mapped->walk_resp_perm_ok =
      tlb_kernel_check_perm(mapped->walk_resp_perm, in->type, eff_priv,
                            in->mstatus_sum, in->mstatus_mxr);
  mapped->walk_same_request =
      ((in->walk.v_addr >> 12) == (in->v_addr >> 12)) &&
      (in->walk.satp == in->satp);
  mapped->refill_slot = refill_slot;
  mapped->refill_next_repl_ptr = (refill_slot + 1u) % capacity;
}

static inline TlbKernelCoreOutput
tlb_kernel_core_comb(const TlbKernelCoreInput *in) {
  TlbKernelCoreOutput out;
  out.resp_valid = 0;
  out.result = 0;
  out.p_addr = 0;
  out.retry_reason = 0;
  out.walk_next = in->walk;
  out.refill_next = in->refill;
  out.walk_req.valid = 0;
  out.walk_req.vaddr = 0;
  out.walk_req.satp = 0;
  out.walk_req.access_type = 0;
  out.walk_resp_consumed = 0;
  out.trace_source = 0;
  out.trace_level = 0xff;
  out.trace_perm = 0;

  if (!in->translate_valid) {
    return out;
  }

  out.resp_valid = 1;

  uint32_t eff_priv = in->privilege & 0x3u;
  if (in->type != 0u && in->mstatus_mprv) {
    eff_priv = in->mstatus_mpp & 0x3u;
  }
  const uint32_t satp = in->satp;
  const uint32_t type = in->type;

  if (eff_priv == 3u || ((satp & 0x80000000u) == 0u)) {
    out.p_addr = in->v_addr;
    out.trace_source = 1;
    return out;
  }

  const uint32_t asid = tlb_kernel_sv32_asid(satp);
  TlbKernelEntry hit;
  uint32_t hit_valid = 0;
  if (in->refill.valid &&
      tlb_kernel_entry_matches(&in->refill.entry, in->v_addr, asid)) {
    hit = in->refill.entry;
    hit_valid = 1;
  } else if (in->lookup.hit) {
    hit = in->lookup.entry;
    hit_valid = 1;
  }

  if (hit_valid) {
    const uint32_t perm = hit.perm & 0xffu;
    if (!tlb_kernel_check_perm(perm, type, eff_priv, in->mstatus_sum,
                               in->mstatus_mxr)) {
      out.result = 1;
      out.trace_source = 3;
      out.trace_level = hit.level;
      out.trace_perm = perm;
      return out;
    }
    out.p_addr = tlb_kernel_compose_paddr(in->v_addr, &hit);
    out.trace_source = 2;
    out.trace_level = hit.level;
    out.trace_perm = perm;
    return out;
  }

  TlbKernelWalkRegs walk = in->walk;
  if (!walk.active) {
    walk.active = 1;
    walk.req_sent = 0;
    walk.v_addr = in->v_addr;
    walk.type = type;
    walk.satp = satp;
    walk.eff_priv = eff_priv;
    walk.mxr = in->mstatus_mxr;
    walk.sum = in->mstatus_sum;
  } else {
    const uint32_t walk_page = walk.v_addr >> 12;
    const uint32_t req_page = in->v_addr >> 12;
    if (walk_page != req_page || walk.satp != satp) {
      out.result = 2;
      out.retry_reason = 1;
      out.trace_source = 4;
      out.walk_next = walk;
      return out;
    }
  }

  if (!walk.req_sent) {
    if (!in->walk_req_ready) {
      out.result = 2;
      out.retry_reason = 2;
      out.trace_source = 4;
      out.walk_next = walk;
      return out;
    }
    out.walk_req.valid = 1;
    out.walk_req.vaddr = walk.v_addr;
    out.walk_req.satp = walk.satp;
    out.walk_req.access_type = walk.type;
    walk.req_sent = 1;
    out.result = 2;
    out.retry_reason = 3;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  if (!in->walk_resp_valid) {
    out.result = 2;
    out.retry_reason = 3;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  const TlbKernelWalkResp wr = in->walk_resp;
  if ((wr.vaddr >> 12) != (walk.v_addr >> 12)) {
    out.walk_resp_consumed = 1;
    out.result = 2;
    out.retry_reason = 3;
    out.trace_source = 4;
    out.walk_next = walk;
    return out;
  }

  out.walk_resp_consumed = 1;
  out.walk_next.active = 0;
  out.walk_next.req_sent = 0;
  out.walk_next.v_addr = 0;
  out.walk_next.type = 0;
  out.walk_next.satp = 0;
  out.walk_next.eff_priv = 0;
  out.walk_next.mxr = 0;
  out.walk_next.sum = 0;
  out.trace_source = 4;
  out.trace_level = wr.leaf_level;
  out.trace_perm = wr.leaf_pte & 0xffu;

  if (wr.fault) {
    out.result = 1;
    return out;
  }

  const uint32_t perm = wr.leaf_pte & 0xffu;
  if (!tlb_kernel_check_perm(perm, type, eff_priv, in->mstatus_sum,
                             in->mstatus_mxr)) {
    out.result = 1;
    return out;
  }

  const uint32_t capacity = in->tlb_capacity > 0 ? in->tlb_capacity : 1u;
  const uint32_t refill_slot = in->repl_ptr % capacity;
  TlbKernelRefill refill;
  refill.valid = 1;
  refill.slot = refill_slot;
  refill.next_repl_ptr = (refill_slot + 1u) % capacity;
  refill.entry.valid = 1;
  refill.entry.global = (wr.leaf_pte & TLB_KERNEL_PTE_G) != 0;
  refill.entry.asid = asid;
  refill.entry.level = wr.leaf_level & 1u;
  refill.entry.vpn1 = (in->v_addr >> 22) & 0x3ffu;
  refill.entry.vpn0 = (in->v_addr >> 12) & 0x3ffu;
  refill.entry.ppn = (wr.leaf_pte >> 10) & 0x3fffffu;
  refill.entry.perm = perm;
  out.refill_next = refill;
  out.p_addr = tlb_kernel_compose_paddr(in->v_addr, &refill.entry);
  return out;
}

static inline void tlb_kernel_core_io_generator(const bool *pi, bool *po,
                                                int entries) {
  const TlbKernelCoreInput in = tlb_kernel_decode_core_input(pi, entries);
  const TlbKernelCoreOutput out = tlb_kernel_core_comb(&in);
  tlb_kernel_encode_core_output(po, &out, entries);
}

static inline int tlb_kernel_top_common_pi_width(int entries) {
  const int slot_bits = tlb_kernel_slot_width(entries);
  return 1 + 1 + 32 + 2 + 1 + 1 + 1 + 2 +
         TLB_KERNEL_PACKED_WALK_REGS_WIDTH +
         (1 + TLB_KERNEL_PACKED_ENTRY_WIDTH + 2 * slot_bits) + slot_bits +
         TLB_KERNEL_PACKED_WALK_PORT_IN_WIDTH;
}

static inline int tlb_kernel_top_common_po_width(int entries);

static inline int tlb_kernel_top_control_po_width(int entries) {
  return 1 + tlb_kernel_top_acc_state_width(entries) +
         tlb_kernel_top_common_po_width(entries);
}

static inline int tlb_kernel_top_common_po_width(int entries) {
  const int slot_bits = tlb_kernel_slot_width(entries);
  return 1 + TLB_KERNEL_PACKED_WALK_REGS_WIDTH +
         (1 + TLB_KERNEL_PACKED_ENTRY_WIDTH + 2 * slot_bits) +
         TLB_KERNEL_PACKED_WALK_REQ_WIDTH + 1 + 1;
}

static inline TlbKernelTopPortInput
tlb_kernel_decode_top_port_input(const bool *bits, TlbKernelCursor *cur,
                                 int is_dtlb, int port_index,
                                 int ldu_count) {
  TlbKernelTopPortInput port;
  port.valid = tlb_kernel_read_bit(bits, cur);
  port.v_addr = tlb_kernel_read_uint(bits, cur, 32);
  port.type = is_dtlb ? ((port_index < ldu_count) ? 1u : 2u) : 0u;
  port.lookup = tlb_kernel_decode_lookup(bits, cur);
  return port;
}

static inline void tlb_kernel_encode_top_port_output(
    bool *bits, TlbKernelCursor *cur, const TlbKernelTopPortOutput *port) {
  tlb_kernel_write_bit(bits, cur, port->resp_valid);
  tlb_kernel_write_uint(bits, cur, port->result, 2);
  tlb_kernel_write_uint(bits, cur, port->p_addr, 32);
  tlb_kernel_write_uint(bits, cur, port->retry_reason, 3);
  tlb_kernel_write_uint(bits, cur, port->trace_source, 3);
  tlb_kernel_write_uint(bits, cur, port->trace_level, 8);
  tlb_kernel_write_uint(bits, cur, port->trace_perm, 8);
}

static inline TlbKernelTopAccState
tlb_kernel_decode_top_acc_state(const bool *bits, TlbKernelCursor *cur,
                                int entries) {
  TlbKernelTopAccState state;
  state.walk = tlb_kernel_decode_walk_regs(bits, cur);
  state.refill = tlb_kernel_decode_refill(bits, cur, entries);
  state.walk_req_ready = tlb_kernel_read_bit(bits, cur);
  state.walk_resp_valid = tlb_kernel_read_bit(bits, cur);
  state.walk_req = tlb_kernel_decode_walk_req(bits, cur);
  state.walk_resp_consumed = tlb_kernel_read_bit(bits, cur);
  return state;
}

static inline void tlb_kernel_encode_top_acc_state(
    bool *bits, TlbKernelCursor *cur, const TlbKernelTopAccState *state,
    int entries) {
  tlb_kernel_encode_walk_regs(bits, cur, &state->walk);
  tlb_kernel_encode_refill(bits, cur, &state->refill, entries);
  tlb_kernel_write_bit(bits, cur, state->walk_req_ready);
  tlb_kernel_write_bit(bits, cur, state->walk_resp_valid);
  tlb_kernel_encode_walk_req(bits, cur, &state->walk_req);
  tlb_kernel_write_bit(bits, cur, state->walk_resp_consumed);
}

static inline void tlb_kernel_encode_top_common_output(
    bool *bits, TlbKernelCursor *cur, uint32_t table_flush,
    const TlbKernelWalkRegs *walk, const TlbKernelRefill *refill,
    const TlbKernelWalkReq *walk_req, uint32_t walk_resp_consumed,
    uint32_t walk_client_flush, int entries) {
  tlb_kernel_write_bit(bits, cur, table_flush);
  tlb_kernel_encode_walk_regs(bits, cur, walk);
  tlb_kernel_encode_refill(bits, cur, refill, entries);
  tlb_kernel_encode_walk_req(bits, cur, walk_req);
  tlb_kernel_write_bit(bits, cur, walk_resp_consumed);
  tlb_kernel_write_bit(bits, cur, walk_client_flush);
}

static inline void
tlb_kernel_top_core_input_mapper_io_generator(const bool *pi, bool *po,
                                              int entries) {
  TlbKernelCursor r = {0};
  (void)tlb_kernel_read_bit(pi, &r);
  (void)tlb_kernel_read_bit(pi, &r);
  const uint32_t satp = tlb_kernel_read_uint(pi, &r, 32);
  const uint32_t privilege = tlb_kernel_read_uint(pi, &r, 2);
  const uint32_t mstatus_mxr = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_sum = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_mprv = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_mpp = tlb_kernel_read_uint(pi, &r, 2);
  const TlbKernelWalkRegs walk = tlb_kernel_decode_walk_regs(pi, &r);
  const TlbKernelRefill refill = tlb_kernel_decode_refill(pi, &r, entries);
  const uint32_t repl_ptr =
      tlb_kernel_read_uint(pi, &r, tlb_kernel_slot_width(entries));
  uint32_t walk_req_ready = 0;
  uint32_t walk_resp_valid = 0;
  TlbKernelWalkResp walk_resp;
  tlb_kernel_decode_walk_port_view(pi, &r, &walk_req_ready, &walk_resp_valid,
                                   &walk_resp);
  TlbKernelTopPortInput port;
  port.valid = tlb_kernel_read_bit(pi, &r);
  port.v_addr = tlb_kernel_read_uint(pi, &r, 32);
  port.type = tlb_kernel_read_uint(pi, &r, 2);
  port.lookup = tlb_kernel_decode_lookup(pi, &r);

  TlbKernelCoreInput core_in;
  core_in.translate_valid = port.valid;
  core_in.v_addr = port.v_addr;
  core_in.type = port.type;
  core_in.satp = satp;
  core_in.privilege = privilege;
  core_in.mstatus_mxr = mstatus_mxr;
  core_in.mstatus_sum = mstatus_sum;
  core_in.mstatus_mprv = mstatus_mprv;
  core_in.mstatus_mpp = mstatus_mpp;
  core_in.lookup = port.lookup;
  core_in.walk = walk;
  core_in.refill = refill;
  core_in.repl_ptr = repl_ptr;
  core_in.tlb_capacity = entries > 0 ? (uint32_t)entries : 1u;
  core_in.walk_req_ready = walk_req_ready;
  core_in.walk_resp_valid = walk_resp_valid;
  core_in.walk_resp = walk_resp;
  tlb_kernel_encode_core_input(po, &core_in, entries);
}

static inline void
tlb_kernel_top_control_io_generator(const bool *pi, bool *po, int entries) {
  TlbKernelCursor r = {0};
  const uint32_t flush = tlb_kernel_read_bit(pi, &r);
  const uint32_t cancel = tlb_kernel_read_bit(pi, &r);
  (void)tlb_kernel_read_uint(pi, &r, 32);
  (void)tlb_kernel_read_uint(pi, &r, 2);
  (void)tlb_kernel_read_bit(pi, &r);
  (void)tlb_kernel_read_bit(pi, &r);
  (void)tlb_kernel_read_bit(pi, &r);
  (void)tlb_kernel_read_uint(pi, &r, 2);
  const TlbKernelWalkRegs walk = tlb_kernel_decode_walk_regs(pi, &r);
  const TlbKernelRefill refill = tlb_kernel_decode_refill(pi, &r, entries);
  const uint32_t repl_ptr =
      tlb_kernel_read_uint(pi, &r, tlb_kernel_slot_width(entries));
  (void)repl_ptr;
  uint32_t walk_req_ready = 0;
  uint32_t walk_resp_valid = 0;
  TlbKernelWalkResp walk_resp;
  tlb_kernel_decode_walk_port_view(pi, &r, &walk_req_ready, &walk_resp_valid,
                                   &walk_resp);

  TlbKernelWalkRegs empty_walk = {0, 0, 0, 0, 0, 0, 0, 0};
  TlbKernelRefill empty_refill;
  empty_refill.valid = 0;
  empty_refill.entry.valid = 0;
  empty_refill.entry.global = 0;
  empty_refill.entry.asid = 0;
  empty_refill.entry.level = 0;
  empty_refill.entry.vpn1 = 0;
  empty_refill.entry.vpn0 = 0;
  empty_refill.entry.ppn = 0;
  empty_refill.entry.perm = 0;
  empty_refill.slot = 0;
  empty_refill.next_repl_ptr = 0;
  TlbKernelWalkReq empty_req = {0, 0, 0, 0};

  TlbKernelTopAccState state;
  state.walk = walk;
  state.refill = refill;
  state.walk_req_ready = walk_req_ready;
  state.walk_resp_valid = walk_resp_valid;
  state.walk_req = empty_req;
  state.walk_resp_consumed = 0;

  TlbKernelCursor w = {0};
  tlb_kernel_write_bit(po, &w, flush || cancel);
  tlb_kernel_encode_top_acc_state(po, &w, &state, entries);
  if (flush) {
    tlb_kernel_encode_top_common_output(po, &w, 1, &empty_walk, &empty_refill,
                                        &empty_req, 0, 1, entries);
  } else if (cancel) {
    tlb_kernel_encode_top_common_output(po, &w, 0, &empty_walk, &refill,
                                        &empty_req, 0, 1, entries);
  } else {
    tlb_kernel_encode_top_common_output(po, &w, 0, &walk, &refill, &empty_req,
                                        0, 0, entries);
  }
}

static inline void
tlb_kernel_top_accumulate_io_generator(const bool *pi, bool *po, int entries) {
  TlbKernelCursor r = {0};
  TlbKernelTopAccState state =
      tlb_kernel_decode_top_acc_state(pi, &r, entries);
  const TlbKernelCoreOutput core_out = tlb_kernel_decode_core_output(pi + r.pos,
                                                                     entries);

  TlbKernelTopPortOutput port;
  port.resp_valid = core_out.resp_valid;
  port.result = core_out.result;
  port.p_addr = core_out.p_addr;
  port.retry_reason = core_out.retry_reason;
  port.trace_source = core_out.trace_source;
  port.trace_level = core_out.trace_level;
  port.trace_perm = core_out.trace_perm;

  state.walk = core_out.walk_next;
  state.refill = core_out.refill_next;
  if (core_out.walk_req.valid) {
    state.walk_req = core_out.walk_req;
    state.walk_req_ready = 0;
    state.walk_resp_valid = 0;
  }
  if (core_out.walk_resp_consumed) {
    state.walk_resp_consumed = 1;
    state.walk_resp_valid = 0;
  }

  TlbKernelCursor w = {0};
  tlb_kernel_encode_top_acc_state(po, &w, &state, entries);
  tlb_kernel_encode_top_port_output(po, &w, &port);
}

static inline void tlb_kernel_top_post_io_generator(const bool *pi, bool *po,
                                                    int entries) {
  TlbKernelCursor r = {0};
  const TlbKernelTopAccState state =
      tlb_kernel_decode_top_acc_state(pi, &r, entries);
  TlbKernelCursor w = {0};
  tlb_kernel_encode_top_common_output(po, &w, 0, &state.walk, &state.refill,
                                      &state.walk_req,
                                      state.walk_resp_consumed, 0, entries);
}

static inline void tlb_kernel_top_io_generator(const bool *pi, bool *po,
                                               int entries, int port_count,
                                               int ldu_count, int is_dtlb) {
  TlbKernelCursor r = {0};
  const uint32_t flush = tlb_kernel_read_bit(pi, &r);
  const uint32_t cancel = tlb_kernel_read_bit(pi, &r);
  const uint32_t satp = tlb_kernel_read_uint(pi, &r, 32);
  const uint32_t privilege = tlb_kernel_read_uint(pi, &r, 2);
  const uint32_t mstatus_mxr = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_sum = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_mprv = tlb_kernel_read_bit(pi, &r);
  const uint32_t mstatus_mpp = tlb_kernel_read_uint(pi, &r, 2);
  TlbKernelWalkRegs shadow_walk = tlb_kernel_decode_walk_regs(pi, &r);
  TlbKernelRefill shadow_refill = tlb_kernel_decode_refill(pi, &r, entries);
  const uint32_t repl_ptr =
      tlb_kernel_read_uint(pi, &r, tlb_kernel_slot_width(entries));
  uint32_t shadow_walk_req_ready = 0;
  uint32_t shadow_walk_resp_valid = 0;
  TlbKernelWalkResp shadow_walk_resp;
  tlb_kernel_decode_walk_port_view(pi, &r, &shadow_walk_req_ready,
                                   &shadow_walk_resp_valid,
                                   &shadow_walk_resp);

  TlbKernelTopPortInput ports[16];
  for (int i = 0; i < port_count; ++i) {
    ports[i] =
        tlb_kernel_decode_top_port_input(pi, &r, is_dtlb, i, ldu_count);
  }

  TlbKernelCursor w = {0};
  if (flush) {
    TlbKernelWalkRegs empty_walk = {0, 0, 0, 0, 0, 0, 0, 0};
    TlbKernelRefill empty_refill;
    empty_refill.valid = 0;
    empty_refill.entry.valid = 0;
    empty_refill.entry.global = 0;
    empty_refill.entry.asid = 0;
    empty_refill.entry.level = 0;
    empty_refill.entry.vpn1 = 0;
    empty_refill.entry.vpn0 = 0;
    empty_refill.entry.ppn = 0;
    empty_refill.entry.perm = 0;
    empty_refill.slot = 0;
    empty_refill.next_repl_ptr = 0;
    TlbKernelWalkReq empty_req = {0, 0, 0, 0};
    tlb_kernel_write_bit(po, &w, 1);
    tlb_kernel_encode_walk_regs(po, &w, &empty_walk);
    tlb_kernel_encode_refill(po, &w, &empty_refill, entries);
    tlb_kernel_encode_walk_req(po, &w, &empty_req);
    tlb_kernel_write_bit(po, &w, 0);
    tlb_kernel_write_bit(po, &w, 1);
    for (int i = 0; i < port_count; ++i) {
      TlbKernelTopPortOutput port = {0, 0, 0, 0, 0, 0xff, 0};
      tlb_kernel_encode_top_port_output(po, &w, &port);
    }
    return;
  }

  if (cancel) {
    TlbKernelWalkRegs empty_walk = {0, 0, 0, 0, 0, 0, 0, 0};
    TlbKernelWalkReq empty_req = {0, 0, 0, 0};
    tlb_kernel_write_bit(po, &w, 0);
    tlb_kernel_encode_walk_regs(po, &w, &empty_walk);
    tlb_kernel_encode_refill(po, &w, &shadow_refill, entries);
    tlb_kernel_encode_walk_req(po, &w, &empty_req);
    tlb_kernel_write_bit(po, &w, 0);
    tlb_kernel_write_bit(po, &w, 1);
    for (int i = 0; i < port_count; ++i) {
      TlbKernelTopPortOutput port = {0, 0, 0, 0, 0, 0xff, 0};
      tlb_kernel_encode_top_port_output(po, &w, &port);
    }
    return;
  }

  uint32_t out_walk_req_valid = 0;
  TlbKernelWalkReq out_walk_req = {0, 0, 0, 0};
  uint32_t out_walk_resp_consumed = 0;
  TlbKernelTopPortOutput out_ports[16];

  for (int i = 0; i < port_count; ++i) {
    TlbKernelCoreInput core_in;
    core_in.translate_valid = ports[i].valid;
    core_in.v_addr = ports[i].v_addr;
    core_in.type = ports[i].type;
    core_in.satp = satp;
    core_in.privilege = privilege;
    core_in.mstatus_mxr = mstatus_mxr;
    core_in.mstatus_sum = mstatus_sum;
    core_in.mstatus_mprv = mstatus_mprv;
    core_in.mstatus_mpp = mstatus_mpp;
    core_in.lookup = ports[i].lookup;
    core_in.walk = shadow_walk;
    core_in.refill = shadow_refill;
    core_in.repl_ptr = repl_ptr;
    core_in.tlb_capacity = entries > 0 ? (uint32_t)entries : 1u;
    core_in.walk_req_ready = shadow_walk_req_ready;
    core_in.walk_resp_valid = shadow_walk_resp_valid;
    core_in.walk_resp = shadow_walk_resp;

    const TlbKernelCoreOutput core_out = tlb_kernel_core_comb(&core_in);
    out_ports[i].resp_valid = core_out.resp_valid;
    out_ports[i].result = core_out.result;
    out_ports[i].p_addr = core_out.p_addr;
    out_ports[i].retry_reason = core_out.retry_reason;
    out_ports[i].trace_source = core_out.trace_source;
    out_ports[i].trace_level = core_out.trace_level;
    out_ports[i].trace_perm = core_out.trace_perm;

    shadow_walk = core_out.walk_next;
    shadow_refill = core_out.refill_next;
    if (core_out.walk_req.valid) {
      out_walk_req_valid = 1;
      out_walk_req = core_out.walk_req;
      shadow_walk_req_ready = 0;
      shadow_walk_resp_valid = 0;
    }
    if (core_out.walk_resp_consumed) {
      out_walk_resp_consumed = 1;
      shadow_walk_resp_valid = 0;
    }
  }

  tlb_kernel_write_bit(po, &w, 0);
  tlb_kernel_encode_walk_regs(po, &w, &shadow_walk);
  tlb_kernel_encode_refill(po, &w, &shadow_refill, entries);
  out_walk_req.valid = out_walk_req_valid;
  tlb_kernel_encode_walk_req(po, &w, &out_walk_req);
  tlb_kernel_write_bit(po, &w, out_walk_resp_consumed);
  tlb_kernel_write_bit(po, &w, 0);
  for (int i = 0; i < port_count; ++i) {
    tlb_kernel_encode_top_port_output(po, &w, &out_ports[i]);
  }
}
