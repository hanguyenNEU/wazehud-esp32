#!/usr/bin/env python3
"""Convert a xiaozhi-esp32 cbin font (78/xiaozhi-fonts) to an LVGL 9 .fnt binfont.

The cbin format is a relocatable blob of LVGL structs (offsets instead of
pointers) as written by 78/lv_font_conv's "cbin" writer. The .fnt output follows
LVGL 9's lv_binfont_loader spec (head/cmap/loca/glyf/kern sections).

Usage: cbin2fnt.py font_puhui_common_20_4.bin [-o font.fnt]
"""
import argparse
import struct
import sys


def count_bits(v):
    return v.bit_length() if v > 0 else 0


def signed_bits(v):
    if v >= 0:
        return count_bits(v) + 1
    return count_bits(abs(v) - 1) + 1


def align4(n):
    return (n + 3) & ~3


class BitWriter:
    """MSB-first bit packer, matching lv_font_conv's BitStream (big-endian)."""

    def __init__(self):
        self.buf = bytearray()
        self.acc = 0
        self.n = 0

    def write(self, value, bits):
        while bits:
            take = min(bits, 8 - self.n)
            shift = bits - take
            self.acc |= ((value >> shift) & ((1 << take) - 1)) << (8 - self.n - take)
            self.n += take
            bits -= take
            if self.n == 8:
                self.buf.append(self.acc)
                self.acc = 0
                self.n = 0

    def bytes(self):
        if self.n:
            self.buf.append(self.acc)
        return bytes(self.buf)


def parse_cbin(path):
    d = open(path, 'rb').read()

    # lv_font_t blob (36 bytes)
    line_height, base_line = struct.unpack_from('<Ii', d, 12)
    subpx = d[20] & 3
    underline_pos, underline_thk = struct.unpack_from('<bb', d, 21)
    dsc_ofs = struct.unpack_from('<I', d, 24)[0]

    # lv_font_fmt_txt_dsc_t blob (24 bytes at dsc_ofs)
    gbm_off, gdsc_off, cmap_off, kern_off = struct.unpack_from('<IIII', d, dsc_ofs)
    kern_scale, bf = struct.unpack_from('<HH', d, dsc_ofs + 16)
    cmap_num = bf & 0x1FF
    bpp = (bf >> 9) & 0xF
    kern_classes = (bf >> 13) & 1
    bitmap_format = (bf >> 14) & 3

    # cmap array: 20 bytes each, offsets relative to the array start
    cmap_base = dsc_ofs + cmap_off
    cmaps = []
    max_id = 0
    covered = set()
    for i in range(cmap_num):
        e = cmap_base + i * 20
        rs, rl, gid, ul, gol, ll, typ = struct.unpack_from('<IHHIIHB', d, e)
        cmaps.append(dict(start=rs, rlen=rl, gid_start=gid, ul_off=ul,
                          gol_off=gol, list_len=ll, type=typ))
        if typ == 0:  # format0: u8 deltas over the whole range
            covered.update(range(rs, rs + rl))
            if gol:
                ids = d[cmap_base + gol:cmap_base + gol + rl]
                max_id = max(max_id, gid + max(ids))
            else:
                max_id = max(max_id, gid + rl - 1)
        elif typ == 1:  # sparse: u16 deltas
            ids = struct.unpack_from('<%dH' % ll, d, cmap_base + gol)
            max_id = max(max_id, gid + max(ids))
            codes = struct.unpack_from('<%dH' % ll, d, cmap_base + ul)
            covered.update(rs + c for c in codes)
        elif typ == 3:  # sparse tiny: contiguous ids
            max_id = max(max_id, gid + ll - 1)
            codes = struct.unpack_from('<%dH' % ll, d, cmap_base + ul)
            covered.update(rs + c for c in codes)
        else:  # format0 tiny
            covered.update(range(rs, rs + rl))
            max_id = max(max_id, gid + rl - 1)

    glyph_count = max_id + 1

    # glyph dscs: 16 bytes each, glyph 0 is the leading zero slot
    dsc_base = dsc_ofs + gdsc_off
    glyphs = []
    for i in range(glyph_count):
        bi, adv = struct.unpack_from('<II', d, dsc_base + i * 16)
        bw, bh, ox, oy = struct.unpack_from('<HHhh', d, dsc_base + i * 16 + 8)
        glyphs.append(dict(bitmap_index=bi, adv_w=adv, box_w=bw, box_h=bh,
                           ofs_x=ox, ofs_y=oy))

    # bitmaps: plain format, packed rows, byte-padded at the end
    bmp_base = dsc_ofs + gbm_off
    bitmaps = []
    if bitmap_format != 0:
        sys.exit('error: compressed cbin bitmaps are not supported yet')
    for i, g in enumerate(glyphs):
        nbytes = (g['box_w'] * bpp * g['box_h'] + 7) // 8
        bitmaps.append(d[bmp_base + g['bitmap_index']:
                         bmp_base + g['bitmap_index'] + nbytes])
        if i > 0:
            assert g['bitmap_index'] == glyphs[i - 1]['bitmap_index'] + len(bitmaps[i - 1]), \
                f'bitmap layout mismatch at glyph {i}'

    # kern
    kern = None
    if kern_off:
        kb = dsc_ofs + kern_off
        if kern_classes == 1:
            vals_off, left_off, right_off = struct.unpack_from('<III', d, kb)
            rows, cols = struct.unpack_from('<BB', d, kb + 12)
            left = d[kb + left_off:kb + left_off + glyph_count]
            right = d[kb + right_off:kb + right_off + glyph_count]
            values = d[kb + vals_off:kb + vals_off + rows * cols]
            kern = dict(classes=1, rows=rows, cols=cols,
                        left=left, right=right, values=values)
        else:
            ids_off, vals_off = struct.unpack_from('<II', d, kb)
            pair_cnt_word = struct.unpack_from('<I', d, kb + 8)[0]
            pair_cnt = pair_cnt_word & 0x3FFFFFFF
            ids_size = pair_cnt_word >> 30
            ids = d[kb + ids_off:kb + ids_off + pair_cnt * 2 * (2 if ids_size else 1)]
            values = d[kb + vals_off:kb + vals_off + pair_cnt]
            kern = dict(classes=0, pair_cnt=pair_cnt, ids_size=ids_size,
                        ids=ids, values=values)

    return dict(line_height=line_height, base_line=base_line, subpx=subpx,
                underline_pos=underline_pos, underline_thk=underline_thk,
                kern_scale=kern_scale, cmap_num=cmap_num, bpp=bpp,
                glyphs=glyphs, bitmaps=bitmaps, cmaps=cmaps, kern=kern,
                covered=covered)


