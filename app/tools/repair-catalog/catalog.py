#!/usr/bin/env python3
"""Validate and sign Nexus repair guidance. Private keys must stay outside source."""
from pathlib import Path
import argparse,json,struct,hashlib
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import padding,rsa
CONDITIONS={'missing-links':(1,1),'converted-metadata':(2,2),'incomplete-files':(3,3),'missing-registration':(4,4),'missing-ticket':(5,3),'missing-maps':(6,5)}
def text(value,limit):
 assert isinstance(value,str) and value and len(value.encode())<=limit
 assert all(ord(c)>=32 and ord(c)!=127 for c in value)
 b=value.encode();return struct.pack('<H',len(b))+b
def integer(value,maximum):
 assert type(value)==int and 0<=value<=maximum;return value
def encode(doc):
 assert set(doc)=={'schema','revision','manager_min','manager_max','rules'} and doc['schema']==1
 revision=integer(doc['revision'],2**64-1);assert revision
 low=integer(doc['manager_min'],2**32-1);high=integer(doc['manager_max'],2**32-1);assert 0<low<=high
 rules=doc['rules'];assert isinstance(rules,list) and 0<len(rules)<=2048
 body=struct.pack('<IQIII',1,revision,low,high,len(rules));ids=set()
 for r in rules:
  assert set(r)=={'id','application','condition','type','version_min','version_max','manager_min','content_id','sha256','title','advice'}
  assert r['id'] not in ids and all(c in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in r['id']);ids.add(r['id'])
  condition,action=CONDITIONS[r['condition']]
  app=int(r['application'],16);assert len(r['application'])==16 and 0<=app<2**64
  typ=integer(r['type'],255);assert typ in (0,0x80,0x81,0x82,0x83)
  vmin=integer(r['version_min'],2**32-1);vmax=integer(r['version_max'],2**32-1);assert vmin<=vmax
  manager=integer(r['manager_min'],2**32-1);assert manager
  cid=bytes.fromhex(r['content_id']);digest=bytes.fromhex(r['sha256']);assert len(cid)==16 and len(digest)==32
  assert bool(any(cid))==bool(any(digest))
  if any(cid):assert app and condition==2
  body+=text(r['id'],64)+struct.pack('<QBBBBIII',app,condition,action,typ,0,vmin,vmax,manager)+cid+digest+text(r['title'],160)+text(r['advice'],512)
 assert len(body)+272<=1024*1024
 return b'NXRCAT1\0'+struct.pack('<II',len(body),256)+body
def main():
 p=argparse.ArgumentParser();p.add_argument('input',type=Path);p.add_argument('--key',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--public-key',type=Path);p.add_argument('--header',type=Path);a=p.parse_args()
 # No signing keys under the app, an output directory, or a Git checkout.
 keypath=a.key.resolve();app=Path(__file__).resolve().parents[2]
 assert not keypath.is_relative_to(app) and not any((parent/'.git').exists() for parent in keypath.parents)
 assert 'outputs' not in [part.lower() for part in keypath.parts]
 key=serialization.load_pem_private_key(keypath.read_bytes(),password=None)
 assert isinstance(key,rsa.RSAPrivateKey) and key.key_size==2048 and key.public_key().public_numbers().e==65537
 doc=json.loads(a.input.read_text());signed=encode(doc)
 signature=key.sign(signed,padding.PSS(mgf=padding.MGF1(hashes.SHA256()),salt_length=32),hashes.SHA256())
 key.public_key().verify(signature,signed,padding.PSS(mgf=padding.MGF1(hashes.SHA256()),salt_length=32),hashes.SHA256())
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(signed+signature)
 if a.public_key:a.public_key.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
 if a.header:
  modulus=key.public_key().public_numbers().n.to_bytes(256,'big')
  a.header.write_text('// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\n#include <array>\n#include <cstdint>\nnamespace nexus::catalog {\ninline constexpr std::array<std::uint8_t,256> PublicModulus{\n'+''.join('    '+','.join('0x%02x'%b for b in modulus[i:i+16])+',\n' for i in range(0,256,16))+'};\n}\n')
 print(json.dumps({'revision':doc['revision'],'rules':len(doc['rules']),'bytes':len(signed)+256,'sha256':hashlib.sha256(signed+signature).hexdigest()}))
if __name__=='__main__':main()
