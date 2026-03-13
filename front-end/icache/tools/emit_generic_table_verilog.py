#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Any


def flat_bits(chunks: int, chunk_bits: int) -> int:
    return chunks * chunk_bits


def gen_header(module_name: str, rows: int, chunks: int, chunk_bits: int, fixed_latency: int | None) -> str:
    params = [
        f"    parameter integer ROWS = {rows}",
        f"    parameter integer CHUNKS = {chunks}",
        f"    parameter integer CHUNK_BITS = {chunk_bits}",
        "    parameter integer ADDR_BITS = $clog2(ROWS)",
        "    parameter integer TOTAL_BITS = CHUNKS * CHUNK_BITS",
    ]
    if fixed_latency is not None:
        params.append(f"    parameter integer FIXED_LATENCY = {fixed_latency}")
    param_block = ',\n'.join(params)
    return f'''module {module_name} #(
{param_block}
) (
    input  wire                         clk,
    input  wire                         rst,
    input  wire                         rd_req_valid,
    input  wire [ADDR_BITS-1:0]         rd_req_addr,
    output wire                         rd_resp_valid,
    output wire [TOTAL_BITS-1:0]        rd_resp_flat,
    input  wire                         wr_req_valid,
    input  wire [ADDR_BITS-1:0]         wr_req_addr,
    input  wire [TOTAL_BITS-1:0]        wr_req_flat,
    input  wire [CHUNKS-1:0]            wr_chunk_enable
);

'''


def gen_common_storage() -> list[str]:
    lines: list[str] = []
    lines.append("  reg  [TOTAL_BITS-1:0] storage [0:ROWS-1];\n")
    lines.append("  wire [TOTAL_BITS-1:0] wr_req_mask;\n")
    lines.append("  genvar c;\n")
    lines.append("  generate\n")
    lines.append("    for (c = 0; c < CHUNKS; c = c + 1) begin : gen_chunk_mask\n")
    lines.append("      assign wr_req_mask[(c+1)*CHUNK_BITS-1:c*CHUNK_BITS] = {CHUNK_BITS{wr_chunk_enable[c]}};\n")
    lines.append("    end\n")
    lines.append("  endgenerate\n\n")
    return lines


def gen_regfile_module(module_name: str, rows: int, chunks: int, chunk_bits: int) -> str:
    lines = [gen_header(module_name, rows, chunks, chunk_bits, None)]
    lines.extend(gen_common_storage())
    lines.append("  assign rd_resp_valid = rd_req_valid;\n")
    lines.append("  assign rd_resp_flat = storage[rd_req_addr];\n\n")
    lines.append("  integer i;\n")
    lines.append("  always @(posedge clk) begin\n")
    lines.append("    if (rst) begin\n")
    lines.append("      for (i = 0; i < ROWS; i = i + 1) begin\n")
    lines.append("        storage[i] <= {TOTAL_BITS{1'b0}};\n")
    lines.append("      end\n")
    lines.append("    end else if (wr_req_valid) begin\n")
    lines.append("      storage[wr_req_addr] <= (storage[wr_req_addr] & ~wr_req_mask) | (wr_req_flat & wr_req_mask);\n")
    lines.append("    end\n")
    lines.append("  end\n")
    lines.append("endmodule\n")
    return ''.join(lines)


