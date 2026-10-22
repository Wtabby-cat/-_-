"""Build an editable TJC HMI from the user's 1.67.6 project. No device I/O.

Binary layout references (implementation below is independent):
https://github.com/eseverson/nextion-hmi/blob/main/findings/format-hmi.md
https://github.com/hagronnestad/nextion-font-editor/tree/master/Docs
The original project is read only. All output is written beside this script.
"""
from pathlib import Path
from collections import OrderedDict
import struct, math, json, hashlib, html
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'加热1.HMI'
OUTPUT=ROOT.parent/'加热1_界面版_V3.HMI'
U32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
P32=lambda v:struct.pack('<I',v)
DATA=SOURCE.read_bytes()
ENTRIES=[]
for i in range(U32(DATA)):
    o=4+28*i
    name=DATA[o:o+16].split(b'\0')[0].decode('ascii',errors='replace')
    a,n,f=struct.unpack_from('<III',DATA,o+16)
    if name and not f&255:ENTRIES.append((name,DATA[a:a+n],f))
FILES={n:b for n,b,f in ENTRIES}

TABLE=[]
for i in range(256):
    r=i<<24
    for _ in range(8):r=((r<<1)^(0x04c11db7 if r&0x80000000 else 0))&0xffffffff
    TABLE.append(r)
def crc(seed,data):
    for b in data:
        seed^=b
        for _ in range(4):seed=((seed<<8)&0xffffffff)^TABLE[seed>>24]
    return seed
def directory_crc(data):
    """CFS directory CRC32_T: little-endian words, including ADEC sentinel."""
    data=bytes(data)+b'ADEC'
    assert len(data)%4==0
    r=0xffffffff
    for i in range(0,len(data),4):
        r^=U32(data,i)
        for _ in range(4):r=((r<<8)&0xffffffff)^TABLE[r>>24]
    return r
def checksum(b,kind):
    r=crc(0xffffffff,b[4:])
    spans=[(4,8),(12,16),(20,21),(21,22)] if kind=='page' else [(16,20),(4,8),(10,11),(14,15)]
    for a,z in spans:r=crc(r,b[a:z])
    return r
def stamp(b,kind):
    b=bytearray(b);struct.pack_into('<I',b,0,checksum(b,kind));return bytes(b)
def records(blob):
    out=[]
    for i in range(U32(blob,12)):
        a,n,_=struct.unpack_from('<III',blob,56+i*12)
        out.append(blob[56+a:56+a+n])
    return out
def attrs(block):
    out=OrderedDict();o=0
    while o+4<=len(block):
        n=U32(block,o);p=block[o+4:o+4+n];o+=4+n
        if not n:break
        if p.startswith(b'codes'):break
        if p.startswith(b'att-'):continue
        out[p[:16].rstrip(b'\0').decode('ascii')]=p[16:]
    return out
TEMPLATES=[attrs(b) for b in records(FILES['0.pa'])]
assert checksum(FILES['0.pa'],'page')==U32(FILES['0.pa'])
assert checksum(FILES['main.HMI'],'main')==U32(FILES['main.HMI'])
assert int.from_bytes(TEMPLATES[0]['w'],'little')==800
assert int.from_bytes(TEMPLATES[0]['h'],'little')==480

WHITE='#ffffff';INK='#29313e';MUTED='#6d7785';BLUE='#137de1';TEAL='#005c65';RED='#d94b4b';GRAY='#e8eaed';LINE='#d6dbe1';GRID='#edf0f4';AMBER='#d58a14';GREEN='#00a191'
def rgb565(c):
    r,g,b=tuple(bytes.fromhex(c.lstrip('#')))
    return (r>>3)<<11|(g>>2)<<5|(b>>3)
