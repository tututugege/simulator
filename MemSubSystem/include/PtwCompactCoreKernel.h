#ifndef PTW_COMPACT_CORE_KERNEL_H
#define PTW_COMPACT_CORE_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PTW_COMPACT_CORE_KERNEL_TOP_EVENT_SLOTS
#define PTW_COMPACT_CORE_KERNEL_TOP_EVENT_SLOTS 4
#endif

#ifndef PTE_V
#define PTE_V (1u << 0)
#endif
#ifndef PTE_R
#define PTE_R (1u << 1)
#endif
#ifndef PTE_W
#define PTE_W (1u << 2)
#endif
#ifndef PTE_X
#define PTE_X (1u << 3)
#endif

enum {
  PTW_COMPACT_CORE_KERNEL_REQ_ID_WIDTH = (int)(sizeof(size_t) * 8),
  PTW_COMPACT_CORE_KERNEL_EVENT_SLOTS = 1,
  PTW_COMPACT_CORE_KERNEL_CORE_STATE_WIDTH = 28,
  PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_STATE_WIDTH = 63,
  PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_STATE_WIDTH = 77,
  PTW_COMPACT_CORE_KERNEL_STATE_WIDTH =
      PTW_COMPACT_CORE_KERNEL_CORE_STATE_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_STATE_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_STATE_WIDTH,
  PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_IN_WIDTH = 32,
  PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_IN_WIDTH = 45,
  PTW_COMPACT_CORE_KERNEL_GRANT_IN_WIDTH = 3,
  PTW_COMPACT_CORE_KERNEL_EVENT_IN_WIDTH = 36,
  PTW_COMPACT_CORE_KERNEL_WAKEUP_IN_WIDTH = 3,
  PTW_COMPACT_CORE_KERNEL_PI_WIDTH =
      PTW_COMPACT_CORE_KERNEL_STATE_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_IN_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_GRANT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_EVENT_SLOTS *
          PTW_COMPACT_CORE_KERNEL_EVENT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_WAKEUP_IN_WIDTH,
  PTW_COMPACT_CORE_KERNEL_PO_WIDTH = PTW_COMPACT_CORE_KERNEL_STATE_WIDTH
};

enum {
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_IN_WIDTH = 32,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_IN_WIDTH = 45,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_GRANT_IN_WIDTH =
      3 + PTW_COMPACT_CORE_KERNEL_REQ_ID_WIDTH,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_SLOTS = 1,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_IN_WIDTH =
      35 + PTW_COMPACT_CORE_KERNEL_REQ_ID_WIDTH,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_WAKEUP_IN_WIDTH = 3,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_IN_WIDTH =
      2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_IN_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_GRANT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_SLOTS *
          PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_WAKEUP_IN_WIDTH,

  PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_OUT_WIDTH = 34,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_OUT_WIDTH = 75,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_ARB_OUT_WIDTH = 99,
  PTW_COMPACT_CORE_KERNEL_CORE_PORT_OUT_WIDTH =
      2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_OUT_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_OUT_WIDTH +
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_ARB_OUT_WIDTH,

  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_MEM_CLIENT_IN_WIDTH = 34,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_WALK_CLIENT_IN_WIDTH = 99,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_GRANT_IN_WIDTH =
      3 + PTW_COMPACT_CORE_KERNEL_REQ_ID_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_COUNT_WIDTH = 8,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_IN_WIDTH =
      69 + PTW_COMPACT_CORE_KERNEL_REQ_ID_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_WAKEUP_IN_WIDTH = 3,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_INPUT_WIDTH =
      2 * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_MEM_CLIENT_IN_WIDTH +
      2 * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_WALK_CLIENT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_GRANT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_COUNT_WIDTH +
      PTW_COMPACT_CORE_KERNEL_TOP_EVENT_SLOTS *
          PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_WAKEUP_IN_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_OUTPUT_WIDTH =
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_OUT_WIDTH,

  PTW_COMPACT_CORE_KERNEL_COMPACT_VISIBLE_PI_WIDTH =
      PTW_COMPACT_CORE_KERNEL_PO_WIDTH,
  PTW_COMPACT_CORE_KERNEL_COMPACT_VISIBLE_PO_WIDTH =
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_OUT_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_GLUE_PI_WIDTH =
      PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_INPUT_WIDTH +
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_OUT_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_GLUE_PO_WIDTH =
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_IN_WIDTH +
      PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_OUTPUT_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_GLUE_CORE_IN_BASE = 0,
  PTW_COMPACT_CORE_KERNEL_TOP_GLUE_EXTERNAL_OUT_BASE =
      PTW_COMPACT_CORE_KERNEL_CORE_PORT_IN_WIDTH,
  PTW_COMPACT_CORE_KERNEL_TOP_GLUE_EXTERNAL_IN_BASE = 0,
  PTW_COMPACT_CORE_KERNEL_TOP_GLUE_CORE_OUT_BASE =
      PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_INPUT_WIDTH
};

enum {
  PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB = 0,
  PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB = 1,
  PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT = 2
};

enum {
  PTW_COMPACT_CORE_KERNEL_WALK_IDLE = 0,
  PTW_COMPACT_CORE_KERNEL_WALK_L1_REQ = 1,
  PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP = 2,
  PTW_COMPACT_CORE_KERNEL_WALK_L2_REQ = 3,
  PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP = 4
};

enum {
  PTW_COMPACT_CORE_KERNEL_OWNER_NONE = 0,
  PTW_COMPACT_CORE_KERNEL_OWNER_MEM_DTLB = 1,
  PTW_COMPACT_CORE_KERNEL_OWNER_MEM_ITLB = 2,
  PTW_COMPACT_CORE_KERNEL_OWNER_WALK = 3
};

typedef struct {
  bool req_pending;
  bool req_inflight;
  uint32_t req_addr;
  bool resp_valid;
  uint32_t resp_data;
} PtwCompactKernelMemClientState;

