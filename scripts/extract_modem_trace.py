#!/usr/bin/env python3
#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from elftools.elf.elffile import ELFFile
import argparse
import struct


# /**
#  * @brief A structure to represent a ring buffer
#  */
# struct ring_buf {
# 	uint8_t *buffer;
# 	int32_t put_head;
# 	int32_t put_tail;
# 	int32_t put_base;
# 	int32_t get_head;
# 	int32_t get_tail;
# 	int32_t get_base;
# 	uint32_t size;
# };
#

ring_buf_struct_size = 32
ring_buf_struct_str = "<IiiiiiiI"


def read_data_from_elf(e: ELFFile, start, size):
    for s in e.iter_segments():
        start_addr = s.header['p_vaddr']
        end_addr = start_addr + s.header['p_filesz']
        if start in range(start_addr, end_addr):
            start_off = start - start_addr
            result = s.data()[start_off:start_off + size]
            return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="extract RAM modem trace from coredump")
    parser.add_argument("-c", "--coredump",
                        help="coredump ELF file", required=True)
    parser.add_argument("-s", "--symbols",
                        help="symbols ELF file", required=True)
    parser.add_argument(
        "-o", "--output", help="output BIN file", required=True)
    args = parser.parse_args()

    with open(args.symbols, "rb") as f:
        symbols = ELFFile(f)
        symtab = symbols.get_section_by_name('.symtab')
        ring_buf_location = symtab.get_symbol_by_name('ram_trace_buf')[
            0].entry['st_value']

    with open(args.coredump, "rb") as f:
        coredump = ELFFile(f)
        data = read_data_from_elf(
            coredump, ring_buf_location, ring_buf_struct_size)
        buffer, put_head, put_tail, put_base, \
            get_head, get_tail, get_base, \
            capacity = struct.unpack(ring_buf_struct_str, data)
        buffer_content = read_data_from_elf(coredump, buffer, capacity)

    stored_size = put_tail - get_head
    start = get_head - get_base
    end = get_head - get_base + stored_size  # might have to wrap around

    print(f"modem trace buffer contained {(stored_size)} bytes.")

    if end > capacity:
        result_buf = buffer_content[start:] + buffer_content[:end - capacity]
    else:
        result_buf = buffer_content[start:end]

    with open(args.output, "wb") as f:
        f.write(result_buf)