def build_fnt(font):
    glyphs = font['glyphs']
    bpp = font['bpp']
    n = len(glyphs)

    monospaced = all(g['adv_w'] == glyphs[1]['adv_w'] for g in glyphs[1:])

    xy_bits = max(max(signed_bits(g['ofs_x']), signed_bits(g['ofs_y'])) for g in glyphs)
    wh_bits = max(max(count_bits(g['box_w']), count_bits(g['box_h'])) for g in glyphs)
    adv_bits = 0 if monospaced else max(1, max(count_bits(g['adv_w']) for g in glyphs))

    # glyf records (glyph 0 = empty, mirroring lv_font_conv)
    records = [b'']
    for i, g in enumerate(glyphs[1:], start=1):
        bw = BitWriter()
        if not monospaced:
            bw.write(g['adv_w'], adv_bits)
        mask = (1 << xy_bits) - 1
        bw.write(g['ofs_x'] & mask, xy_bits)
        bw.write(g['ofs_y'] & mask, xy_bits)
        bw.write(g['box_w'], wh_bits)
        bw.write(g['box_h'], wh_bits)
        for byte in font['bitmaps'][i]:
            bw.write(byte, 8)
        records.append(bw.bytes())

    # loca offsets are relative to the glyf section start (label position)
    offsets = [8]
    for i in range(1, n):
        offsets.append(offsets[-1] + len(records[i - 1]))
    glyf_payload = b''.join(records)

    # head section (48-byte header)
    ascent = font['line_height'] - font['base_line']
    descent = -font['base_line']
    min_y = min(g['ofs_y'] for g in glyphs)
    max_y = max(g['ofs_y'] + g['box_h'] for g in glyphs)
    kern = font['kern']
    head = struct.pack('<I4sIHHHhHhHhhHHBBBBBBBBBBhH',
                       48, b'head', 1,                         # len, label, version
                       4 if kern else 3, 20,                   # tables_count, font_size
                       ascent, descent, ascent, descent, 0,    # ascent..typo_line_gap
                       min_y, max_y,
                       (glyphs[1]['adv_w'] if monospaced else 0),  # default_advance_width
                       font['kern_scale'],                     # FP12.4
                       1 if len(glyf_payload) > 65535 else 0,  # index_to_loc_format
                       1 if n - 1 > 255 else 0,                # glyph_id_format
                       1, bpp,                                 # advance_width_format, bpp
                       xy_bits, wh_bits, adv_bits,
                       0, font['subpx'], 0,                    # compression, subpx, pad
                       font['underline_pos'], font['underline_thk'])

    # cmap section
    cmap_headers = []
    cmap_data = []
    data_off = 12 + 16 * font['cmap_num']
    for c in font['cmaps']:
        blobs = []
        if c['type'] == 0:      # format0 full: u8 id deltas
            blobs.append(c['gol_bytes'])
        elif c['type'] == 1:    # sparse full: u16 codes + u16 ids
            blobs.append(c['ul_bytes'])
            blobs.append(c['gol_bytes'])
        elif c['type'] == 3:    # sparse tiny: u16 codes
            blobs.append(c['ul_bytes'])
        # format0 tiny: no data
        c['data_off'] = data_off
        for blob in blobs:
            cmap_data.append(blob + b'\x00' * (align4(len(blob)) - len(blob)))
            data_off += align4(len(blob))
        cmap_headers.append(struct.pack('<IIHHHBB', c['data_off'], c['start'],
                                        c['rlen'], c['gid_start'], c['entries'],
                                        c['type'], 0))

    cmap_body = b''.join(cmap_headers) + b''.join(cmap_data)
    cmap = struct.pack('<I4sI', 12 + len(cmap_body), b'cmap', font['cmap_num']) + cmap_body

    # loca section
    if len(glyf_payload) > 65535:
        loca_offs = b''.join(struct.pack('<I', o) for o in offsets)
    else:
        loca_offs = b''.join(struct.pack('<H', o) for o in offsets)
    loca = struct.pack('<I4sI', 12 + len(loca_offs), b'loca', n) + loca_offs

    # glyf section
    glyf = struct.pack('<I4s', 8 + len(glyf_payload), b'glyf') + glyf_payload

    # kern section
    kern_buf = b''
    if kern:
        if kern['classes']:
            payload = struct.pack('<HBB', n, kern['rows'], kern['cols']) \
                + kern['left'] + kern['right'] + kern['values']
        else:
            payload = struct.pack('<I', kern['pair_cnt']) + kern['ids'] + kern['values']
        payload += b'\x00' * (align4(len(payload)) - len(payload))
        kern_buf = struct.pack('<I4sB3x', 12 + len(payload), b'kern',
                               3 if kern['classes'] else 0) + payload

    return head + cmap + loca + glyf + kern_buf


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', help='cbin font (e.g. font_puhui_common_20_4.bin)')
    ap.add_argument('-o', '--output', help='output .fnt path')
    args = ap.parse_args()

    font = parse_cbin(args.input)

    # carry cmap blob bytes from the source image into the cmap table
    d = open(args.input, 'rb').read()
    dsc_ofs = struct.unpack_from('<I', d, 24)[0]
    cmap_off = struct.unpack_from('<I', d, dsc_ofs + 8)[0]
    cmap_base = dsc_ofs + cmap_off
    for c in font['cmaps']:
        def blob(off, size):
            return d[cmap_base + off:cmap_base + off + size] if off else b''
        c['gol_bytes'] = blob(c['gol_off'], c['rlen'] if c['type'] == 0 else c['list_len'] * 2)
        c['ul_bytes'] = blob(c['ul_off'], c['list_len'] * 2)
        c['entries'] = c['rlen'] if c['type'] in (0, 2) else c['list_len']
        c['data_off'] = 0  # filled in build_fnt

    fnt = build_fnt(font)

    out = args.output or args.input.rsplit('.', 1)[0] + '.fnt'
    with open(out, 'wb') as f:
        f.write(fnt)

    vi = (list(range(0x102, 0x104)) + list(range(0x110, 0x112))
          + list(range(0x1A0, 0x1A2)) + list(range(0x1AF, 0x1B1))
          + list(range(0xC2, 0xC3)) + list(range(0xCA, 0xCB))
          + list(range(0xD4, 0xD5)) + list(range(0xEA, 0xEB))
          + list(range(0xF4, 0xF5)) + list(range(0x1EA0, 0x1EFA)))
    vi_missing = [c for c in vi if c not in font['covered']]
    print(f'{args.input} -> {out}')
    print(f'  {len(font["glyphs"])} glyphs, bpp={font["bpp"]}, '
          f'line_height={font["line_height"]}, base_line={font["base_line"]}')
    print(f'  {font["cmap_num"]} cmaps, kern={"classes" if font["kern"] and font["kern"]["classes"] else ("pairs" if font["kern"] else "none")}')
    if vi_missing:
        print(f'  vietnamese: MISSING {len(vi_missing)} chars, e.g. '
              + ' '.join(f'U+{c:04X}' for c in vi_missing[:8]))
    else:
        print('  vietnamese: full')
    print(f'  size {len(fnt)} bytes')


if __name__ == '__main__':
    main()