typedef struct {
  uint32_t vaddr;
  uint32_t satp;
  uint8_t access_type;
} PtwCompactKernelWalkReq;

typedef struct {
  bool fault;
  uint32_t vaddr;
  uint32_t leaf_pte;
  uint8_t leaf_level;
} PtwCompactKernelWalkResp;

typedef struct {
  bool req_pending;
  bool req_inflight;
  PtwCompactKernelWalkReq req;
  bool resp_valid;
  PtwCompactKernelWalkResp resp;
} PtwCompactKernelWalkClientState;

typedef struct {
  PtwCompactKernelMemClientState mem_clients[2];
  PtwCompactKernelWalkClientState walk_clients[2];
  uint8_t walk_state;
  bool walk_active;
  uint8_t walk_owner;
  uint8_t walk_rr_next;
  uint32_t walk_l1_pte;
  uint32_t walk_drop_resp_credit;
  bool walk_req_id_valid;
  size_t walk_req_id;
} PtwCompactKernelState;

typedef struct {
  bool req_valid;
  uint32_t req_addr;
  bool resp_consumed;
} PtwCompactKernelMemClientIn;

typedef struct {
  bool req_valid;
  PtwCompactKernelWalkReq req;
  bool resp_consumed;
} PtwCompactKernelWalkClientIn;

typedef struct {
  PtwCompactKernelMemClientIn mem_clients[2];
  PtwCompactKernelWalkClientIn walk_clients[2];
} PtwCompactKernelPortIn;

typedef struct {
  bool walk_client_flush[2];
} PtwCompactKernelSeqIn;

typedef struct {
  bool valid;
  uint8_t owner;
  uint32_t data;
  uint8_t replay;
  size_t req_id;
} PtwCompactKernelEventIn;

typedef struct {
  bool grant_valid;
  uint8_t grant_owner;
  size_t grant_req_id;
  PtwCompactKernelEventIn events[PTW_COMPACT_CORE_KERNEL_EVENT_SLOTS];
  uint8_t event_count;
  bool wakeup_dtlb;
  bool wakeup_itlb;
  bool wakeup_walk;
} PtwCompactKernelFeedbackIn;

static inline unsigned ptw_kernel_read_uint(const bool *bits, int lsb,
                                            int width) {
  unsigned value = 0;
  for (int i = 0; i < width && i < 32; i++) {
    if (bits[lsb + i]) {
      value |= 1u << i;
    }
  }
  return value;
}

static inline bool ptw_kernel_read_bit(const bool *bits, int bit) {
  return bits[bit];
}

static inline void ptw_kernel_write_uint(bool *bits, int lsb, int width,
                                         unsigned value) {
  for (int i = 0; i < width; i++) {
    bits[lsb + i] = ((value >> i) & 1u) != 0u;
  }
}

static inline void ptw_kernel_write_bit(bool *bits, int bit, bool value) {
  bits[bit] = value;
}

static inline void ptw_kernel_clear_bits(bool *bits, int width) {
  for (int i = 0; i < width; i++) {
    bits[i] = false;
  }
}

static inline void ptw_kernel_copy_bits(bool *dst, int dst_lsb,
                                        const bool *src, int src_lsb,
                                        int width) {
  for (int i = 0; i < width; i++) {
    dst[dst_lsb + i] = src[src_lsb + i];
  }
}

static inline int ptw_kernel_state_mem_base(int client) {
  return PTW_COMPACT_CORE_KERNEL_CORE_STATE_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_STATE_WIDTH;
}

static inline int ptw_kernel_state_walk_base(int client) {
  return PTW_COMPACT_CORE_KERNEL_CORE_STATE_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_STATE_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_STATE_WIDTH;
}

