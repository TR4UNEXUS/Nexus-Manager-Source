#!/usr/bin/env python3
"""Nexus diagnostic receiver. Run behind HTTPS; no public report-reading API."""
import hashlib,json,os,re,shutil,tempfile,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
MAX_BODY=128*1024
ROOT=Path(os.environ.get('NEXUS_REPORT_ROOT','/var/lib/nexus-reports'))
CATALOG=Path(os.environ.get('NEXUS_REPORT_CATALOG','/opt/nexus-report-server/catalog.nxrc'))
MAX_FILES=1000;MAX_BYTES=100*1024*1024;RETENTION=30*86400
LOCK=threading.Lock()
KINDS={'install','transfer','delete','repair','import','manual'}
MANAGER=re.compile(r'NM2 [0-9TZ:-]{20} version=[0-9.]{1,15} session=[0-9a-fA-F]{16} uptime_ms=[0-9]{1,20} operation=[0-9]{1,20} task=[A-Za-z0-9_.-]{1,64} event=[A-Za-z0-9_.-]{1,192} rc=0x[0-9a-fA-F]{8} value=[0-9]{1,20}')
PREFIXES=('usb_transport_','usb_live_','usb_startup_','usb_wake_','usb_last_','usb_first_live_','usb_completion_','loader_first_nonzero_')
STATUS=re.compile(r'usb_query=0x[0-9A-F]{8} mounted=\d{1,3} ready=\d{1,3} state=\d{1,3} usb_result=0x[0-9A-F]{8} backup_query=0x[0-9A-F]{8} backup_flags=\d{1,10} backup_result=0x[0-9A-F]{8}')
def pairs(items):
 d={}
 for k,v in items:
  if k in d:raise ValueError('duplicate field')
  d[k]=v
 return d
def validate(raw):
 if not raw or len(raw)>MAX_BODY:raise ValueError('size')
 d=json.loads(raw,object_pairs_hook=pairs)
 if not isinstance(d,dict) or set(d)!={'schema','manager','kind','result','description','logs'}:raise ValueError('fields')
 if type(d['schema'])!=int or d['schema']!=1 or d['kind'] not in KINDS:raise ValueError('schema')
 if not isinstance(d['manager'],str) or not re.fullmatch(r'\d{1,3}\.\d{1,3}\.\d{1,3}',d['manager']):raise ValueError('version')
 if type(d['result'])!=int or not 0<=d['result']<2**32:raise ValueError('result')
 desc=d['description']
 if not isinstance(desc,str) or len(desc.encode())>2000 or any(ord(c)<32 and c not in '\n\t' for c in desc):raise ValueError('description')
 if re.search(r'-----BEGIN .*PRIVATE KEY|(?:password|passwd|authorization|api[_ -]?key|secret|token)\s*[:=]',desc,re.I):raise ValueError('private-description')
 if d['kind']!='manual' and desc:raise ValueError('automatic-description')
 logs=d['logs']
 if not isinstance(logs,dict) or set(logs)!={'manager','firmware','status'} or not all(isinstance(v,str) for v in logs.values()):raise ValueError('logs')
 if len(logs['manager'].encode())>48*1024 or len(logs['manager'].splitlines())>80 or any(not MANAGER.fullmatch(line) for line in logs['manager'].splitlines()):raise ValueError('manager-log')
 if len(logs['firmware'].encode())>24*1024:raise ValueError('firmware-size')
 for line in logs['firmware'].splitlines():
  key,sep,value=line.partition('=')
  if not sep:raise ValueError('firmware-line')
  if key in ('build','build_id'):
   if not re.fullmatch(r'[A-Za-z0-9_.-]{1,96}',value):raise ValueError('build')
  elif not ((key.startswith(PREFIXES) or key in ('io_read_us','io_write_us','io_sync_us')) and re.fullmatch(r'[A-Za-z0-9_.-]{1,80}',key) and re.fullmatch(r'(?:0x[0-9A-Fa-f]{1,16}|[0-9]{1,20})',value)):raise ValueError('firmware-field')
 if not STATUS.fullmatch(logs['status']):raise ValueError('status')
 return d