def gen_sram_module(module_name: str, rows: int, chunks: int, chunk_bits: int, fixed_latency: int) -> str:
    lines = [gen_header(module_name, rows, chunks, chunk_bits, fixed_latency)]
    lines.append("  // Template for a synchronous-read generic table.\n")
    lines.append("  // Replace storage with SRAM macros or generated wrappers as needed.\n")
    lines.extend(gen_common_storage())
    lines.append("  reg [ADDR_BITS-1:0] rd_addr_q;\n")
    lines.append("  reg                 rd_valid_q;\n")
    lines.append("  integer             rd_latency_q;\n\n")
    lines.append("  assign rd_resp_valid = rd_valid_q && (rd_latency_q == 0);\n")
    lines.append("  assign rd_resp_flat = storage[rd_addr_q];\n\n")
    lines.append("  integer i;\n")
    lines.append("  always @(posedge clk) begin\n")
    lines.append("    if (rst) begin\n")
    lines.append("      rd_addr_q <= {ADDR_BITS{1'b0}};\n")
    lines.append("      rd_valid_q <= 1'b0;\n")
    lines.append("      rd_latency_q <= 0;\n")
    lines.append("      for (i = 0; i < ROWS; i = i + 1) begin\n")
    lines.append("        storage[i] <= {TOTAL_BITS{1'b0}};\n")
    lines.append("      end\n")
    lines.append("    end else begin\n")
    lines.append("      if (rd_req_valid && !rd_valid_q) begin\n")
    lines.append("        rd_addr_q <= rd_req_addr;\n")
    lines.append("        rd_valid_q <= 1'b1;\n")
    lines.append("        rd_latency_q <= FIXED_LATENCY - 1;\n")
    lines.append("      end else if (rd_valid_q && rd_latency_q > 0) begin\n")
    lines.append("        rd_latency_q <= rd_latency_q - 1;\n")
    lines.append("      end else if (rd_valid_q) begin\n")
    lines.append("        rd_valid_q <= 1'b0;\n")
    lines.append("      end\n")
    lines.append("      if (wr_req_valid) begin\n")
    lines.append("        storage[wr_req_addr] <= (storage[wr_req_addr] & ~wr_req_mask) | (wr_req_flat & wr_req_mask);\n")
    lines.append("      end\n")
    lines.append("    end\n")
    lines.append("  end\n")
    lines.append("endmodule\n")
    return ''.join(lines)


def emit_single_module(spec: dict[str, Any], output_dir: Path) -> Path:
    module_name = spec['module_name']
    style = spec['style']
    rows = int(spec.get('rows', 128))
    chunks = int(spec['chunks'])
    chunk_bits = int(spec['chunk_bits'])
    fixed_latency = int(spec.get('fixed_latency', 1))
    output_name = spec.get('output', f'{module_name}.v')

    if style == 'regfile':
        text = gen_regfile_module(module_name, rows, chunks, chunk_bits)
    else:
        text = gen_sram_module(module_name, rows, chunks, chunk_bits, fixed_latency)

    out_path = output_dir / output_name
    out_path.write_text(text)
    return out_path


def load_config_file(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text())
    if 'modules' not in data or not isinstance(data['modules'], list):
        raise ValueError(f'{path}: config must contain a list field "modules"')
    return data


def merge_defaults(defaults: dict[str, Any], module_spec: dict[str, Any]) -> dict[str, Any]:
    merged = dict(defaults)
    merged.update(module_spec)
    return merged


def emit_from_config_path(config_path: Path, output_dir: Path) -> list[Path]:
    config = load_config_file(config_path)
    defaults = config.get('defaults', {})
    emitted: list[Path] = []
    for module_spec in config['modules']:
        emitted.append(emit_single_module(merge_defaults(defaults, module_spec), output_dir))
    return emitted


def emit_from_config_dir(config_dir: Path, output_dir: Path) -> list[Path]:
    emitted: list[Path] = []
    for config_path in sorted(config_dir.glob('*.json')):
        emitted.extend(emit_from_config_path(config_path, output_dir))
    return emitted


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Emit Verilog for generic tables matching GenericTable semantics.'
    )
    parser.add_argument('--module-name')
    parser.add_argument('--rows', type=int, default=128)
    parser.add_argument('--chunks', type=int)
    parser.add_argument('--chunk-bits', type=int)
    parser.add_argument('--style', choices=['regfile', 'sram'])
    parser.add_argument('--fixed-latency', type=int, default=1)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--config', type=Path,
                        help='JSON config file or directory. A single file can emit multiple modules.')
    parser.add_argument('--output-dir', type=Path,
                        help='Output directory when using --config')
    args = parser.parse_args()

    if args.config is not None:
        if args.output_dir is None:
            raise SystemExit('--output-dir is required when using --config')
        args.output_dir.mkdir(parents=True, exist_ok=True)
        if args.config.is_dir():
            emitted = emit_from_config_dir(args.config, args.output_dir)
        else:
            emitted = emit_from_config_path(args.config, args.output_dir)
        for path in emitted:
            print(path)
        return

    required = [args.module_name, args.chunks, args.chunk_bits, args.style, args.output]
    if any(v is None for v in required):
        raise SystemExit('single-module mode requires --module-name --chunks --chunk-bits --style --output')

    emit_single_module({
        'module_name': args.module_name,
        'rows': args.rows,
        'chunks': args.chunks,
        'chunk_bits': args.chunk_bits,
        'style': args.style,
        'fixed_latency': args.fixed_latency,
        'output': args.output.name,
    }, args.output.parent)


if __name__ == '__main__':
    main()