static inline int ptw_kernel_pi_mem_in_base(int client) {
  return PTW_COMPACT_CORE_KERNEL_STATE_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_pi_walk_in_base(int client) {
  return PTW_COMPACT_CORE_KERNEL_STATE_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_IN_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_pi_grant_base(void) {
  return PTW_COMPACT_CORE_KERNEL_STATE_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_IN_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_pi_event_base(int slot) {
  return ptw_kernel_pi_grant_base() +
         PTW_COMPACT_CORE_KERNEL_GRANT_IN_WIDTH +
         slot * PTW_COMPACT_CORE_KERNEL_EVENT_IN_WIDTH;
}

static inline int ptw_kernel_pi_wakeup_base(void) {
  return ptw_kernel_pi_grant_base() +
         PTW_COMPACT_CORE_KERNEL_GRANT_IN_WIDTH +
         PTW_COMPACT_CORE_KERNEL_EVENT_SLOTS *
             PTW_COMPACT_CORE_KERNEL_EVENT_IN_WIDTH;
}

static inline int ptw_kernel_core_port_mem_in_base(int client) {
  return client * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_core_port_walk_in_base(int client) {
  return 2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_IN_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_core_port_grant_in_base(void) {
  return 2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_IN_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_core_port_event_in_base(int slot) {
  return ptw_kernel_core_port_grant_in_base() +
         PTW_COMPACT_CORE_KERNEL_CORE_PORT_GRANT_IN_WIDTH +
         slot * PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_IN_WIDTH;
}

static inline int ptw_kernel_core_port_wakeup_in_base(void) {
  return ptw_kernel_core_port_grant_in_base() +
         PTW_COMPACT_CORE_KERNEL_CORE_PORT_GRANT_IN_WIDTH +
         PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_SLOTS *
             PTW_COMPACT_CORE_KERNEL_CORE_PORT_EVENT_IN_WIDTH;
}

static inline int ptw_kernel_core_port_mem_out_base(int client) {
  return client * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_OUT_WIDTH;
}

static inline int ptw_kernel_core_port_walk_out_base(int client) {
  return 2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_OUT_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_OUT_WIDTH;
}

static inline int ptw_kernel_core_port_arb_out_base(void) {
  return 2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_MEM_CLIENT_OUT_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_CORE_PORT_WALK_CLIENT_OUT_WIDTH;
}

static inline int ptw_kernel_top_external_mem_in_base(int client) {
  return client * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_MEM_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_top_external_walk_in_base(int client) {
  return 2 * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_MEM_CLIENT_IN_WIDTH +
         client * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_WALK_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_top_external_grant_in_base(void) {
  return 2 * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_MEM_CLIENT_IN_WIDTH +
         2 * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_WALK_CLIENT_IN_WIDTH;
}

static inline int ptw_kernel_top_external_event_count_base(void) {
  return ptw_kernel_top_external_grant_in_base() +
         PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_GRANT_IN_WIDTH;
}

static inline int ptw_kernel_top_external_event_in_base(int slot) {
  return ptw_kernel_top_external_event_count_base() +
         PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_COUNT_WIDTH +
         slot * PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_IN_WIDTH;
}

static inline int ptw_kernel_top_external_wakeup_in_base(void) {
  return ptw_kernel_top_external_event_count_base() +
         PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_COUNT_WIDTH +
         PTW_COMPACT_CORE_KERNEL_TOP_EVENT_SLOTS *
             PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_EVENT_IN_WIDTH;
}

static inline bool ptw_kernel_walk_state_waits(uint8_t state) {
  return state == PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP ||
         state == PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP;
}

static inline uint32_t ptw_kernel_read_word_addr(const bool *bits, int lsb) {
  return ptw_kernel_read_uint(bits, lsb, 30) << 2;
}

static inline void ptw_kernel_write_word_addr(bool *bits, int lsb,
                                              uint32_t addr) {
  ptw_kernel_write_uint(bits, lsb, 30, addr >> 2);
}

static inline uint32_t ptw_kernel_pack_pte_payload(uint32_t pte) {
  return (((pte >> 10) & 0x3FFFFFu) << 8) | (pte & 0xFFu);
}

static inline uint32_t ptw_kernel_unpack_pte_payload(uint32_t payload) {
  const uint32_t ppn = (payload >> 8) & 0x3FFFFFu;
  const uint32_t perm = payload & 0xFFu;
  return (ppn << 10) | perm;
}

static inline void ptw_kernel_reset_state(PtwCompactKernelState *state) {
  memset(state, 0, sizeof(*state));
  state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_IDLE;
  state->walk_owner = PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
  state->walk_rr_next = PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
}

static inline void ptw_kernel_decode_state(const bool *pi,
                                           PtwCompactKernelState *state) {
  ptw_kernel_reset_state(state);
  state->walk_state = (uint8_t)ptw_kernel_read_uint(pi, 0, 3);
  state->walk_active =
      state->walk_state != PTW_COMPACT_CORE_KERNEL_WALK_IDLE;
  state->walk_rr_next = ptw_kernel_read_bit(pi, 3)
                            ? PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB
                            : PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
  state->walk_req_id_valid =
      ptw_kernel_walk_state_waits(state->walk_state);
  state->walk_req_id = state->walk_req_id_valid ? (size_t)1 : (size_t)0;
  state->walk_l1_pte = ptw_kernel_read_uint(pi, 4, 22) << 10;
  state->walk_drop_resp_credit = ptw_kernel_read_uint(pi, 26, 2);

  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    const int mem_base = ptw_kernel_state_mem_base(i);
    PtwCompactKernelMemClientState *mem = &state->mem_clients[i];
    const unsigned mem_req_state =
        ptw_kernel_read_uint(pi, mem_base + 0, 2);
    mem->req_pending = (mem_req_state & 1u) != 0u;
    mem->req_inflight = (mem_req_state & 2u) != 0u;
    mem->req_addr = ptw_kernel_read_word_addr(pi, mem_base + 2);
    mem->resp_valid = ptw_kernel_read_bit(pi, mem_base + 32);
    mem->resp_data = ptw_kernel_unpack_pte_payload(
        ptw_kernel_read_uint(pi, mem_base + 33, 30));

    const int walk_base = ptw_kernel_state_walk_base(i);
    PtwCompactKernelWalkClientState *walk = &state->walk_clients[i];
    const unsigned walk_req_state =
        ptw_kernel_read_uint(pi, walk_base + 0, 2);
    const uint32_t walk_vpn =
        ptw_kernel_read_uint(pi, walk_base + 2, 20);
    const uint32_t walk_root_ppn =
        ptw_kernel_read_uint(pi, walk_base + 22, 22);
    const uint32_t walk_leaf_ppn =
        ptw_kernel_read_uint(pi, walk_base + 46, 22);
    const uint32_t walk_leaf_perm =
        ptw_kernel_read_uint(pi, walk_base + 68, 8);
    walk->req_pending = (walk_req_state & 1u) != 0u;
    walk->req_inflight = (walk_req_state & 2u) != 0u;
    walk->req.vaddr = walk_vpn << 12;
    walk->req.satp = walk_root_ppn;
    walk->req.access_type = 0;
    walk->resp_valid = ptw_kernel_read_bit(pi, walk_base + 44);
    walk->resp.fault = ptw_kernel_read_bit(pi, walk_base + 45);
    walk->resp.vaddr = walk->req.vaddr;
    walk->resp.leaf_pte = (walk_leaf_ppn << 10) | walk_leaf_perm;
    walk->resp.leaf_level =
        ptw_kernel_read_bit(pi, walk_base + 76) ? 1 : 0;
  }

  if (state->walk_active &&
      state->walk_clients[PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB].req_inflight) {
    state->walk_owner = PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB;
  } else {
    state->walk_owner = PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
  }
}

static inline void ptw_kernel_encode_state(bool *po,
                                           const PtwCompactKernelState *state) {
  ptw_kernel_write_uint(po, 0, 3, state->walk_state);
  ptw_kernel_write_bit(po, 3,
                       state->walk_rr_next !=
                           PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB);
  ptw_kernel_write_uint(po, 4, 22,
                        (state->walk_l1_pte >> 10) & 0x3FFFFFu);
  ptw_kernel_write_uint(po, 26, 2,
                        state->walk_drop_resp_credit > 3
                            ? 3
                            : state->walk_drop_resp_credit);

  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    const int mem_base = ptw_kernel_state_mem_base(i);
    const PtwCompactKernelMemClientState *mem = &state->mem_clients[i];
    const unsigned mem_req_state = (mem->req_pending ? 1u : 0u) |
                                   (mem->req_inflight ? 2u : 0u);
    ptw_kernel_write_uint(po, mem_base + 0, 2, mem_req_state);
    ptw_kernel_write_word_addr(po, mem_base + 2, mem->req_addr);
    ptw_kernel_write_bit(po, mem_base + 32, mem->resp_valid);
    ptw_kernel_write_uint(po, mem_base + 33, 30,
                          ptw_kernel_pack_pte_payload(mem->resp_data));

    const int walk_base = ptw_kernel_state_walk_base(i);
    const PtwCompactKernelWalkClientState *walk = &state->walk_clients[i];
    const unsigned walk_req_state = (walk->req_pending ? 1u : 0u) |
                                    (walk->req_inflight ? 2u : 0u);
    const uint32_t walk_vpn = (walk->req.vaddr >> 12) & 0xFFFFFu;
    const uint32_t root_ppn = walk->req.satp & 0x3FFFFFu;
    const uint32_t leaf_ppn = (walk->resp.leaf_pte >> 10) & 0x3FFFFFu;
    const uint32_t leaf_perm = walk->resp.leaf_pte & 0xFFu;
    ptw_kernel_write_uint(po, walk_base + 0, 2, walk_req_state);
    ptw_kernel_write_uint(po, walk_base + 2, 20, walk_vpn);
    ptw_kernel_write_uint(po, walk_base + 22, 22, root_ppn);
    ptw_kernel_write_bit(po, walk_base + 44, walk->resp_valid);
    ptw_kernel_write_bit(po, walk_base + 45, walk->resp.fault);
    ptw_kernel_write_uint(po, walk_base + 46, 22, leaf_ppn);
    ptw_kernel_write_uint(po, walk_base + 68, 8, leaf_perm);
    ptw_kernel_write_bit(po, walk_base + 76,
                         (walk->resp.leaf_level & 1u) != 0u);
  }
}

static inline void ptw_kernel_decode_input(const bool *pi,
                                           PtwCompactKernelState *state,
                                           PtwCompactKernelPortIn *port_in,
                                           PtwCompactKernelSeqIn *seq_in,
                                           PtwCompactKernelFeedbackIn *fb) {
  ptw_kernel_decode_state(pi, state);
  memset(port_in, 0, sizeof(*port_in));
  memset(seq_in, 0, sizeof(*seq_in));
  memset(fb, 0, sizeof(*fb));

  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    const int mem_base = ptw_kernel_pi_mem_in_base(i);
    port_in->mem_clients[i].req_valid = ptw_kernel_read_bit(pi, mem_base + 0);
    port_in->mem_clients[i].req_addr =
        ptw_kernel_read_word_addr(pi, mem_base + 1);
    port_in->mem_clients[i].resp_consumed =
        ptw_kernel_read_bit(pi, mem_base + 31);

    const int walk_base = ptw_kernel_pi_walk_in_base(i);
    port_in->walk_clients[i].req_valid =
        ptw_kernel_read_bit(pi, walk_base + 0);
    port_in->walk_clients[i].req.vaddr =
        ptw_kernel_read_uint(pi, walk_base + 1, 20) << 12;
    port_in->walk_clients[i].req.satp =
        ptw_kernel_read_uint(pi, walk_base + 21, 22);
    port_in->walk_clients[i].req.access_type = 0;
    port_in->walk_clients[i].resp_consumed =
        ptw_kernel_read_bit(pi, walk_base + 43);
    seq_in->walk_client_flush[i] =
        ptw_kernel_read_bit(pi, walk_base + 44);
  }

  const int grant_base = ptw_kernel_pi_grant_base();
  fb->grant_valid = ptw_kernel_read_bit(pi, grant_base + 0);
  fb->grant_owner = (uint8_t)ptw_kernel_read_uint(pi, grant_base + 1, 2);
  fb->grant_req_id = 1;

  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_EVENT_SLOTS; i++) {
    const int event_base = ptw_kernel_pi_event_base(i);
    PtwCompactKernelEventIn *evt = &fb->events[i];
    evt->valid = ptw_kernel_read_bit(pi, event_base + 0);
    evt->owner = (uint8_t)ptw_kernel_read_uint(pi, event_base + 1, 2);
    evt->data = ptw_kernel_unpack_pte_payload(
        ptw_kernel_read_uint(pi, event_base + 3, 30));
    evt->replay = (uint8_t)ptw_kernel_read_uint(pi, event_base + 33, 2);
    evt->req_id = ptw_kernel_read_bit(pi, event_base + 35)
                      ? state->walk_req_id
                      : (state->walk_req_id ^ (size_t)1);
    if (evt->valid) {
      fb->event_count = 1;
    }
  }

  const int wake_base = ptw_kernel_pi_wakeup_base();
  fb->wakeup_dtlb = ptw_kernel_read_bit(pi, wake_base + 0);
  fb->wakeup_itlb = ptw_kernel_read_bit(pi, wake_base + 1);
  fb->wakeup_walk = ptw_kernel_read_bit(pi, wake_base + 2);
}

static inline void ptw_kernel_send_mem_req(PtwCompactKernelState *state,
                                           uint8_t client, uint32_t paddr) {
  PtwCompactKernelMemClientState *s = &state->mem_clients[client];
  s->resp_valid = false;
  if (s->req_pending || s->req_inflight) {
    return;
  }
  s->req_pending = true;
  s->req_addr = paddr;
}

static inline void ptw_kernel_send_walk_req(PtwCompactKernelState *state,
                                            uint8_t client,
                                            PtwCompactKernelWalkReq req) {
  PtwCompactKernelWalkClientState *s = &state->walk_clients[client];
  s->resp_valid = false;
  if (s->req_pending || s->req_inflight) {
    return;
  }
  s->req_pending = true;
  s->req = req;
}

static inline void ptw_kernel_apply_port_inputs(
    PtwCompactKernelState *state, const PtwCompactKernelPortIn *in) {
  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    if (in->mem_clients[i].resp_consumed) {
      state->mem_clients[i].resp_valid = false;
    }
    if (in->mem_clients[i].req_valid) {
      ptw_kernel_send_mem_req(state, (uint8_t)i, in->mem_clients[i].req_addr);
    }
    if (in->walk_clients[i].resp_consumed) {
      state->walk_clients[i].resp_valid = false;
    }
    if (in->walk_clients[i].req_valid) {
      ptw_kernel_send_walk_req(state, (uint8_t)i, in->walk_clients[i].req);
    }
  }
}

static inline void ptw_kernel_select_walk_owner(PtwCompactKernelState *state) {
  if (state->walk_active ||
      state->walk_state != PTW_COMPACT_CORE_KERNEL_WALK_IDLE) {
    return;
  }

  const uint8_t first = state->walk_rr_next;
  const uint8_t second = first == PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB
                             ? PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB
                             : PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
  uint8_t owner = 0xFFu;
  if (state->walk_clients[first].req_pending) {
    owner = first;
  } else if (state->walk_clients[second].req_pending) {
    owner = second;
  } else {
    return;
  }

  PtwCompactKernelWalkClientState *wc = &state->walk_clients[owner];
  wc->req_pending = false;
  wc->req_inflight = true;
  state->walk_owner = owner;
  state->walk_active = true;
  state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L1_REQ;
  state->walk_l1_pte = 0;
  state->walk_rr_next = owner == PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB
                            ? PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB
                            : PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
}

static inline void ptw_kernel_on_mem_read_granted(
    PtwCompactKernelState *state, uint8_t client) {
  PtwCompactKernelMemClientState *s = &state->mem_clients[client];
  s->req_pending = false;
  s->req_inflight = true;
}

static inline void ptw_kernel_on_walk_read_granted(
    PtwCompactKernelState *state, size_t req_id) {
  if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L1_REQ) {
    state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP;
  } else if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L2_REQ) {
    state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP;
  }
  state->walk_req_id_valid = true;
  state->walk_req_id = req_id;
}

static inline void ptw_kernel_apply_grant(PtwCompactKernelState *state,
                                          const PtwCompactKernelFeedbackIn *fb) {
  if (!fb->grant_valid) {
    return;
  }
  switch (fb->grant_owner) {
  case PTW_COMPACT_CORE_KERNEL_OWNER_MEM_DTLB:
    ptw_kernel_on_mem_read_granted(state, PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB);
    break;
  case PTW_COMPACT_CORE_KERNEL_OWNER_MEM_ITLB:
    ptw_kernel_on_mem_read_granted(state, PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB);
    break;
  case PTW_COMPACT_CORE_KERNEL_OWNER_WALK:
    ptw_kernel_on_walk_read_granted(state, fb->grant_req_id);
    break;
  default:
    break;
  }
}

static inline void ptw_kernel_on_mem_resp_client(
    PtwCompactKernelState *state, uint8_t client, uint32_t data) {
  PtwCompactKernelMemClientState *s = &state->mem_clients[client];
  s->resp_valid = true;
  s->resp_data = data;
  s->req_inflight = false;
}

static inline void ptw_kernel_publish_walk_fault(PtwCompactKernelState *state) {
  PtwCompactKernelWalkClientState *wc =
      &state->walk_clients[state->walk_owner];
  wc->req_inflight = false;
  wc->resp_valid = true;
  wc->resp.fault = true;
  wc->resp.vaddr = wc->req.vaddr;
  wc->resp.leaf_pte = 0;
  wc->resp.leaf_level = 0;
  state->walk_active = false;
  state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_IDLE;
  state->walk_req_id_valid = false;
  state->walk_req_id = 0;
}

static inline void ptw_kernel_publish_walk_leaf(PtwCompactKernelState *state,
                                                uint32_t pte,
                                                uint8_t leaf_level) {
  PtwCompactKernelWalkClientState *wc =
      &state->walk_clients[state->walk_owner];
  wc->req_inflight = false;
  wc->resp_valid = true;
  wc->resp.fault = false;
  wc->resp.vaddr = wc->req.vaddr;
  wc->resp.leaf_pte = pte;
  wc->resp.leaf_level = leaf_level;
  state->walk_active = false;
  state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_IDLE;
  state->walk_req_id_valid = false;
  state->walk_req_id = 0;
}

static inline void ptw_kernel_on_walk_mem_resp(PtwCompactKernelState *state,
                                               size_t req_id, uint32_t pte) {
  const bool is_active_req =
      state->walk_active && state->walk_req_id_valid &&
      (state->walk_req_id == req_id);
  if (state->walk_drop_resp_credit > 0 && !is_active_req) {
    state->walk_drop_resp_credit--;
    return;
  }
  if (!state->walk_active || !state->walk_req_id_valid || !is_active_req) {
    return;
  }

  const bool v = (pte & PTE_V) != 0;
  const bool r = (pte & PTE_R) != 0;
  const bool w = (pte & PTE_W) != 0;
  const bool x = (pte & PTE_X) != 0;

  if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP) {
    if (!v || (!r && w)) {
      ptw_kernel_publish_walk_fault(state);
    } else if (r || x) {
      if (((pte >> 10) & 0x3FFu) != 0) {
        ptw_kernel_publish_walk_fault(state);
      } else {
        ptw_kernel_publish_walk_leaf(state, pte, 1);
      }
    } else {
      state->walk_l1_pte = pte;
      state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L2_REQ;
      state->walk_req_id_valid = false;
      state->walk_req_id = 0;
    }
  } else if (state->walk_state ==
             PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP) {
    if (!v || (!r && w) || !(r || x)) {
      ptw_kernel_publish_walk_fault(state);
    } else {
      ptw_kernel_publish_walk_leaf(state, pte, 0);
    }
  } else {
    ptw_kernel_publish_walk_fault(state);
  }
}

