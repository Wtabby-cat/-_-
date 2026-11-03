"""Independent structural checks and a limited offline event interpreter.
This does not replace compilation in USART HMI or device testing.
"""
from pathlib import Path
import struct,re,json,hashlib

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'加热1.HMI'
TARGET=ROOT.parent/'加热1_调试版_V9.HMI'
U=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
def check_directory(data,base=0):
    end=base+4+28*U(data,base)
    assert end+4<=len(data)
    payload=data[base:end]+b'ADEC';value=0xffffffff
    for i in range(0,len(payload),4):
        value^=U(payload,i)
        for _ in range(32):value=((value<<1)^(0x04c11db7 if value&0x80000000 else 0))&0xffffffff
    assert value==U(data,end),f'Directory checksum mismatch at {base:#x}: stored={U(data,end):08x}, expected={value:08x}'
    return value
def unpack(path):
    data=path.read_bytes();out={};spans=[]
    check_directory(data);check_directory(data,0x80000)
    for i in range(U(data)):
        name,off,size,flag=struct.unpack_from('<16sIII',data,4+i*28)
        if flag&255:continue
        assert off>=0x700000 and off+size<=len(data)
        name=name.rstrip(b'\0').decode('ascii');assert name not in out
        out[name]=data[off:off+size];spans.append((off,size))
    return data,out,spans
def crc(seed,b):
    for v in b:
        seed^=v
        for _ in range(32):seed=((seed<<1)^(0x04c11db7 if seed&0x80000000 else 0))&0xffffffff
    return seed
def checksum(blob,main=False):
    value=crc(0xffffffff,blob[4:])
    spans=[(16,20),(4,8),(10,11),(14,15)] if main else [(4,8),(12,16),(20,21),(21,22)]
    for a,z in spans:value=crc(value,blob[a:z])
    assert value==U(blob),'CRC mismatch'
def component(b):
    items=[];pos=0
    while pos<len(b):
        n=U(b,pos);assert pos+4+n<=len(b);items.append(b[pos+4:pos+4+n]);pos+=4+n
    assert items[-1]==b'';assert items[0].startswith(b'att-')
    count=int(items[0][4:]);a={}
    for q in items[1:1+count]:
        name=q[:16].rstrip(b'\0').decode('ascii');value=q[16:]
        a[name]=value.decode('gbk') if name in ('objname','txt') else int.from_bytes(value,'little')
    events={};pos=1+count
    while pos<len(items)-1:
        marker=items[pos].decode('ascii');name,n=marker.rsplit('-',1);n=int(n);pos+=1
        assert name.startswith('codes');events[name]=[q.decode('gbk') for q in items[pos:pos+n]];pos+=n
    assert pos==len(items)-1
    return dict(attr=a,events=events)
def page(blob):
    checksum(blob);assert U(blob,4)==len(blob) and U(blob,8)==56
    objects=[];last=12*U(blob,12)
    for i in range(U(blob,12)):
        off,size,flag=struct.unpack_from('<III',blob,56+i*12);assert off==last and flag==0
        objects.append(component(blob[56+off:56+off+size]));last+=size
    assert 56+last==len(blob)
    return objects

class Jump(Exception):
    def __init__(self,target):self.target=target

