#pragma once

namespace deadlock_debug {
using DumpCallback = void (*)();

void register_lsu_dump_cb(DumpCallback cb);
void register_mem_dump_cb(DumpCallback cb);
void register_soc_dump_cb(DumpCallback cb);
void dump_all();
} // namespace deadlock_debug