static inline void ptw_kernel_on_walk_mem_replay(PtwCompactKernelState *state,
                                                 size_t req_id,
                                                 uint8_t replay_reason) {
  const bool is_active_req =
      state->walk_active && state->walk_req_id_valid &&
      (state->walk_req_id == req_id);
  if (state->walk_drop_resp_credit > 0 && !is_active_req) {
    state->walk_drop_resp_credit--;
    return;
  }
  if (!state->walk_active || !is_active_req) {
    return;
  }

  PtwCompactKernelWalkClientState *wc =
      &state->walk_clients[state->walk_owner];
  wc->req_inflight = false;

  if (replay_reason == 2u) {
    if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP) {
      state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L1_REQ;
    } else if (state->walk_state ==
               PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP) {
      state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L2_REQ;
    }
  }
  state->walk_req_id_valid = false;
  state->walk_req_id = 0;
}

static inline void ptw_kernel_apply_events(PtwCompactKernelState *state,
                                           const PtwCompactKernelFeedbackIn *fb) {
  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_EVENT_SLOTS; i++) {
    if ((unsigned)i >= fb->event_count) {
      break;
    }
    const PtwCompactKernelEventIn *evt = &fb->events[i];
    if (!evt->valid) {
      continue;
    }
    switch (evt->owner) {
    case PTW_COMPACT_CORE_KERNEL_OWNER_MEM_DTLB:
      if (evt->replay == 0) {
        ptw_kernel_on_mem_resp_client(
            state, PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB, evt->data);
      }
      break;
    case PTW_COMPACT_CORE_KERNEL_OWNER_MEM_ITLB:
      if (evt->replay == 0) {
        ptw_kernel_on_mem_resp_client(
            state, PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB, evt->data);
      }
      break;
    case PTW_COMPACT_CORE_KERNEL_OWNER_WALK:
      if (evt->replay == 0) {
        ptw_kernel_on_walk_mem_resp(state, evt->req_id, evt->data);
      } else {
        ptw_kernel_on_walk_mem_replay(state, evt->req_id, evt->replay);
      }
      break;
    default:
      break;
    }
  }
}

