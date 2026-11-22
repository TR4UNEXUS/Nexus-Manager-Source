#!/usr/bin/env python3
import hashlib,http.client,importlib.util,json,os,tempfile,threading,time
from pathlib import Path
spec=importlib.util.spec_from_file_location('receiver',Path(__file__).with_name('receiver.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
status='usb_query=0x00000000 mounted=1 ready=1 state=4 usb_result=0x00000000 backup_query=0x00000000 backup_flags=1 backup_result=0x00000000'
sample={'schema':1,'manager':'0.2.28','kind':'manual','result':0,'description':'Synthetic deployment check. No user data.','logs':{'manager':'','firmware':'usb_live_write_failures=1\n','status':status}}
raw=json.dumps(sample,separators=(',',':')).encode()
with tempfile.TemporaryDirectory(prefix='nexus-receiver-') as tmp:
 r.ROOT=Path(tmp)/'reports';r.CATALOG=Path(tmp)/'catalog.nxrc'
 identity,created=r.store(raw);assert created and identity==hashlib.sha256(raw).hexdigest();assert r.store(raw)==(identity,False)
 assert (r.ROOT/(identity+'.json')).read_bytes()==raw
 for bad in [dict(sample,password='private'),dict(sample,kind='script'),dict(sample,result=-1),dict(sample,description='password=private'),dict(sample,logs={'keys':'private'}),dict(sample,kind='repair')]:
  try:r.validate(json.dumps(bad).encode());raise AssertionError('invalid data accepted')
  except ValueError:pass
 try:r.validate(b'{"schema":1,"schema":1}');raise AssertionError('duplicate accepted')
 except ValueError:pass
 bad=dict(sample,logs=dict(sample['logs'],firmware='usb_password=secret\n'))
 try:r.validate(json.dumps(bad).encode());raise AssertionError('private log accepted')
 except ValueError:pass
 server=r.Server(('127.0.0.1',0),r.Handler);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
 def request(method,path,data=None,headers={}):
  c=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=5);c.request(method,path,data,headers);response=c.getresponse();body=response.read();code=response.status;c.close();return code,body
 assert request('GET','/nexus-reports/health')[0]==200
 code,body=request('POST','/nexus-reports/v1/reports',raw,{'Content-Type':'application/json'});assert code==200 and json.loads(body)['id']==identity
 assert request('GET','/nexus-reports/v1/reports')[0]==404
 assert request('GET','/nexus-reports/'+identity+'.json')[0]==404
 assert request('POST','/nexus-reports/v1/reports',raw,{'Content-Type':'text/plain'})[0]==415
 assert request('POST','/nexus-reports/v1/reports',b'{'*200,{'Content-Type':'application/json'})[0]==422
 assert request('POST','/nexus-reports/v1/reports',b'x'*(r.MAX_BODY+1),{'Content-Type':'application/json'})[0]==413
 assert request('GET','/nexus-reports/v1/catalog')[0]==503
 r.CATALOG.write_bytes(b'NXRCAT1\0'+b'x'*300);assert request('GET','/nexus-reports/v1/catalog')[1]==r.CATALOG.read_bytes()
 # Retention removes only receiver-owned report names, never unrelated files.
 unrelated=r.ROOT/'keep.txt';unrelated.write_text('keep')
 os.utime(r.ROOT/(identity+'.json'),(1,1));new=dict(sample,description='Second synthetic report.');r.store(json.dumps(new).encode());assert not (r.ROOT/(identity+'.json')).exists() and unrelated.read_text()=='keep'
 # Expiry also runs without a new upload.
 for f in r.ROOT.glob('*.json'):os.utime(f,(1,1))
 server.last_cleanup=0;server.service_actions();assert not list(r.ROOT.glob('*.json')) and unrelated.exists()
 server.shutdown();server.server_close();worker.join()
print('PASS: receipt/deduplication, strict schema and private-field rejection, bounded HTTP bodies, private report storage, catalogue delivery and retention.')
