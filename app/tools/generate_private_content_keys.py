"""PRIVATE opt-in generator; never print key values or copy a complete key file.

Only eight derived NCA key-area/title KEKs (15/16) are embedded. Master,
header, device, ticket-RSA and Mariko boot secrets are deliberately excluded.
Generated material must remain under this checkout's ignored build folder.
"""
import argparse, re
from pathlib import Path

FAMILIES=('key_area_key_application','key_area_key_ocean','key_area_key_system','titlekek')
NAMES=tuple(f'{f}_{g:02x}' for g in (0x15,0x16) for f in FAMILIES)

def read_selected(path):
    p=Path(path)
    if p.stat().st_size>1024*1024:raise ValueError('key_file_size')
    values={}
    for line in p.read_text(encoding='utf-8-sig').splitlines():
        name,sep,value=line.partition('=');name=name.strip()
        if not sep or name not in NAMES:continue
        value=value.strip()
        if not re.fullmatch('[0-9a-fA-F]{32}',value):raise ValueError('key_format')
        raw=bytes.fromhex(value)
        if not any(raw):raise ValueError('empty_key')
        if name in values and values[name]!=raw:raise ValueError('conflicting_key')
        values[name]=raw
    if set(values)!=set(NAMES):raise ValueError('required_key_missing')
    return values

def render(values):
    if set(values)!=set(NAMES) or any(len(v)!=16 or not any(v) for v in values.values()):raise ValueError('invalid_keys')
    rows=[]
    for g in (0x15,0x16):
        entries=['{{'+','.join(f'0x{b:02x}' for b in values[f'{f}_{g:02x}'])+'}}' for f in FAMILIES]
        rows.append('{'+','.join(entries)+'}')
    return '''// PRIVATE GENERATED MATERIAL - DO NOT DISTRIBUTE
#pragma once
#include "yati/nx/keys.hpp"
namespace sphaira::keys {
inline unsigned ApplyPrivateContentKeyFallback(Keys& out) {
    static const KeyEntry material[2][4] = {'''+','.join(rows)+'''};
    unsigned filled=0;
    for (unsigned g=0;g<2;++g) {
        for (unsigned area=0;area<3;++area) {
            auto& target=out.key_area_key[area][0x15+g];
            if (!target.IsValid()) { target=material[g][area]; ++filled; }
        }
        auto& target=out.titlekek[0x15+g];
        if (!target.IsValid()) { target=material[g][3]; ++filled; }
    }
    return filled;
}
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--keys',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[1];out=Path(a.output).resolve()
    if not out.is_relative_to(root/'build') or out.name!='private_content_keys.hpp':raise ValueError('private_output_scope')
    content=render(read_selected(a.keys));out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(content,encoding='ascii')
    print('PRIVATE C4K: generated eight fallback slots; values withheld')
if __name__=='__main__':
    try:main()
    except Exception:
        print('PRIVATE C4K generation failed; secret details withheld');raise SystemExit(1)