static inline void ptw_kernel_retry_mem_req(PtwCompactKernelState *state,
                                            uint8_t client) {
  PtwCompactKernelMemClientState *s = &state->mem_clients[client];
  if (!s->req_inflight) {
    return;
  }
  s->req_inflight = false;
  s->req_pending = true;
}

static inline void ptw_kernel_retry_active_walk(PtwCompactKernelState *state) {
  if (!state->walk_active) {
    return;
  }
  if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP) {
    state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L1_REQ;
  } else if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP) {
    state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_L2_REQ;
  }
  state->walk_req_id_valid = false;
  state->walk_req_id = 0;
}

static inline void ptw_kernel_apply_wakeups(PtwCompactKernelState *state,
                                            const PtwCompactKernelFeedbackIn *fb) {
  if (fb->wakeup_dtlb) {
    ptw_kernel_retry_mem_req(state, PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB);
  }
  if (fb->wakeup_itlb) {
    ptw_kernel_retry_mem_req(state, PTW_COMPACT_CORE_KERNEL_CLIENT_ITLB);
  }
  if (fb->wakeup_walk) {
    ptw_kernel_retry_active_walk(state);
  }
}

static inline void ptw_kernel_flush_walk_client(PtwCompactKernelState *state,
                                                uint8_t client) {
  PtwCompactKernelWalkClientState *s = &state->walk_clients[client];
  s->req_pending = false;
  s->req_inflight = false;
  s->resp_valid = false;

  if (state->walk_active && state->walk_owner == client) {
    if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L1_WAIT_RESP ||
        state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L2_WAIT_RESP) {
      state->walk_drop_resp_credit++;
    }
    state->walk_active = false;
    state->walk_state = PTW_COMPACT_CORE_KERNEL_WALK_IDLE;
    state->walk_l1_pte = 0;
    state->walk_req_id_valid = false;
    state->walk_req_id = 0;
  }
}