def rgb888(c):
    return ((c>>11)*255//31,((c>>5)&63)*255//63,(c&31)*255//31)

def decode_glyph(data):
    p=[]
    for b in data[1:]:
        if b<32:p.extend([0]*(b&31))
        elif b<64:p.extend([255]*(b&31))
        elif b<128:p.extend([0]*(b&31)+[255]*(1+bool(b&32)))
        elif data[0]==1:
            if b<192:p.extend([0]*(b&31)+[255]*(3+bool(b&32)))
            else:p.extend([0]*((b>>3)&7)+[255]*(b&7))
        elif b<192:p.extend([0]*((b>>3)&7)+[round((b&7)*255/7)])
        else:p.extend([round(((b>>3)&7)*255/7),round((b&7)*255/7)])
    return p
def encode_glyph(im):
    px=[min(7,round(v*7/255)) for v in im.getdata()];out=bytearray([3]);i=0
    while i<len(px):
        if px[i] in (0,7):
            v=px[i];j=i+1
            while j<len(px) and px[j]==v and j-i<31:j+=1
            out.append((32 if v else 0)|(j-i));i=j
        elif i+1<len(px):out.append(192|(px[i]<<3)|px[i+1]);i+=2
        else:out.append(128|px[i]);i+=1
    assert decode_glyph(out)==[round(v*255/7) for v in px]
    return out

# Full GB2312 repertoire preserves arbitrary later controller messages.
original_font=FILES['0.zi'];fb=44+original_font[17]
GBKEYS=[original_font[fb+i*10:fb+i*10+2] for i in range(U32(original_font,12))]
FONTS={};GLYPHS={}
def makefont(fid,height,ascii_only=False,bold=False):
    cache=ROOT/'fonts'/f'ui2_{fid}.zi';cache.parent.mkdir(exist_ok=True)
    keys=[bytes([i,0]) for i in range(32,127)] if ascii_only else GBKEYS
    name=f'CFRP{height}'.encode('ascii');desc=name+(b'ascii' if ascii_only else b'gb2312')
    if cache.exists():
        data=cache.read_bytes()
    else:
        face=ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc' if bold else 'C:/Windows/Fonts/msyh.ttc',height-2)
        lookup=bytearray();payload=bytearray();start=len(keys)*10
        for key in keys:
            width=height//2 if key[1]==0 else height
            try:char=key.rstrip(b'\0').decode('gbk')
            except UnicodeDecodeError:char=' '
            if key[1]==0:width=max(5,math.ceil(face.getlength(char))+2)
            im=Image.new('L',(width,height))
            d=ImageDraw.Draw(im);box=d.textbbox((0,0),char,font=face)
            # All ASCII digits share a baseline. Chinese occupies the same line box.
            d.text(((width-face.getlength(char))/2,height-4),char,font=face,fill=255,anchor='ls')
            encoded=encode_glyph(im);offset=start+len(payload)
            lookup+=key+bytes([width,0,0])+offset.to_bytes(3,'little')+struct.pack('<H',len(encoded))
            payload+=encoded
        h=bytearray(original_font[:44]);h[4]=1 if ascii_only else 2;h[5]=0 if ascii_only else 1;h[6]=0;h[7]=height
        h[8:12]=bytes([0,0,32,126]) if ascii_only else original_font[8:12]
        struct.pack_into('<I',h,12,len(keys));h[17]=len(desc);struct.pack_into('<I',h,20,len(desc)+len(lookup)+len(payload))
        h[32]=len(name);struct.pack_into('<I',h,36,len(keys))
        data=bytes(h)+desc+lookup+payload;cache.write_bytes(data)
    FONTS[fid]=data;g={};base=44+data[17]
    for i in range(U32(data,12)):
        r=data[base+i*10:base+i*10+10];o=int.from_bytes(r[5:8],'little');n=int.from_bytes(r[8:10],'little');pixels=decode_glyph(data[base+o:base+o+n])
        assert len(pixels)==r[2]*data[7],(fid,i)
        im=Image.new('L',(r[2],data[7]));im.putdata(pixels);g[r[:2]]=im
    GLYPHS[fid]=g

PARAMS=[];GLOBALS=OrderedDict(run=0,field=0,backpg=1,editval=0,editmin=0,editmax=9999,fresh=1,nseg=3,mode=0,frozen=0,stagepage=0,sel=1,ddopen=0,stage=0,stageSec=0,totalSec=0,remain=0,hold=0,tgAct=0,done=0,saved=0,ack=0,tmp=0,sumt=0,ymax=150,elapsed=0,px=0,py=0,nx=0,ny=0)
def param(var,label,initial,unit,low,high):
    GLOBALS[var]=initial;PARAMS.append(dict(id=len(PARAMS)+1,var=var,label=label,initial=initial,unit=unit,low=low,high=high));return PARAMS[-1]
PLIM=param('plim','功率上限',1500,'W',1,9999)
STAGES=[]
for i in range(8):
    t=[60,90,120,120,120,120,120,120][i];h=[180,300,600,300,300,300,300,300][i]
    STAGES.append([param(f'tg{i+1}',f'温段{i+1}目标温度',t,'℃',1,999),param(f'hd{i+1}',f'温段{i+1}保温时间',h,'s',1,9999)])
WINDOWS={}
for pid,var,value in [(2,'wtemp',600),(3,'wpower',600),(4,'wfreq',60),(6,'wphase',60)]:
    WINDOWS[pid]=param(var,'曲线时间窗',value,'s',1,86400)
    WINDOWS[pid]['view_only']=True

FLO=param('flo','搜频下限',15,'kHz',1,99);FHI=param('fhi','搜频上限',25,'kHz',1,99)
PROTECT=[param(v,l,0,u,1,9999 if i<3 else 999) for i,(v,l,u) in enumerate([('ov','母线过压','V'),('uv','母线欠压','V'),('oc','谐振过流','A'),('ot1','功率管过温','℃'),('ot2','整流桥过温','℃'),('ot3','工件超温','℃')])]

PAGES=[]
class Page:
    def __init__(self,pid,name,title,nav):
        self.pid=pid;self.name=name;self.title=title;self.items=[];self.draw=[];self.load=[];self.nav=nav;self.plots=[];self.timer='';PAGES.append(self)
    def text(self,name,txt,x,y,w,h=28,font=0,color=INK,bg=WHITE,align=0,border=0,kind='text',code=''):
        item=dict(name=name,txt=str(txt),x=x,y=y,w=w,h=h,font=font,color=color,bg=bg,align=align,border=border,kind=kind,code=code)
        self.items.append(item);return item
    def button(self,name,txt,x,y,w,h=38,color=INK,bg=GRAY,code='',font=0,border=0):
        return self.text(name,txt,x,y,w,h,font,color,bg,1,border,'button',code)
    def line(self,x1,y1,x2,y2,c=LINE):self.draw.append(('line',x1,y1,x2,y2,c))
    def fill(self,x,y,w,h,c):
        # A background text object is editor-visible and repaints correctly.
        obj=dict(name='tBg'+str(len(self.items)),txt='',x=x,y=y,w=w,h=h,font=0,color=INK,bg=c,align=0,border=0,kind='text',code='')
        self.items.insert(0,obj)
    def field(self,p,x,y,w=108,h=34,disabled=False):
        name='b'+p['var'];value='—' if not p['initial'] else str(p['initial'])
        gate='1==1' if p.get('view_only') else 'run==0'
        code=f'if({gate})\r\n{{\r\nfield={p["id"]}\r\nbackpg={self.pid}\r\neditval={p["var"]}\r\neditmin={p["low"]}\r\neditmax={p["high"]}\r\npage 7\r\n}}'
        self.text('f'+p['var'],'',x-1,y-1,w+2,h+2,bg=LINE)
        self.button(name,value,x,y,w,h,bg=WHITE,border=1,code=code,font=2)
        self.load+= [f'covx {p["var"]},{name}.txt,0,0']
        if not p['initial']:self.load +=[f'if({p["var"]}==0)\r\n{{\r\n{name}.txt="—"\r\n}}']
        return name
    def frame(self):
        self.text('tTitle','CFRP感应加热 · '+self.title,12,4,510,36,bg=GRAY)
        self.text('tState','演示待机',568,5,98,34,font=2,color=TEAL,bg=GRAY,align=1)
        self.button('bStop','停止加热',674,4,114,34,color=WHITE,bg=RED,font=2,code=f'run=0\r\npage {self.pid}')
        self.fill(0,0,800,42,GRAY)
        self.text('tFoot','演示界面 · 主控未连接',12,416,390,20,font=3,color=MUTED)
        for i,(label,target) in enumerate([('运行总览',0),('分段温控',1),('曲线调试',2),('保护报警',5)]):
            self.button(f'bNav{i}',label,i*200,440,199,40,color=WHITE if i==self.nav else INK,bg=BLUE if i==self.nav else GRAY,code=f'page {target}')
        self.load +=['if(run==1)\r\n{\r\ntState.txt="演示运行"\r\n}']
    def tabs(self,selected):
        for i,(s,pid) in enumerate([('温度',2),('功率',3),('频率',4),('相位差',6)]):
            self.button(f'bTab{i}',s,16+i*192,52,188,32,color=WHITE if i==selected else INK,bg=BLUE if i==selected else GRAY,font=2,code=f'page {pid}')
    def curve_controls(self):
        self.text('lWindow','时间窗',16,382,70,30,font=3)
        self.field(WINDOWS[self.pid],92,380,96,34)
        self.text('uWindow','s',194,382,28,30,font=3)
        self.button('bFreeze','冻结显示',466,380,150,34,font=2,code='frozen=1\r\ntFoot.txt="演示曲线 · 已冻结显示"')
        self.button('bLive','恢复实时',632,380,152,34,font=2,color=WHITE,bg=BLUE,code=f'frozen=0\r\npage {self.pid}')
        self.load +=['if(frozen==1)\r\n{\r\ntFoot.txt="演示曲线 · 已冻结显示"\r\n}']

def plot(page,x,y,w,h,yrange,yticks,xticks,series,title,unit):
    meta=dict(x=x,y=y,w=w,h=h,yrange=yrange,series=series,drawstart=len(page.draw),yt=[],xt=[])
    page.text('tAxis'+str(len(page.items)),title,x-34,y-34,w,24,font=3)
    for v,label in yticks:
        yy=round(y+h-(v-yrange[0])*h/(yrange[1]-yrange[0]));page.line(x,yy,x+w,yy,GRID)
        name='ty'+str(len(page.items));page.text(name,label,x-58,yy-11,56,22,font=3,align=2);meta['yt'].append(name)
    for frac,label in xticks:
        xx=round(x+w*frac);page.line(xx,y,xx,y+h,GRID);name='tx'+str(len(page.items));page.text(name,label,min(734,xx-33),y+h+5,66,22,font=3,align=1);meta['xt'].append(name)
    page.line(x,y,x,y+h,LINE);page.line(x,y+h,x+w,y+h,LINE)
    page.text('tXUnit'+str(len(page.items)),unit,x+w//2-80,y+h+30,160,22,font=3,align=1)
    meta['gridend']=len(page.draw)
    for points,color in series:
        prev=None
        for fx,v in points:
            p=(round(x+fx*w),round(y+h-(v-yrange[0])*h/(yrange[1]-yrange[0])))
            if prev:page.line(*prev,*p,color)
            prev=p
    page.plots.append(meta)

# 0: overview
p=Page(0,'运行总览','运行总览',0);p.frame()
for idx,(label,value,unit,name) in enumerate([('实际温度','119.6','℃','tActual'),('目标温度','120.0','℃','tTarget'),('电功率（估算）','860','W','tPower'),('母线电压','308','V','tBus'),('工作频率','20.10','kHz','tFreq'),('谐振电流','18.2','A','tCurrent')]):
    x=28+(idx%3)*264;y=62+(idx//3)*106
    p.text('l'+name,label,x,y,240,28);p.text(name,value,x,y+31,170,54,font=1,color=TEAL);p.text('u'+name,unit,x+174,y+48,70,28)
for y in [152,258,314,368]:p.line(16,y,784,y)
for x in [268,532]:p.line(x,62,x,140);p.line(x,170,x,246);p.line(x,270,x,302)
for x,label,val,name in [(28,'当前温段','0 / 3','tStage'),(292,'当前阶段','待机','tPhase'),(550,'锁相状态','已锁定','tLock')]:
    p.text('l'+name,label,x,273,105,30,font=2);p.text(name,val,x+110,269,125,38,color=TEAL)
p.text('lElapsed','已运行',28,329,108,30);p.text('tElapsed','0 s',142,324,150,40,color=TEAL)
p.text('lRemain','预计剩余',400,329,115,30);p.text('tRemain','1080 s',526,324,150,40,color=TEAL)
p.button('bStart','启动加热',215,378,178,36,color=WHITE,bg=BLUE,code='run=1\r\nstage=1\r\nstageSec=0\r\ntotalSec=0\r\ndone=0\r\npage 0')
p.button('bSetup','温控设置',409,378,178,36,color=WHITE,bg=BLUE,code='page 1')
p.load +=['if(run==1)\r\n{\r\nbStart.bco=54970\r\ntsw bStart,0\r\n}']

# 1: boxed programme table. Eight slots, four rows per screen.
p=Page(1,'分段温控','分段温控',1);p.frame()
p.text('lPower','功率上限',16,56,92,30,font=2);p.field(PLIM,112,54,106);p.text('uPower','W',226,56,28,30,font=2)
p.text('lMode','控制算法',336,56,104,30,font=2)
p.text('fMode','',449,53,332,36,bg=LINE)
p.button('bMode','LADRC',450,54,330,34,font=2,bg=WHITE,code='if(run==0)\r\n{\r\nddopen=1-ddopen\r\nif(ddopen==1)\r\n{\r\nvis bOpt0,1\r\nvis bOpt1,1\r\nvis bOpt2,1\r\n}else\r\n{\r\npage 1\r\n}\r\n}')
p.load+=['ddopen=0','vis bOpt0,0','vis bOpt1,0','vis bOpt2,0']
p.load+=['ref tPreview']
for i,row in enumerate(STAGES):
    p.load += [f'vis fr{i},0',f'vis bRow{i},0']
    for pr in row:p.load += [f'vis f{pr["var"]},0',f'vis b{pr["var"]},0']
for i,label in enumerate(['LADRC','PID','模糊LADRC']):
    p.load +=[f'if(mode=={i})\r\n{{\r\nbMode.txt="{label}"\r\n}}']
for x,w,label in [(16,68,'温段'),(85,180,'目标温度 / ℃'),(266,190,'保温时间 / s')]:
    p.text('th'+str(x),label,x,102,w,32,font=2,bg=GRAY,align=1)
for i,row in enumerate(STAGES):
    y=136+i%4*44;names=[]
    p.text('fr'+str(i),'',16,y,440,43,bg=LINE);names.append('fr'+str(i))
    p.button('bRow'+str(i),str(i+1),17,y+1,66,41,font=2,bg=WHITE,code=f'sel={i+1}\r\npage 1');names.append('bRow'+str(i))
    for j,pr in enumerate(row):
        x=[94,278][j];p.field(pr,x,y+6,160,30);names += ['f'+pr['var'],'b'+pr['var']]
    p.load += [f'if(stagepage=={i//4}&&nseg>={i+1})','{']
    for name in names:p.load += [f'vis {name},1']
    p.load += [f'if(sel=={i+1})','{',f'bRow{i}.bco={rgb565(BLUE)}',f'bRow{i}.pco=65535','}','}']
add=['if(run==0&&nseg<8)','{']
for i in range(7,0,-1):
    add +=[f'if(sel<={i}&&nseg>={i})','{',f'tg{i+1}=tg{i}',f'hd{i+1}=hd{i}','}']
add += ['sel=sel+1','nseg=nseg+1']
for i in range(1,9):add +=[f'if(sel=={i})','{',f'tg{i}=120',f'hd{i}=300','}']
add += ['stagepage=sel-1','stagepage=stagepage/4','stage=0','done=0','page 1','}']
remove=['if(run==0&&nseg>1)','{']
for i in range(1,8):remove += [f'if(sel<={i}&&nseg>{i})','{',f'tg{i}=tg{i+1}',f'hd{i}=hd{i+1}','}']
remove += ['nseg=nseg-1','if(sel>nseg)','{','sel=nseg','}','stagepage=sel-1','stagepage=stagepage/4','stage=0','done=0','page 1','}']
p.button('bAdd','插入温段',16,324,120,34,font=2,code='\r\n'.join(add))
p.button('bRemove','删除选中',144,324,120,34,font=2,code='\r\n'.join(remove))
p.button('bPrev','上一页',276,324,84,34,font=3,code='stagepage=0\r\nsel=1\r\npage 1')
p.button('bNext','下一页',368,324,88,34,font=3,code='if(nseg>4)\r\n{\r\nstagepage=1\r\nsel=5\r\npage 1\r\n}')
p.text('tPreview','程序预览',496,100,280,26,font=2)
plot(p,526,160,246,126,(0,150),[(0,'0'),(75,'75'),(150,'150')],[(0,'0'),(.5,'540'),(1,'1080')],[([(0,60),(180/1080,60),(180/1080,90),(480/1080,90),(480/1080,120),(1,120)],BLUE)],'','时间 / s')
p.text('tEnd','结束动作：停止加热',492,344,292,28,font=2)
p.button('bSave','保存方案',492,378,138,36,font=2,bg=WHITE,border=1,code='saved=1\r\ntFoot.txt="方案已暂存；掉电不保存"')
p.button('bApply','应用参数',644,378,140,36,font=2,color=WHITE,bg=BLUE,code='if(run==0)\r\n{\r\ntFoot.txt="参数已暂存；主控未连接"\r\n}')
# Last in paint order so the dropdown covers the programme preview while open.
for i,label in enumerate(['LADRC','PID','模糊LADRC']):
    p.button('bOpt'+str(i),label,450,89+i*36,330,36,font=2,bg=GRAY,border=1,code=f'mode={i}\r\npage 1')


# 2: temperature traces
p=Page(2,'曲线调试温度','温度曲线',2);p.frame();p.tabs(0);p.curve_controls()
points=[i/60 for i in range(61)]
series=[([(x,60+min(60,x*120)) for x in points],BLUE),([(x,56+64*(min(x/.55,1))) for x in points],GREEN),([(x,62+68*(1-math.exp(-3*x))) for x in points],'#ff7c43')]
plot(p,60,140,480,176,(0,150),[(0,'0'),(50,'50'),(100,'100'),(150,'150')],[(0,'0'),(.5,'300'),(1,'600')],series,'温度 / ℃','时间 / s')
for x,s,c in [(300,'目标',BLUE),(382,'平均',GREEN),(464,'最高','#ff7c43')]:p.line(x,108,x+20,108,c);p.text('leg'+s,s,x+24,96,60,24,font=2)
for i,(label,val,name) in enumerate([('目标温度','120.0','tTarget'),('区域平均','119.6','tMean'),('区域最高','124.8','tHigh'),('区域最低','114.2','tLow')]):
    y=102+i*65;p.text('l'+name,label,568,y+11,102,30,font=2);p.text(name,val,674,y+9,86,34,font=2,color=TEAL);p.text('u'+name,'℃',760,y+16,24,24,font=2);p.line(562,y+60,784,y+60)

# 3: power
p=Page(3,'曲线调试功率','功率曲线',2);p.frame();p.tabs(1);p.curve_controls()
plot(p,60,140,460,176,(0,1500),[(0,'0'),(500,'500'),(1000,'1000'),(1500,'1500')],[(0,'0'),(.5,'300'),(1,'600')],[([(x,850+530*math.exp(-((x/.33)**2))) for x in points],TEAL)],'电功率（估算）/ W','时间 / s')
p.line(548,98,548,364)
p.text('lPower','当前功率',570,110,100,28,font=2);p.text('tPower','860',646,146,105,48,font=4,color=TEAL);p.text('uPower','W',754,167,30,25,font=2)
p.text('lMax','功率上限',570,232,100,30,font=2);p.field(PLIM,678,230,84);p.text('uMax','W',766,234,24,28,font=2)
p.text('lAngle','调功移相角',570,280,116,28,font=2);p.text('tAngle','62.0°',692,280,90,28,color=TEAL)
p.button('bApply','应用设置',570,322,214,36,font=2,color=WHITE,bg=BLUE,code='if(run==0)\r\n{\r\ntFoot.txt="设置已暂存；主控未连接"\r\n}')

# 4: frequency
p=Page(4,'曲线调试频率','频率曲线',2);p.frame();p.tabs(2);p.curve_controls()
plot(p,60,140,460,176,(19.8,20.4),[(19.8,'19.8'),(20.1,'20.1'),(20.4,'20.4')],[(0,'0'),(.5,'30'),(1,'60')],[([(x,20.1+.3*math.exp(-8*x)+.005*math.sin(x*160)) for x in points],GREEN)],'工作频率 / kHz','时间 / s')
p.line(548,98,548,364);p.text('lFreq','当前频率',568,108,200,28,font=2);p.text('tFreq','20.10',578,142,150,48,font=4,color=TEAL)
p.text('uFreq','kHz',734,158,52,26,font=3)
p.text('tLocked','已锁定',570,202,214,28,font=2,color=TEAL,align=1)
p.text('lLow','搜频下限',568,244,106,28,font=2);p.field(FLO,680,240,70);p.text('uLow','kHz',754,244,42,28,font=3)
p.text('lHigh','搜频上限',568,288,106,28,font=2);p.field(FHI,680,284,70);p.text('uHigh','kHz',754,288,42,28,font=3)
p.button('bLock','重新搜频',568,330,214,34,font=2,code='if(run==0)\r\n{\r\ntFoot.txt="搜频命令预留；主控未连接"\r\n}')

# 5: protection and alarms
p=Page(5,'保护报警','保护报警',3);p.frame()
p.text('tSettings','保护参数',18,56,300,32);p.text('tAlarmTitle','报警信息',416,56,360,32)
p.line(392,56,392,410)
for i,pr in enumerate(PROTECT):
    y=100+i*43;p.text('l'+pr['var'],pr['label'],18,y,140,32,font=2);p.field(pr,164,y,164,32);p.text('u'+pr['var'],pr['unit'],338,y+2,40,28,font=2)
p.text('tProtectHint','保护参数待配置',18,374,170,30,font=2,color=AMBER)
condition='&&'.join(pr['var']+'>0' for pr in PROTECT)
p.button('bSave','保存参数',212,372,162,38,color=WHITE,bg=BLUE,font=2,code=f'if(run==0)\r\n{{\r\nif({condition})\r\n{{\r\nif(uv<ov)\r\n{{\r\ntProtectHint.txt="已暂存"\r\ntFoot.txt="参数已暂存；尚未下发主控"\r\n}}else\r\n{{\r\ntFoot.txt="欠压阈值必须小于过压阈值"\r\n}}\r\n}}else\r\n{{\r\ntFoot.txt="请先填写全部保护参数"\r\n}}\r\n}}')
p.text('lAlarm','当前报警：',416,100,130,28,font=2);p.text('tAlarm','无（演示）',546,100,224,28,font=2,color=GREEN)
for x,w,label in [(416,88,'运行时间'),(504,182,'报警内容'),(686,98,'状态')]:p.text('th'+str(x),label,x,142,w,32,font=2,bg=GRAY,align=1)
for i,(a,b,c) in enumerate([('12 s','测温数据中断','已复位'),('8 s','锁相失败','已复位')]):
    y=180+i*36
    for j,(x,w,s) in enumerate([(416,88,a),(504,182,b),(686,98,c)]):p.text(f'a{i}{j}',s,x,y,w,28,font=2,align=1)
    p.line(416,y+31,784,y+31)
p.text('tLog','暂无待处理报警\r\n\r\n以上为示例记录',416,266,368,80,font=3,color=MUTED)
p.button('bAck','确认报警',416,372,174,38,font=2,code='ack=1\r\ntFoot.txt="暂无待确认报警（演示）"')
p.button('bReset','故障复位',606,372,178,38,font=2,code='if(run==0)\r\n{\r\ntFoot.txt="复位命令预留；主控未连接"\r\n}')

# 6: the fourth curve tab, complementing the reference set.
p=Page(6,'曲线调试相位','相位差曲线',2);p.frame();p.tabs(3);p.curve_controls()
plot(p,60,140,460,176,(-30,30),[(-30,'-30'),(0,'0'),(30,'30')],[(0,'0'),(.5,'30'),(1,'60')],[([(x,24*math.exp(-7*x)+.7*math.sin(x*38)) for x in points],TEAL)],'相位差 / °','时间 / s')
p.line(548,98,548,364);p.text('lPhase','当前相位差',568,112,216,30,font=2);p.text('tAngle','0.8',582,156,178,48,font=4,color=TEAL);p.text('uAngle','°',756,174,30,32)
p.text('tLocked','已锁定',570,226,214,28,color=TEAL,align=1)
p.text('tInfo','锁相状态与相位反馈\r\n等待主控协议接入',568,284,216,66,font=3,color=MUTED)

# 7: reusable integer numeric keypad with bounds and cancellation.
p=Page(7,'参数输入','参数输入',1)
p.fill(0,0,800,42,GRAY);p.text('tTitle','参数输入',20,5,740,32,bg=GRAY)
p.text('tParam','功率上限 / W',48,64,704,36)
p.text('tValue','1500',48,112,704,65,font=1,color=TEAL,bg='#f3f6f9',align=2,border=1)
p.text('tRange','输入范围：1 至 9999',48,186,704,30,font=3,color=MUTED)
for i,s in enumerate(['7','8','9','4','5','6','1','2','3','0','清空','退格']):
    x=48+(i%3)*180;y=230+(i//3)*55
    if s.isdigit():code=f'if(fresh==1)\r\n{{\r\neditval=0\r\nfresh=0\r\n}}\r\nif(editval<10000)\r\n{{\r\neditval=editval*10+{s}\r\ncovx editval,tValue.txt,0,0\r\n}}'
    elif s=='清空':code='editval=0\r\nfresh=0\r\ncovx editval,tValue.txt,0,0'
    else:code='editval=editval/10\r\nfresh=0\r\ncovx editval,tValue.txt,0,0'
    p.button('k'+str(i),s,x,y,166,47,code=code)
p.button('bCancel','取消',608,230,144,102,code='page backpg')
apply=['if(editval>=editmin&&editval<=editmax)','{']
for pr in PARAMS:apply+=[f'if(field=={pr["id"]})','{',f'{pr["var"]}=editval','}']
apply+=['page backpg','}else','{','tRange.txt="数值超出范围，请重新输入"','tRange.pco='+str(rgb565(RED)),'}']
p.button('bOK','确认',608,340,144,102,color=WHITE,bg=BLUE,code='\r\n'.join(apply))
p.load+=['fresh=1','covx editval,tValue.txt,0,0']
for pr in PARAMS:p.load +=[f'if(field=={pr["id"]})\r\n{{\r\ntParam.txt="{pr["label"]} / {pr["unit"]}"\r\ntRange.txt="输入范围：{pr["low"]} 至 {pr["high"]}"\r\n}}']

def draw_command(op):
    name,*args=op
    return name+' '+','.join(str(rgb565(v)) if isinstance(v,str) and v.startswith('#') else str(v) for v in args)

# Recalculate step programme from all active holding intervals (no ramp-time field).
p=PAGES[1];m=p.plots[0]
dynamic=['sumt=0','ymax=1']
for i in range(1,9):dynamic += [f'if(nseg>={i})','{',f'sumt=sumt+hd{i}',f'if(tg{i}>ymax)','{',f'ymax=tg{i}','}','}']
dynamic += ['ymax=ymax+30',f'covx ymax,{m["yt"][2]}.txt,0,0','tmp=ymax/2',f'covx tmp,{m["yt"][1]}.txt,0,0',f'covx sumt,{m["xt"][2]}.txt,0,0','tmp=sumt/2',f'covx tmp,{m["xt"][1]}.txt,0,0','fill 526,160,247,127,65535']
dynamic += [draw_command(op) for op in p.draw[m['drawstart']:m['gridend']]]
dynamic += ['elapsed=0','px=526','py=286']
for i in range(1,9):
    dynamic += [f'if(nseg>={i})','{',f'ny=tg{i}*126/ymax','ny=286-ny',f'line px,py,px,ny,{rgb565(BLUE)}','py=ny',f'elapsed=elapsed+hd{i}','nx=elapsed*246/sumt+526',f'line px,py,nx,py,{rgb565(BLUE)}','px=nx','}']
p.load+=['\r\n'.join(dynamic),'line 750,65,758,73,31695','line 758,73,766,65,31695']

# Time window controls the actual plotted x coordinates as well as labels.
for pid in [2,3,4,6]:
    p=PAGES[pid];m=p.plots[0];x,y,w,h=[m[k] for k in ['x','y','w','h']];var=WINDOWS[pid]['var'];horizon=600 if pid in (2,3) else 60
    alt=[f'covx {var},{m["xt"][2]}.txt,0,0',f'tmp={var}/2',f'covx tmp,{m["xt"][1]}.txt,0,0',f'tmp={var}%2','if(tmp==1)','{',f'{m["xt"][1]}.txt={m["xt"][1]}.txt+".5"','}',f'fill {x},{y},{w+1},{h+1},65535']
    alt += [draw_command(op) for op in p.draw[m['drawstart']:m['gridend']]]
    for pts,color in m['series']:
        previous=None
        for fx,v in pts:
            t=round(fx*horizon);yy=round(y+h-(v-m['yrange'][0])*h/(m['yrange'][1]-m['yrange'][0]))
            if previous:
                t0,y0=previous
                alt += [f'if({var}>{t0})','{',f'px={t0}*{w}/{var}+{x}',f'py={y0}',f'if({var}>={t})','{',f'nx={t}*{w}/{var}+{x}',f'ny={yy}','}else','{',f'nx={x+w}',f'ny={var}-{t0}',f'ny=ny*{abs(yy-y0)}/{t-t0}',f'ny={y0}'+('+' if yy>=y0 else '-')+'ny','}',f'line px,py,nx,ny,{rgb565(color)}','}']
            previous=(t,yy)
    p.load+=['\r\n'.join(alt)]

# One native one-second timer on every page keeps demonstration stage progression
# alive while changing tabs or using the keypad. Device control remains unconnected.
select=['hold=hd1','tgAct=tg1']
for i in range(1,9):select += [f'if(stage=={i})','{',f'hold=hd{i}',f'tgAct=tg{i}','}']
remaining=['remain=0']
for i in range(1,9):remaining += [f'if(nseg>={i}&&stage<={i})','{',f'remain=remain+hd{i}','}']
remaining+=['remain=remain-stageSec','if(remain<0)','{','remain=0','}']
refresh=select+remaining+['covx stage,tStage.txt,0,0','tStage.txt=tStage.txt+" / "','covx nseg,tPhase.txt,0,0','tStage.txt=tStage.txt+tPhase.txt','covx tgAct,tTarget.txt,0,0','tTarget.txt=tTarget.txt+".0"','covx totalSec,tElapsed.txt,0,0','tElapsed.txt=tElapsed.txt+" s"','covx remain,tRemain.txt,0,0','tRemain.txt=tRemain.txt+" s"','tPhase.txt="待机"','if(run==1)','{','tPhase.txt="保温"','}','if(done==1)','{','tPhase.txt="已完成"','tState.txt="演示结束"','bStart.bco='+str(rgb565(BLUE)),'tsw bStart,1','}']
tick=['if(run==1)','{','totalSec=totalSec+1','stageSec=stageSec+1']+select+['if(stageSec>=hold)','{','if(stage<nseg)','{','stage=stage+1','stageSec=0','}else','{','run=0','done=1','}','}','}']
PAGES[0].load+=['\r\n'.join(refresh)]
for p in PAGES:
    code=tick[:]
    if p.pid==0:code+=refresh
    elif p.pid!=7:code+=['if(done==1)','{','tState.txt="演示结束"','}']
    p.timer='\r\n'.join(code)


# Frequency bounds are validated before leaving the keypad.
key=PAGES[7];ok=next(o for o in key.items if o['name']=='bOK')
ok['code']=ok['code'].replace('if(editval>=editmin&&editval<=editmax)',f'if(field=={FLO["id"]}&&editval>=fhi)\r\n{{\r\ntRange.txt="搜频下限必须小于上限"\r\n}}else\r\n{{\r\nif(field=={FHI["id"]}&&editval<=flo)\r\n{{\r\ntRange.txt="搜频上限必须大于下限"\r\n}}else\r\n{{\r\nif(editval>=editmin&&editval<=editmax)')+'\r\n}\r\n}'

def component(template,changes,codes):
    a=OrderedDict(template)
    for k,v in changes.items():
        if isinstance(v,str):a[k]=v.encode('gbk')
        else:a[k]=v.to_bytes(len(a[k]),'little')
    out=bytearray();pay=f'att-{len(a)}'.encode();out+=P32(len(pay))+pay
    for k,v in a.items():
        pay=k.encode().ljust(16,b'\0')+v;out+=P32(len(pay))+pay
    for event,code in codes:
        lines=code.splitlines() if code else []
        marker=f'{event}-{len(lines)}'.encode();out+=P32(len(marker))+marker
        for line in lines:
            pay=line.encode('gbk');out+=P32(len(pay))+pay
    return bytes(out)+P32(0)

def assemble(page):
    # Draw page decorations after controls have been initialized. Geometry avoids controls.
    load=[draw_command(op) for op in page.draw]+page.load
    base=component(TEMPLATES[0],dict(objname=page.name,id=0,bco=65535),[('codesload',''),('codesloadend','\r\n'.join(load)),('codesdown',''),('codesup',''),('codesunload','')])
    chunks=[base]
    for i,obj in enumerate(page.items,1):
        tpl=TEMPLATES[3] if obj['kind']=='button' else TEMPLATES[1]
        changes=dict(id=i,objname=obj['name'],x=obj['x'],y=obj['y'],w=obj['w'],h=obj['h'],endx=obj['x']+obj['w']-1,endy=obj['y']+obj['h']-1,txt=obj['txt'],txt_maxl=max(64,len(obj['txt'].encode('gbk'))+1),font=obj['font'],bco=rgb565(obj['bg']),pco=rgb565(obj['color']),xcen=obj['align'],ycen=1,style=0,borderw=obj['border'],borderc=rgb565(LINE),isbr=1 if '\n' in obj['txt'] else 0)
        if obj['kind']=='button':changes.update(bco2=rgb565(BLUE),pco2=65535)
        chunks.append(component(tpl,changes,[('codesdown',''),('codesup',obj['code'])]))
    timer=OrderedDict(type=b'3',id=b'\0',objname=b'tmRun',vscope=b'\0',lockobj=b'\0',groupid0=bytes(4),groupid1=bytes(4),tim=(1000).to_bytes(2,'little'),en=b'\1')
    chunks.append(component(timer,dict(id=len(chunks)),[('codestimer',page.timer)]))
    h=bytearray(FILES['0.pa'][:56]);h[24:40]=page.name.encode('gbk').ljust(16,b'\0');struct.pack_into('<I',h,12,len(chunks))
    offset=12*len(chunks);table=bytearray()
    for chunk in chunks:table+=struct.pack('<III',offset,len(chunk),0);offset+=len(chunk)
    blob=h+table+b''.join(chunks);struct.pack_into('<I',blob,4,len(blob));return stamp(blob,'page')

def draw_text(im,text,x,y,w,h,fid,fg,align=0):
    lines=text.replace('\r','').split('\n');height=FONTS[fid][7];yy=y+max(0,(h-height*len(lines))//2)
    for line in lines:
        glyphs=[]
        for ch in line:
            key=ch.encode('gbk').ljust(2,b'\0');glyphs.append(GLYPHS[fid].get(key,GLYPHS[fid].get(b'?\0')))
        total=sum(g.width for g in glyphs);xx=x+(max(0,w-total)//2 if align==1 else max(0,w-total-4) if align==2 else 2)
        for g in glyphs:
            # Clip masks to the component bounds just as the device does.
            cw=min(g.width,x+w-xx);ch=min(g.height,y+h-yy)
            if cw>0 and ch>0:im.paste(rgb888(rgb565(fg)),(xx,yy),g.crop((0,0,cw,ch)))
            xx+=g.width
        yy+=height

HIDDEN_INITIAL={f'{prefix}{i}' for i in range(3,8) for prefix in ['fr','bRow']}|{prefix+pr['var'] for row in STAGES[3:] for pr in row for prefix in ['f','b']}

def preview(page):
    im=Image.new('RGB',(800,480),'white');d=ImageDraw.Draw(im)
    for o in page.items:
        if page.pid==1 and (o['name'].startswith('bOpt') or o['name'] in HIDDEN_INITIAL):continue
        x,y,w,h=[o[k] for k in ('x','y','w','h')];d.rectangle((x,y,x+w-1,y+h-1),fill=rgb888(rgb565(o['bg'])))
        if o['border']:d.rectangle((x,y,x+w-1,y+h-1),outline=rgb888(rgb565(LINE)),width=o['border'])
        text=o['txt']
        if page.pid==1 and o['name'] in ['btg4','brp4','bhd4']:text='—'
        draw_text(im,text,x,y,w,h,o['font'],o['color'],o['align'])
    # Page post-initialization drawing occurs after visible controls, as on the HMI.
    for op,*a in page.draw:
        c=rgb888(rgb565(a[-1]))
        if op=='line':d.line(a[:4],fill=c,width=1)
        else:x,y,w,h=a[:4];d.rectangle((x,y,x+w-1,y+h-1),fill=c)
    return im

def run():
    ROOT.mkdir(exist_ok=True);(ROOT/'预览').mkdir(exist_ok=True)
    for fid,h,ascii_only,bold in [(0,24,False,False),(1,44,True,True),(2,20,False,False),(3,16,False,False),(4,32,True,True)]:
        print('font',fid,h,flush=True);makefont(fid,h,ascii_only,bold)
    built={f'{p.pid}.pa':assemble(p) for p in PAGES}
    # Preserve every original model/rotation/encoding option; update only resource references.
    main=bytearray(FILES['main.HMI'][:96]);refs=[]
    for fid in FONTS:refs.append(('zi',f'{fid}.zi'))
    for p in PAGES:refs.append(('pa',f'{p.pid}.pa'))
    struct.pack_into('<I',main,28,len(refs))
    for ext,name in refs:main+=ext.encode().ljust(8,b'\0')+name.encode().ljust(8,b'\0')
    main=stamp(main,'main')
    program='// 界面演示工程：不向主控发送加热命令。协议确定后接入。\r\n'
    for k,v in GLOBALS.items():program+=f'int {k}={v}\r\n'
    program+='baud=9600\r\ndim=100\r\nrecmod=0\r\npage 0\r\n'
    blocks=[('main.HMI',main),('Program.s',program.encode('gbk'))]+[(f'{i}.zi',f) for i,f in FONTS.items()]+list(built.items())
    # Compact primary and mirrored directory. Reserved prefix and format magic are retained.
    head=bytearray(DATA[:0x700000]);directory=bytearray(P32(len(blocks)));body=bytearray()
    for name,b in blocks:
        directory+=name.encode().ljust(16,b'\0')+struct.pack('<III',0x700000+len(body),len(b),0);body+=b
    # CFSOpenSystem rejects the whole project if either directory omits this checksum.
    directory+=P32(directory_crc(directory))
    for base in [0,0x80000]:
        head[base:base+4096]=bytes(4096);head[base:base+len(directory)]=directory
    OUTPUT.write_bytes(head+body)
    for p in PAGES:preview(p).save(ROOT/'预览'/f'{p.pid:02d}_{p.name}.png')
    sheet=Image.new('RGB',(1600,480*3),'#d6dbe1')
    for i,p in enumerate(PAGES[:6]):sheet.paste(preview(p),((i%2)*800,(i//2)*480))
    sheet.save(ROOT/'界面总览.png')
    spec=[dict(id=p.pid,name=p.name,title=p.title,items=p.items,draw=p.draw,load=p.load,timer=p.timer) for p in PAGES]
    (ROOT/'界面定义.json').write_text(json.dumps(dict(pages=spec,params=PARAMS,globals=GLOBALS),ensure_ascii=False,indent=2),'utf8')
    (ROOT/'Program.s').write_bytes(program.encode('gbk'))
    for p in PAGES:
        lines=['# '+p.name,'\n## 1 秒定时器\n',p.timer,'\n## 页面后初始化\n','\n'.join(p.load)]
        for o in p.items:
            if o['code']:lines+=['\n## '+o['name']+' '+o['txt']+' 触摸弹起\n',o['code']]
        (ROOT/'预览'/f'{p.pid:02d}_事件.txt').write_text('\n'.join(lines),'utf8')
    gallery='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>感应加热 HMI 页面预览</title><style>body{margin:24px;background:#e9edf2;color:#29313e;font:16px Microsoft YaHei}nav{display:flex;gap:8px;flex-wrap:wrap;margin:16px 0}button{padding:10px 18px;border:0;background:white;cursor:pointer}button.active{background:#137de1;color:white}img{display:block;width:min(100%,1000px);box-shadow:0 10px 35px #0002}p{color:#6d7785}</style><h2>感应加热 · HMI 界面预览</h2><p>800×480 · 原生工程页面的离线渲染预览 · 数据为演示值</p><nav>'
    for p in PAGES:gallery+=f'<button onclick="show({p.pid},this)">{html.escape(p.title)}</button>'
    paths=[f'预览/{p.pid:02d}_{p.name}.png' for p in PAGES]
    gallery+='</nav><img id="screen"><p>本页仅预览外观。按钮事件、参数键盘保存在 .HMI 工程中，需编辑器编译验证。</p><script>const files='+json.dumps(paths,ensure_ascii=False)+';function show(i,b){document.getElementById("screen").src=files[i];document.querySelectorAll("button").forEach(x=>x.classList.remove("active"));b.classList.add("active")}show(0,document.querySelector("button"));</script></html>'
    (ROOT/'预览.html').write_text(gallery,'utf8')
    print('OUTPUT',OUTPUT,'bytes',OUTPUT.stat().st_size,flush=True)

if __name__=='__main__':run()
