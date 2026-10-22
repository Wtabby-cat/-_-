"""Independent structural checks and a limited offline event interpreter.
This does not replace compilation in USART HMI or device testing.
"""
from pathlib import Path
import struct,re,json,hashlib

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'加热1.HMI'
TARGET=ROOT.parent/'加热1_界面版_V3.HMI'
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
    def __init__(self,pages,variables):self.pages=pages;self.g=variables.copy();self.pid=0;self.enter(0)
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
        if s.startswith('page '):raise Jump(self.value(s[5:]))
        if s.startswith('ref '):assert s[4:] in self.objs;return
        if s.startswith('covx '):
            src,dst,length,fmt=s[5:].split(',');assert fmt=='0';v=self.value(src);self.set(dst,str(v) if dst.endswith('.txt') else int(v));return
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
                    assert obj['attr']['tim']==1000
                    try:self.execute(obj['events']['codestimer'])
                    except Jump as jump:self.enter(jump.target)

def run():
    raw,files,spans=unpack(TARGET);original,old,_=unpack(SOURCE)
    assert raw[:8+28*U(raw)]==raw[0x80000:0x80000+8+28*U(raw)]
    assert all(a+n==b for (a,n),(b,m) in zip(spans,spans[1:]));assert sum(n for a,n in spans)+0x700000==len(raw)
    checksum(files['main.HMI'],True)
    for i in range(4,96):
        if i not in range(28,32):assert files['main.HMI'][i]==old['main.HMI'][i],i
    refs=files['main.HMI'][96:];assert len(refs)==16*U(files['main.HMI'],28)
    assert {refs[i+8:i+16].rstrip(b'\0').decode() for i in range(0,len(refs),16)}==set(files)-{'main.HMI','Program.s'}
    pages={int(name[:-3]):page(b) for name,b in files.items() if name.endswith('.pa')};assert len(pages)==8
    counts={};all_events=0
    for pid,objects in pages.items():
        names=[]
        for i,o in enumerate(objects):
            a=o['attr'];assert a['id']==i;name=a['objname'];assert len(name.encode('gbk'))<=14;assert name not in names;names.append(name)
            if a['type']!=51:
                assert 0<=a['x']<800 and 0<=a['y']<480 and a['x']+a['w']<=800 and a['y']+a['h']<=480,(pid,name,a)
                assert a['endx']==a['x']+a['w']-1 and a['endy']==a['y']+a['h']-1
            if 'txt' in a:
                assert len(a['txt'].encode('gbk'))<=a['txt_maxl'];assert f'{a["font"]}.zi' in files
            for lines in o['events'].values():
                all_events+=bool(lines)
                assert not any(re.match(r'(printh|prints|print |wepo|repo|whmi|baud|connect)',s) for s in lines)
        counts[pid]=len(objects)-1
    variables={k:int(v) for k,v in re.findall(r'^int (\w+)=(-?\d+)\r?$',files['Program.s'].decode('gbk'),re.M)}
    tests=[]
    def passed(name):tests.append(name)
    corrupt=bytearray(raw);corrupt[4+28*U(raw):8+28*U(raw)]=b'\0'*4
    try:check_directory(corrupt)
    except AssertionError:pass
    else:raise AssertionError('Regression: missing directory checksum was accepted')
    passed('主目录/备份目录校验；拒绝缺少目录校验码的旧版文件')
    rt=Runtime(pages,variables)
    for start in range(7):
        for btn,dst in [('bNav0',0),('bNav1',1),('bNav2',2),('bNav3',5)]:rt.enter(start);rt.click(btn);assert rt.pid==dst
    for start in [2,3,4,6]:
        for j,dst in enumerate([2,3,4,6]):rt.enter(start);rt.click('bTab'+str(j));assert rt.pid==dst
    passed('所有主导航与四个曲线页签跳转')
    rt.enter(1);rt.click('bplim');assert rt.pid==7
    for n in [2,0,0,0]:rt.click('k'+str(['7','8','9','4','5','6','1','2','3','0'].index(str(n))))
    rt.click('bOK');assert rt.pid==1 and rt.g['plim']==2000 and rt.objs['bplim']['attr']['txt']=='2000'
    rt.click('bplim');rt.click('k10');rt.click('bOK');assert rt.pid==7 and rt.g['plim']==2000
    rt.click('bCancel');assert rt.pid==1 and rt.g['plim']==2000
    passed('数字键盘输入、提交、下限校验和取消')
    for i in range(3):
        rt.click('bMode');assert all(rt.visible['bOpt'+str(j)] for j in range(3))
        rt.click('bOpt'+str(i));assert rt.g['mode']==i and not rt.visible['bOpt0']
        assert ['LADRC','PID','模糊LADRC'][i] in rt.objs['bMode']['attr']['txt']
    rt.click('bMode');rt.click('bMode');assert not rt.visible['bOpt0']
    passed('算法下拉展开/收起与 LADRC、PID、模糊 LADRC 三项选择')
    assert rt.g['nseg']==3
    rt.click('bRow0');rt.click('bAdd')
    assert rt.g['nseg']==4 and rt.g['tg2']==120 and rt.g['tg3']==90 and rt.g['hd4']==600
    rt.click('bRemove');assert rt.g['nseg']==3 and rt.g['tg2']==90 and rt.g['tg3']==120
    for _ in range(5):rt.click('bAdd')
    assert rt.g['nseg']==8
    rt.click('bAdd');assert rt.g['nseg']==8
    rt.click('bPrev');assert rt.visible['btg1'] and not rt.visible['btg5']
    rt.click('bNext');assert rt.visible['btg5'] and not rt.visible['btg1']
    rt.click('bRow7');rt.click('bRemove');assert rt.g['nseg']==7 and rt.g['sel']==7
    for _ in range(6):rt.click('bRemove')
    assert rt.g['nseg']==1 and rt.g['sel']==1 and rt.g['stagepage']==0
    rt.click('bRemove');assert rt.g['nseg']==1
    rt.click('bAdd');rt.click('bAdd');assert rt.g['nseg']==3
    passed('任意选中行插入/删除、参数移动、1 至 8 段上下限与分页')
    rt.enter(0);rt.click('bStart');assert rt.g['run']==1 and not rt.enabled['bStart']
    rt.enter(1);rt.click('bplim');assert rt.pid==1;rt.click('bAdd');assert rt.g['nseg']==3
    rt.click('bStop');assert rt.g['run']==0;rt.click('bplim');assert rt.pid==7
    passed('演示启动/停止及运行中参数锁定')
    rt=Runtime(pages,variables)
    rt.g.update(hd1=2,hd2=3,hd3=1)
    rt.enter(0);assert rt.objs['tStage']['attr']['txt']=='0 / 3'
    rt.click('bStart');assert rt.objs['tStage']['attr']['txt']=='1 / 3' and rt.objs['tTarget']['attr']['txt']=='60.0'
    rt.tick(2);assert rt.g['stage']==2 and rt.objs['tTarget']['attr']['txt']=='90.0' and rt.objs['tRemain']['attr']['txt']=='4 s'
    rt.enter(3);rt.tick(1);rt.click('bwpower');assert rt.pid==7
    rt.tick(2);assert rt.g['stage']==3
    rt.click('bCancel');rt.enter(0);assert rt.objs['tStage']['attr']['txt']=='3 / 3'
    rt.tick(1);assert rt.g['done']==1 and rt.g['run']==0 and rt.objs['tRemain']['attr']['txt']=='0 s'
    assert rt.objs['tPhase']['attr']['txt']=='已完成' and rt.enabled['bStart']
    rt.click('bStart');rt.tick(1);rt.click('bStop');stopped=rt.g['totalSec'];rt.tick(3);assert rt.g['totalSec']==stopped
    passed('1 秒定时切段、目标/剩余时间联动、跨页面和键盘持续计时、完成与停止')
    rt.enter(4);rt.click('bflo');rt.g['editval']=30;rt.click('bOK');assert rt.pid==7 and rt.g['flo']==15
    rt.g['editval']=10;rt.click('bOK');assert rt.pid==4 and rt.g['flo']==10
    passed('搜频上下限交叉校验')
    rt.enter(5);rt.click('bSave');assert '全部' in rt.objs['tFoot']['attr']['txt']
    rt.g.update(ov=400,uv=450,oc=30,ot1=90,ot2=90,ot3=160);rt.click('bSave');assert '欠压' in rt.objs['tFoot']['attr']['txt']
    rt.g['uv']=200;rt.click('bSave');assert '暂存' in rt.objs['tFoot']['attr']['txt']
    passed('保护参数空值与欠压/过压关系校验')
    windows={2:'wtemp',3:'wpower',4:'wfreq',6:'wphase'}
    for pid in [2,3,4,6]:
        var=windows[pid];rt.enter(pid);rt.click('b'+var);assert rt.pid==7
        rt.g['editval']=123;rt.click('bOK');assert rt.pid==pid and rt.g[var]==123
        labels=[o['attr']['txt'] for name,o in rt.objs.items() if name.startswith('tx')]
        assert labels==['0','61.5','123'],labels
        rt.click('b'+var);rt.g['editval']=0;rt.click('bOK');assert rt.pid==7 and rt.g[var]==123
        rt.g['editval']=86401;rt.click('bOK');assert rt.pid==7
        rt.g['editval']=86400;rt.click('bOK');assert rt.pid==pid
        rt.click('bFreeze');assert rt.g['frozen']==1;rt.click('bLive');assert rt.g['frozen']==0 and rt.g[var]==86400
        for window in [1,2,59,60,600,86400]:rt.g[var]=window;rt.enter(pid)
    assert [rt.g[v] for v in windows.values()]==[86400]*4
    passed('四页独立秒制时间窗、奇数刻度、1 至 86400 秒范围、绘图缩放边界')
    for target in [1,60,999]:
        for n in range(1,9):
            rt.g['nseg']=n
            for i in range(1,9):rt.g['tg'+str(i)]=target;rt.g['hd'+str(i)]=9999
            rt.enter(1)
    passed('温段参数变化后的程序预览缩放与边界')
    assert not any(re.match(r'rp\d+',k) for k in variables)
    rt.enter(4);assert rt.objs['tLocked']['attr']['txt']=='已锁定' and rt.objs['uFreq']['attr']['txt']=='kHz'
    assert rt.objs['uFreq']['attr']['y']<rt.objs['tLocked']['attr']['y']
    passed('移除升温时间变量，频率单位和锁定状态分离')
    report=dict(status='PASS',scope='离线结构与事件子集校验；不替代官方编辑器编译和实屏测试',pages=counts,events=all_events,tests=tests,model_crc=files['main.HMI'][16:20].hex(),source_sha256=hashlib.sha256(original).hexdigest(),output_sha256=hashlib.sha256(raw).hexdigest())
    (ROOT/'验证报告.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8')
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':run()