static inline void ptw_kernel_apply_seq_inputs(
    PtwCompactKernelState *state, const PtwCompactKernelSeqIn *seq_in) {
  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    if (seq_in->walk_client_flush[i]) {
      ptw_kernel_flush_walk_client(state, (uint8_t)i);
    }
  }
}

static inline void ptw_compact_core_kernel_eval_state(
    const bool *pi, PtwCompactKernelState *state) {
  PtwCompactKernelPortIn port_in;
  PtwCompactKernelSeqIn seq_in;
  PtwCompactKernelFeedbackIn feedback;

  ptw_kernel_decode_input(pi, state, &port_in, &seq_in, &feedback);
  ptw_kernel_apply_port_inputs(state, &port_in);
  ptw_kernel_select_walk_owner(state);
  ptw_kernel_apply_grant(state, &feedback);
  ptw_kernel_apply_events(state, &feedback);
  ptw_kernel_apply_wakeups(state, &feedback);
  ptw_kernel_apply_seq_inputs(state, &seq_in);
}

static inline bool ptw_kernel_uint_bit(unsigned value, int bit) {
  return ((value >> bit) & 1u) != 0u;
}

static inline bool ptw_kernel_u32_bit(uint32_t value, int bit) {
  return ((value >> bit) & 1u) != 0u;
}

