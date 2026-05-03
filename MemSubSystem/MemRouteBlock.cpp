#include "MemRouteBlock.h"
void MemRouteBlock::init() {
  cur = {};
  nxt = {};

  cur.next_req_id = MemRouteBlockReqID;
  nxt.next_req_id = MemRouteBlockReqID;
}
void MemRouteBlock::comb_response() {

  *out.icache_resp = {};
  *out.lsu_resp = {};
  *out.ptw_events = {};
  *out.wakeup = {};

  memcpy(out.lsu_resp, in.dcache_resp, sizeof(DcacheLsuIO));

  for (int i = 0; i < LSU_LDU_COUNT; i++) {
    auto &resp = out.lsu_resp->resp_ports.load_resps[i];
    if (resp.valid && dcache_req_id_is_route(resp.req_id)) {
      resp.valid = false;
    }
  }

  if (cur.issued_tags.owner != Owner::NONE && in.dcache_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].valid && in.dcache_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].req_id == cur.issued_tags.req_id) {
    out.lsu_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].valid = cur.lsu_replay_valid;
    out.lsu_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].req_id = cur.lsu_replay_req_id;
    out.lsu_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].data = 0;                      // Data can be set as needed
    out.lsu_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].replay = ReplayType::CONFLICT; // Set replay type as needed

    Owner owner = cur.issued_tags.owner;
    ReplayType replay = in.dcache_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].replay;
    if (replay == ReplayType::HIT) {
      if (owner == Owner::ICACHE) {
        out.icache_resp->resp_valid = true;
        out.icache_resp->resp_miss = false;
        out.icache_resp->resp_data = in.dcache_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].data;
      } else {
        out.ptw_events->valid = true;
        out.ptw_events->owner = owner;
        out.ptw_events->data = in.dcache_resp->resp_ports.load_resps[LSU_LDU_COUNT - 1].data;
        out.ptw_events->replay = 0;
        out.ptw_events->req_addr = cur.issued_tags.req_addr;
        out.ptw_events->req_id = cur.issued_tags.req_id;
      }
    } else if (replay == ReplayType::CONFLICT) {
      out.icache_resp->resp_valid = (owner == Owner::ICACHE);
      out.icache_resp->resp_miss = (owner == Owner::ICACHE);
      out.wakeup->dtlb = (owner == Owner::PTW_DTLB);
      out.wakeup->itlb = (owner == Owner::PTW_ITLB);
      out.wakeup->walk = (owner == Owner::PTW_WALK);
    } else {
      out.icache_resp->resp_valid = (owner == Owner::ICACHE);
      out.icache_resp->resp_miss = (owner == Owner::ICACHE);
      if (in.dcache_resp->mshr_fill) {
        out.wakeup->dtlb = (owner == Owner::PTW_DTLB);
        out.wakeup->itlb = (owner == Owner::PTW_ITLB);
        out.wakeup->walk = (owner == Owner::PTW_WALK);
      } else {
        nxt.ptw_wait.dtlb = (owner == Owner::PTW_DTLB);
        nxt.ptw_wait.itlb = (owner == Owner::PTW_ITLB);
        nxt.ptw_wait.walk = (owner == Owner::PTW_WALK);
      }
    }
  }
  if (in.dcache_resp->mshr_fill) {
    out.wakeup->dtlb |= nxt.ptw_wait.dtlb;
    out.wakeup->itlb |= nxt.ptw_wait.itlb;
    out.wakeup->walk |= nxt.ptw_wait.walk;
    nxt.ptw_wait = {};
  }
}

void MemRouteBlock::comb_request() {
  *out.ptw_grant = {};
  *out.dcache_req = LsuDcacheIO{};

  memcpy(&out.dcache_req->req_ports, in.lsu_req, sizeof(DCacheReqPorts));

  Owner ptw_owner = Owner::NONE;
  uint32_t ptw_addr = 0;
  if (in.icache_req->req_valid && cur.issued_tags.owner != Owner::ICACHE) {
    ptw_owner = Owner::ICACHE;
    ptw_addr = in.icache_req->req_addr;
  } else if (in.ptw_walk_req->valid && cur.issued_tags.owner != Owner::PTW_WALK) {
    ptw_owner = Owner::PTW_WALK;
    ptw_addr = in.ptw_walk_req->addr;
  } else if (in.ptw_dtlb_req->valid && cur.issued_tags.owner != Owner::PTW_DTLB) {
    ptw_owner = Owner::PTW_DTLB;
    ptw_addr = in.ptw_dtlb_req->addr;
  } else if (in.ptw_itlb_req->valid && cur.issued_tags.owner != Owner::PTW_ITLB) {
    ptw_owner = Owner::PTW_ITLB;
    ptw_addr = in.ptw_itlb_req->addr;
  }

  nxt.issued_tags.owner = ptw_owner;
  nxt.issued_tags.req_addr = ptw_addr;
  nxt.issued_tags.req_id = cur.next_req_id;
  if (ptw_owner != Owner::NONE) {
    out.dcache_req->req_ports.load_ports[LSU_LDU_COUNT - 1].valid = true;
    out.dcache_req->req_ports.load_ports[LSU_LDU_COUNT - 1].addr = ptw_addr;
    out.dcache_req->req_ports.load_ports[LSU_LDU_COUNT - 1].req_id = cur.next_req_id;
    out.dcache_req->icache_req = (ptw_owner == Owner::ICACHE) ? LSU_LDU_COUNT - 1 : LSU_LDU_COUNT; // Inject into port 0

    nxt.lsu_replay_valid = in.lsu_req->load_ports[LSU_LDU_COUNT - 1].valid;
    nxt.lsu_replay_req_id = in.lsu_req->load_ports[LSU_LDU_COUNT - 1].req_id;

    nxt.next_req_id = next_route_req_id(cur.next_req_id);
  }

  out.ptw_grant->valid = (ptw_owner != Owner::NONE) && (ptw_owner != Owner::ICACHE);
  out.ptw_grant->owner = ptw_owner;
  out.ptw_grant->req_id = cur.next_req_id;
}
void MemRouteBlock::seq() {
  // Update the current state to the next state at the end of the cycle.
  cur = nxt;
}
void MemRouteBlock::dump_debug_state(FILE *out) const {
  fprintf(out, "MemRouteBlock State:\n");
  fprintf(out, "  Issued Tag - Owner: %d, Addr: 0x%08x, ID: %u\n", static_cast<int>(cur.issued_tags.owner), cur.issued_tags.req_addr, cur.issued_tags.req_id);
  fprintf(out, "  PTW Wait - DTLB: %d, ITLB: %d, WALK: %d\n", cur.ptw_wait.dtlb, cur.ptw_wait.itlb, cur.ptw_wait.walk);
  fprintf(out, "  LSU Replay - Valid: %d, Req ID: %u\n", cur.lsu_replay_valid, cur.lsu_replay_req_id);
}