class Runtime:
    def __init__(self,pages,variables):self.pages=pages;self.g=variables.copy();self.pid=0;self.enter_count=0;self.refresh_stopped=False;self.presented_rows=[];self.tx=bytearray();self.trace=[];self.enter(0)
    def get(self,key):
        if re.fullmatch(r'-?\d+',key):return int(key)
        if '.' in key:
            obj,prop=key.split('.');return self.objs[obj]['attr'][prop]
        return self.g[key]
    def value(self,expr):
        terms=re.findall(r'"[^"\r\n]*"|[+*/%-]|[^+*/%\-]+',expr)
        def operand(t):return t[1:-1] if t.startswith('"') and t.endswith('"') else self.get(t)
        if terms[0]=='':terms=['0']+terms[1:]
        if terms[0]=='-':terms=['0']+terms
        v=operand(terms[0])
        for i in range(1,len(terms),2):
            op,rhs=terms[i],operand(terms[i+1])
            if op=='+':v+=rhs
            elif op=='-':v-=rhs
            elif op=='*':v*=rhs
            elif op=='/':v=int(v/rhs)
            else:v%=rhs
        return v
    def condition(self,s):
        if '||' in s:return any(self.condition(p) for p in s.split('||'))
        if '&&' in s:return all(self.condition(p) for p in s.split('&&'))
        a,op,b=re.split('(==|!=|>=|<=|>|<)',s);a=self.value(a);b=self.value(b)
        return {'==':a==b,'!=':a!=b,'>=':a>=b,'<=':a<=b,'>':a>b,'<':a<b}[op]
    def set(self,key,v):
        if '.' in key:
            obj,prop=key.split('.');assert obj in self.objs and prop in self.objs[obj]['attr'];self.objs[obj]['attr'][prop]=v
        else:assert key in self.g;self.g[key]=v
    def command(self,s):
        self.trace.append((self.pid,s,self.refresh_stopped))
        if s=='doevents':return
        if s.startswith('printh '):self.tx.extend(bytes.fromhex(s[7:]));return
        if s.startswith('prints '):
            name,length=s[7:].split(',');self.tx.extend((self.get(name)&0xffffffff).to_bytes(4,'little')[:int(length)]);return
        if s=='ref_stop':self.refresh_stopped=True;return
        if s=='ref_star':
            self.refresh_stopped=False
            if self.pid==1:self.presented_rows.append(tuple(i+1 for i in range(12) if self.visible['bRow'+str(i)]))
            return
        if s.startswith('page '):raise Jump(self.value(s[5:]))
        if s.startswith('click '):
            name,event=s[6:].split(',');assert event in ('0','1')
            self.execute(self.objs[name]['events']['codesup' if event=='0' else 'codesdown']);return
        if s.startswith('ref '):assert s[4:] in self.objs;return
        if s.startswith('covx '):
            src,dst,length,fmt=s[5:].split(',');assert fmt=='0';v=self.value(src);self.set(dst,str(v) if dst.endswith('.txt') else int(v));return
        if s.startswith('strlen '):
            src,dst=s[7:].split(',');self.set(dst,len(self.get(src)));return
        if s.startswith('tsw '):
            obj,on=s[4:].split(',');assert obj in self.objs;self.enabled[obj]=bool(int(on));return
        if s.startswith('vis '):
            obj,on=s[4:].split(',');assert obj in self.objs;self.visible[obj]=bool(int(on));return
        if s.startswith(('line ','fill ')):
            kind,args=s.split(' ',1);v=[self.value(x) for x in args.split(',')]
            assert len(v)==5 and 0<=v[-1]<=65535
            if kind=='line':assert all(0<=n<limit for n,limit in zip(v[:4],[800,480,800,480])),v
            else:assert v[0]>=0 and v[1]>=0 and v[0]+v[2]<=800 and v[1]+v[3]<=480
            self.drawing.append((kind,v))
            return
        assert '=' in s,s
        key,expr=s.split('=',1);self.set(key,self.value(expr))
    def execute(self,raw):
        lines=[]
        for line in raw:lines+=line.replace('}else','}\nelse').splitlines()
        def block(i,active,closing=False):
            while i<len(lines):
                s=lines[i].strip()
                if s=='}':assert closing;return i+1
                if s.startswith('if('):
                    assert s.endswith(')') and lines[i+1]=='{'
                    yes=self.condition(s[3:-1]) if active else False;i=block(i+2,active and yes,True)
                    if i<len(lines) and lines[i]=='else':
                        assert lines[i+1]=='{';i=block(i+2,active and not yes,True)
                    continue
                assert s not in ['{','else']
                if active and s:self.command(s)
                i+=1
            assert not closing
            return i
        block(0,True)
    def enter(self,pid):
        self.enter_count+=1
        assert pid in self.pages;self.pid=pid;self.objs={o['attr']['objname']:json.loads(json.dumps(o)) for o in self.pages[pid]};self.enabled={k:True for k in self.objs};self.visible={k:True for k in self.objs};self.drawing=[]
        root=next(iter(self.objs.values()))
        try:self.execute(root['events']['codesload']);self.execute(root['events']['codesloadend'])
        except Jump as jump:self.enter(jump.target)
    def click(self,name):
        assert name in self.objs
        if not self.enabled[name] or not self.visible[name]:return
        try:self.execute(self.objs[name]['events']['codesup'])
        except Jump as jump:self.enter(jump.target)
    def tick(self,seconds=1):
        for _ in range(seconds):
            for obj in list(self.objs.values()):
                if obj['attr']['type']==51 and obj['attr']['en']:
                    assert obj['attr']['tim']==200
                    try:self.execute(obj['events']['codestimer'])
                    except Jump as jump:self.enter(jump.target)