static inline bool ptw_compact_core_kernel_state_bit(
    const PtwCompactKernelState *state, int bit) {
  if (bit < 0 || bit >= PTW_COMPACT_CORE_KERNEL_PO_WIDTH) {
    return false;
  }

  if (bit < PTW_COMPACT_CORE_KERNEL_CORE_STATE_WIDTH) {
    if (bit < 3) {
      return ptw_kernel_uint_bit(state->walk_state, bit);
    }
    if (bit == 3) {
      return state->walk_rr_next != PTW_COMPACT_CORE_KERNEL_CLIENT_DTLB;
    }
    if (bit < 26) {
      return ptw_kernel_u32_bit((state->walk_l1_pte >> 10) & 0x3FFFFFu,
                                bit - 4);
    }
    const unsigned credit = state->walk_drop_resp_credit > 3
                                ? 3
                                : state->walk_drop_resp_credit;
    return ptw_kernel_uint_bit(credit, bit - 26);
  }

  for (int client = 0; client < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT;
       client++) {
    const int base = ptw_kernel_state_mem_base(client);
    const int off = bit - base;
    if (off < 0 || off >= PTW_COMPACT_CORE_KERNEL_MEM_CLIENT_STATE_WIDTH) {
      continue;
    }
    const PtwCompactKernelMemClientState *mem =
        &state->mem_clients[client];
    if (off < 2) {
      const unsigned req_state = (mem->req_pending ? 1u : 0u) |
                                 (mem->req_inflight ? 2u : 0u);
      return ptw_kernel_uint_bit(req_state, off);
    }
    if (off < 32) {
      return ptw_kernel_u32_bit(mem->req_addr >> 2, off - 2);
    }
    if (off == 32) {
      return mem->resp_valid;
    }
    return ptw_kernel_u32_bit(ptw_kernel_pack_pte_payload(mem->resp_data),
                              off - 33);
  }

  for (int client = 0; client < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT;
       client++) {
    const int base = ptw_kernel_state_walk_base(client);
    const int off = bit - base;
    if (off < 0 || off >= PTW_COMPACT_CORE_KERNEL_WALK_CLIENT_STATE_WIDTH) {
      continue;
    }
    const PtwCompactKernelWalkClientState *walk =
        &state->walk_clients[client];
    if (off < 2) {
      const unsigned req_state = (walk->req_pending ? 1u : 0u) |
                                 (walk->req_inflight ? 2u : 0u);
      return ptw_kernel_uint_bit(req_state, off);
    }
    if (off < 22) {
      return ptw_kernel_u32_bit((walk->req.vaddr >> 12) & 0xFFFFFu,
                                off - 2);
    }
    if (off < 44) {
      return ptw_kernel_u32_bit(walk->req.satp & 0x3FFFFFu, off - 22);
    }
    if (off == 44) {
      return walk->resp_valid;
    }
    if (off == 45) {
      return walk->resp.fault;
    }
    if (off < 68) {
      return ptw_kernel_u32_bit((walk->resp.leaf_pte >> 10) & 0x3FFFFFu,
                                off - 46);
    }
    if (off < 76) {
      return ptw_kernel_u32_bit(walk->resp.leaf_pte & 0xFFu, off - 68);
    }
    return (walk->resp.leaf_level & 1u) != 0u;
  }

  return false;
}

static inline bool ptw_compact_core_kernel_eval_bit(const bool *pi,
                                                    int bit) {
  PtwCompactKernelState state;
  ptw_compact_core_kernel_eval_state(pi, &state);
  return ptw_compact_core_kernel_state_bit(&state, bit);
}

static inline void ptw_compact_core_kernel_eval(const bool *pi, bool *po) {
  PtwCompactKernelState state;
  ptw_compact_core_kernel_eval_state(pi, &state);

  ptw_kernel_clear_bits(po, PTW_COMPACT_CORE_KERNEL_PO_WIDTH);
  ptw_kernel_encode_state(po, &state);
}

static inline void ptw_kernel_encode_core_port_output(
    bool *po, const PtwCompactKernelState *state) {
  ptw_kernel_clear_bits(po, PTW_COMPACT_CORE_KERNEL_CORE_PORT_OUT_WIDTH);

  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    const PtwCompactKernelMemClientState *mem = &state->mem_clients[i];
    const int mem_base = ptw_kernel_core_port_mem_out_base(i);
    ptw_kernel_write_bit(po, mem_base + 0,
                         !mem->req_pending && !mem->req_inflight);
    ptw_kernel_write_bit(po, mem_base + 1, mem->resp_valid);
    ptw_kernel_write_uint(po, mem_base + 2, 32, mem->resp_data);

    const PtwCompactKernelWalkClientState *walk = &state->walk_clients[i];
    const int walk_base = ptw_kernel_core_port_walk_out_base(i);
    ptw_kernel_write_bit(po, walk_base + 0,
                         !walk->req_pending && !walk->req_inflight);
    ptw_kernel_write_bit(po, walk_base + 1, walk->resp_valid);
    ptw_kernel_write_bit(po, walk_base + 2, walk->resp.fault);
    ptw_kernel_write_uint(po, walk_base + 3, 32, walk->resp.vaddr);
    ptw_kernel_write_uint(po, walk_base + 35, 32, walk->resp.leaf_pte);
    ptw_kernel_write_uint(po, walk_base + 67, 8, walk->resp.leaf_level);
  }

  const int arb_base = ptw_kernel_core_port_arb_out_base();
  ptw_kernel_write_bit(po, arb_base + 33,
                       state->mem_clients[0].req_pending);
  ptw_kernel_write_bit(po, arb_base + 34,
                       state->mem_clients[1].req_pending);
  ptw_kernel_write_uint(po, arb_base + 35, 32,
                        state->mem_clients[0].req_addr);
  ptw_kernel_write_uint(po, arb_base + 67, 32,
                        state->mem_clients[1].req_addr);

  if (!state->walk_active) {
    return;
  }

  const PtwCompactKernelWalkReq *req =
      &state->walk_clients[state->walk_owner].req;
  if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L1_REQ) {
    const uint32_t root_ppn = req->satp & 0x3FFFFFu;
    const uint32_t vpn1 = (req->vaddr >> 22) & 0x3FFu;
    ptw_kernel_write_bit(po, arb_base + 0, true);
    ptw_kernel_write_uint(po, arb_base + 1, 32,
                          (root_ppn << 12) + (vpn1 << 2));
  } else if (state->walk_state == PTW_COMPACT_CORE_KERNEL_WALK_L2_REQ) {
    const uint32_t ppn = (state->walk_l1_pte >> 10) & 0x3FFFFFu;
    const uint32_t vpn0 = (req->vaddr >> 12) & 0x3FFu;
    ptw_kernel_write_bit(po, arb_base + 0, true);
    ptw_kernel_write_uint(po, arb_base + 1, 32,
                          (ppn << 12) + (vpn0 << 2));
  }
}