def store(raw):
 validate(raw);identity=hashlib.sha256(raw).hexdigest()
 with LOCK:
  ROOT.mkdir(mode=0o700,parents=True,exist_ok=True)
  target=ROOT/(identity+'.json')
  if target.exists():
   if target.is_symlink() or target.read_bytes()!=raw:raise ValueError('existing-report')
   return identity,False
  now=time.time();files=[]
  for p in ROOT.iterdir():
   if not re.fullmatch(r'[0-9a-f]{64}\.json',p.name) or not p.is_file() or p.is_symlink():continue
   stat=p.stat()
   if now-stat.st_mtime>RETENTION:p.unlink()
   else:files.append((stat.st_mtime,stat.st_size,p))
  total=sum(row[1] for row in files)
  for _,size,p in sorted(files):
   if len(files)<MAX_FILES and total+len(raw)<=MAX_BYTES:break
   p.unlink();total-=size;files.remove((_,size,p))
  if shutil.disk_usage(ROOT).free<512*1024*1024:raise OSError('low-space')
  fd,name=tempfile.mkstemp(prefix='.pending-',dir=ROOT)
  try:
   with os.fdopen(fd,'wb') as f:f.write(raw);f.flush();os.fsync(f.fileno())
   os.replace(name,target)
   directory=os.open(ROOT,os.O_RDONLY)
   try:os.fsync(directory)
   finally:os.close(directory)
  finally:
   if os.path.exists(name):os.unlink(name)
 return identity,True
class Handler(BaseHTTPRequestHandler):
 server_version='NexusReports/1';sys_version=''
 def log_message(self,*args):pass # Report contents and client addresses are not access logs.
 def reply(self,code,data,content_type='application/json'):
  if not isinstance(data,bytes):data=json.dumps(data,separators=(',',':')).encode()
  self.send_response(code);self.send_header('Content-Type',content_type);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers()
  if self.command!='HEAD':self.wfile.write(data)
 def do_GET(self):
  if self.path=='/nexus-reports/health':return self.reply(200,{'status':'ok','schema':1})
  if self.path=='/nexus-reports/v1/catalog':
   try:
    size=CATALOG.stat().st_size
    if not 272<size<=1024*1024:raise ValueError('catalogue size')
    data=CATALOG.read_bytes()
    if not data.startswith(b'NXRCAT1\0'):raise ValueError('catalogue format')
    return self.reply(200,data,'application/octet-stream')
   except (OSError,ValueError):return self.reply(503,{'error':'catalogue-unavailable'})
  self.reply(404,{'error':'not-found'})
 do_HEAD=do_GET
 def do_POST(self):
  if self.path!='/nexus-reports/v1/reports':return self.reply(404,{'error':'not-found'})
  if self.headers.get('Transfer-Encoding') or self.headers.get_content_type()!='application/json':return self.reply(415,{'error':'json-required'})
  try:
   size=int(self.headers.get('Content-Length','0'))
   if not 0<size<=MAX_BODY:return self.reply(413,{'error':'report-too-large'})
   raw=self.rfile.read(size)
   if len(raw)!=size:raise ValueError('short-body')
   identity,created=store(raw)
   return self.reply(201 if created else 200,{'status':'stored','id':identity})
  except (ValueError,TypeError,KeyError,UnicodeError):return self.reply(422,{'error':'invalid-diagnostic-report'})
  except OSError:return self.reply(503,{'error':'storage-unavailable'})
 def setup(self):super().setup();self.connection.settimeout(20)
class Server(ThreadingHTTPServer):
 daemon_threads=True
 def __init__(self,*args,**kwargs):super().__init__(*args,**kwargs);self.slots=threading.BoundedSemaphore(8);self.last_cleanup=0
 def service_actions(self):
  if time.monotonic()-self.last_cleanup<60:return
  self.last_cleanup=time.monotonic()
  try:
   with LOCK:
    if not ROOT.exists():return
    now=time.time()
    for p in ROOT.iterdir():
     if re.fullmatch(r'[0-9a-f]{64}\.json',p.name) and not p.is_symlink() and p.is_file() and now-p.stat().st_mtime>RETENTION:p.unlink()
  except OSError:pass # Storage errors cannot expose data or stop the listener.
 def process_request(self,request,address):
  if not self.slots.acquire(False):request.close();return
  try:super().process_request(request,address)
  except BaseException:self.slots.release();raise
 def process_request_thread(self,*args):
  try:super().process_request_thread(*args)
  finally:self.slots.release()
if __name__=='__main__':
 Server(('127.0.0.1',int(os.environ.get('NEXUS_REPORT_PORT','8791'))),Handler).serve_forever()