static inline void ptw_compact_core_visible_kernel(const bool *pi, bool *po) {
  PtwCompactKernelState state;
  ptw_kernel_decode_state(pi, &state);
  ptw_kernel_encode_core_port_output(po, &state);
}

static inline void ptw_kernel_encode_core_port_input_from_top_external(
    bool *po, const bool *pi) {
  ptw_kernel_clear_bits(po, PTW_COMPACT_CORE_KERNEL_CORE_PORT_IN_WIDTH);

  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_CLIENT_COUNT; i++) {
    const int top_mem_base = ptw_kernel_top_external_mem_in_base(i);
    const int core_mem_base = ptw_kernel_core_port_mem_in_base(i);
    const uint32_t mem_addr =
        ptw_kernel_read_uint(pi, top_mem_base + 1, 32);
    ptw_kernel_write_bit(po, core_mem_base + 0,
                         ptw_kernel_read_bit(pi, top_mem_base + 0));
    ptw_kernel_write_word_addr(po, core_mem_base + 1, mem_addr);
    ptw_kernel_write_bit(po, core_mem_base + 31,
                         ptw_kernel_read_bit(pi, top_mem_base + 33));

    const int top_walk_base = ptw_kernel_top_external_walk_in_base(i);
    const int core_walk_base = ptw_kernel_core_port_walk_in_base(i);
    const uint32_t vaddr =
        ptw_kernel_read_uint(pi, top_walk_base + 1, 32);
    const uint32_t satp =
        ptw_kernel_read_uint(pi, top_walk_base + 33, 32);
    ptw_kernel_write_bit(po, core_walk_base + 0,
                         ptw_kernel_read_bit(pi, top_walk_base + 0));
    ptw_kernel_write_uint(po, core_walk_base + 1, 20,
                          (vaddr >> 12) & 0xFFFFFu);
    ptw_kernel_write_uint(po, core_walk_base + 21, 22, satp & 0x3FFFFFu);
    ptw_kernel_write_bit(po, core_walk_base + 43,
                         ptw_kernel_read_bit(pi, top_walk_base + 97));
    ptw_kernel_write_bit(po, core_walk_base + 44,
                         ptw_kernel_read_bit(pi, top_walk_base + 98));
  }

  ptw_kernel_copy_bits(po, ptw_kernel_core_port_grant_in_base(), pi,
                       ptw_kernel_top_external_grant_in_base(),
                       PTW_COMPACT_CORE_KERNEL_CORE_PORT_GRANT_IN_WIDTH);

  const unsigned event_count =
      ptw_kernel_read_uint(pi, ptw_kernel_top_external_event_count_base(), 8);
  int selected_event = -1;
  for (int i = 0; i < PTW_COMPACT_CORE_KERNEL_TOP_EVENT_SLOTS; i++) {
    if ((unsigned)i >= event_count) {
      break;
    }
    const int top_event_base = ptw_kernel_top_external_event_in_base(i);
    if (ptw_kernel_read_bit(pi, top_event_base + 0)) {
      selected_event = i;
      break;
    }
  }
  if (selected_event >= 0) {
    const int top_event_base =
        ptw_kernel_top_external_event_in_base(selected_event);
    const int core_event_base = ptw_kernel_core_port_event_in_base(0);
    const uint32_t data =
        ptw_kernel_read_uint(pi, top_event_base + 3, 32);
    ptw_kernel_write_bit(po, core_event_base + 0, true);
    ptw_kernel_copy_bits(po, core_event_base + 1, pi, top_event_base + 1, 2);
    ptw_kernel_write_uint(po, core_event_base + 3, 30,
                          ptw_kernel_pack_pte_payload(data));
    ptw_kernel_copy_bits(po, core_event_base + 33, pi,
                         top_event_base + 35, 2);
    ptw_kernel_copy_bits(po, core_event_base + 35, pi,
                         top_event_base + 69,
                         PTW_COMPACT_CORE_KERNEL_REQ_ID_WIDTH);
  }

  ptw_kernel_copy_bits(po, ptw_kernel_core_port_wakeup_in_base(), pi,
                       ptw_kernel_top_external_wakeup_in_base(),
                       PTW_COMPACT_CORE_KERNEL_CORE_PORT_WAKEUP_IN_WIDTH);
}

static inline void ptw_top_glue_kernel(const bool *pi, bool *po) {
  ptw_kernel_clear_bits(po, PTW_COMPACT_CORE_KERNEL_TOP_GLUE_PO_WIDTH);
  ptw_kernel_encode_core_port_input_from_top_external(
      po + PTW_COMPACT_CORE_KERNEL_TOP_GLUE_CORE_IN_BASE,
      pi + PTW_COMPACT_CORE_KERNEL_TOP_GLUE_EXTERNAL_IN_BASE);
  ptw_kernel_copy_bits(po, PTW_COMPACT_CORE_KERNEL_TOP_GLUE_EXTERNAL_OUT_BASE,
                       pi, PTW_COMPACT_CORE_KERNEL_TOP_GLUE_CORE_OUT_BASE,
                       PTW_COMPACT_CORE_KERNEL_TOP_EXTERNAL_OUTPUT_WIDTH);
}

#endif
