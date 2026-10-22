from pathlib import Path
p=Path(__file__).with_name('build_hmi.py')
s=p.read_text('utf8')
def replace_block(start,end,new):
    global s
    a=s.index(start);b=s.index(end,a);s=s[:a]+new+'\n\n'+s[b:]

s=s.replace("OUTPUT=ROOT.parent/'加热1_界面版.HMI'", "OUTPUT=ROOT.parent/'加热1_界面版_V2.HMI'")
s=s.replace('sys0','tmp')
s=s.replace('win=0,','stagepage=0,sel=1,ddopen=0,stage=0,stageSec=0,totalSec=0,remain=0,hold=0,tgAct=0,done=0,')
replace_block('STAGES=[]','FLO=param', '''STAGES=[]
for i in range(8):
    t=[60,90,120,120,120,120,120,120][i];h=[180,300,600,300,300,300,300,300][i]
    STAGES.append([param(f'tg{i+1}',f'温段{i+1}目标温度',t,'℃',1,999),param(f'hd{i+1}',f'温段{i+1}保温时间',h,'s',1,9999)])
WINDOWS={}
for pid,var,value in [(2,'wtemp',600),(3,'wpower',600),(4,'wfreq',60),(6,'wphase',60)]:
    WINDOWS[pid]=param(var,'曲线时间窗',value,'s',1,86400)
    WINDOWS[pid]['view_only']=True''')
s=s.replace("self.nav=nav;self.plots=[]", "self.nav=nav;self.plots=[];self.timer=''")
s=s.replace("'3 / 3','tStage'", "'0 / 3','tStage'").replace("'当前阶段','保温'","'当前阶段','待机'")
s=s.replace("'tElapsed','18:36'","'tElapsed','0 s'").replace("'tRemain','08:24'","'tRemain','1080 s'")
s=s.replace("code='run=1\\r\\npage 0'", "code='run=1\\r\\nstage=1\\r\\nstageSec=0\\r\\ntotalSec=0\\r\\ndone=0\\r\\npage 0'")
replace_block('# 1: temperature programme','# 2: temperature traces',r'''# 1: boxed programme table. Eight slots, four rows per screen.
p=Page(1,'分段温控','分段温控',1);p.frame()
p.text('lPower','功率上限',16,56,92,30,font=2);p.field(PLIM,112,54,106);p.text('uPower','W',226,56,28,30,font=2)
p.text('lMode','控制算法',336,56,104,30,font=2)
p.text('fMode','',449,53,332,36,bg=LINE)
p.button('bMode','LADRC  ▼',450,54,330,34,font=2,bg=WHITE,code='if(run==0)\r\n{\r\nddopen=1-ddopen\r\nif(ddopen==1)\r\n{\r\nvis bOpt0,1\r\nvis bOpt1,1\r\nvis bOpt2,1\r\n}else\r\n{\r\npage 1\r\n}\r\n}')
p.load+=['ddopen=0','vis bOpt0,0','vis bOpt1,0','vis bOpt2,0']
for i,label in enumerate(['LADRC','PID','模糊LADRC']):
    p.load +=[f'if(mode=={i})\r\n{{\r\nbMode.txt="{label}  ▼"\r\n}}']
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
    p.load += [f'if(sel=={i+1})','{',f'bRow{i}.bco={rgb565(BLUE)}',f'bRow{i}.pco=65535','}','}else','{']
    for name in names:p.load += [f'vis {name},0']
    p.load+=['}']
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
p.button('bPrev','上一页',276,324,84,34,font=3,code='stagepage=0\r\npage 1')
p.button('bNext','下一页',368,324,88,34,font=3,code='if(nseg>4)\r\n{\r\nstagepage=1\r\npage 1\r\n}')
p.text('tTip','点温段编号选中；插入到选中行之后',16,370,442,24,font=3,color=MUTED)
p.text('tCount','共 3 段 · 第 1 / 1 页',16,397,442,20,font=3,color=MUTED)
p.load+=['covx nseg,tCount.txt,0,0','tCount.txt="共 "+tCount.txt+" 段 · 第 "','tmp=stagepage+1','covx tmp,tTip.txt,0,0','tCount.txt=tCount.txt+tTip.txt','if(nseg>4)','{','tCount.txt=tCount.txt+" / 2 页"','}else','{','tCount.txt=tCount.txt+" / 1 页"','}','tTip.txt="点编号选中；可编辑 1 至 8 个温段"']
p.text('tPreview','程序预览',496,100,280,26,font=2)
plot(p,526,160,246,126,(0,150),[(0,'0'),(75,'75'),(150,'150')],[(0,'0'),(.5,'540'),(1,'1080')],[([(0,60),(180/1080,60),(180/1080,90),(480/1080,90),(480/1080,120),(1,120)],BLUE)],'','时间 / s')
p.text('tEnd','结束动作：停止加热',492,344,292,28,font=2)
p.button('bSave','保存方案',492,378,138,36,font=2,bg=WHITE,border=1,code='saved=1\r\ntFoot.txt="方案已暂存；掉电不保存"')
p.button('bApply','应用参数',644,378,140,36,font=2,color=WHITE,bg=BLUE,code='if(run==0)\r\n{\r\ntFoot.txt="参数已暂存；主控未连接"\r\n}')
# Last in paint order so the dropdown covers the programme preview while open.
for i,label in enumerate(['LADRC','PID','模糊LADRC']):
    p.button('bOpt'+str(i),label,450,89+i*36,330,35,font=2,bg=GRAY,code=f'mode={i}\r\npage 1')
''')
# Old fixed-window toggles are removed; all four windows use the numeric keypad.
import re
s=re.sub(r"p.load\+=\['if\(win==1\).*?\]\n",'',s)
s=re.sub(r"for it in p.items:\n    if it\['name'\]=='bWindow':.*?\n",'',s)
s=s.replace("[(0,'0'),(.5,'5'),(1,'10')]","[(0,'0'),(.5,'300'),(1,'600')]")
s=s.replace("'时间 / min'","'时间 / s'")
s=s.replace("'20.10',578,142,202,56,font=1","'20.10',578,142,150,48,font=4")
s=s.replace("p.text('tLocked','已锁定 · kHz',570,202,214,28,font=2,color=TEAL,align=1)","p.text('uFreq','kHz',734,158,52,26,font=3)\np.text('tLocked','已锁定',570,202,214,28,font=2,color=TEAL,align=1)")
s=s.replace("'860',646,146,105,54,font=1","'860',646,146,105,48,font=4")
s=s.replace("'0.8',582,156,178,56,font=1","'0.8',582,156,178,48,font=4")
s=s.replace("'uLow','kHz',754,244,42,28,font=2","'uLow','kHz',754,244,42,28,font=3").replace("'uHigh','kHz',754,288,42,28,font=2","'uHigh','kHz',754,288,42,28,font=3")
s=s.replace("674,y+9,86,34,color=TEAL","674,y+9,86,34,font=2,color=TEAL")
s=s.replace('if(editval<1000)','if(editval<10000)')
replace_block('# Recalculate programme preview','# Frequency bounds',r'''# Recalculate step programme from all active holding intervals (no ramp-time field).
p=PAGES[1];m=p.plots[0]
dynamic=['sumt=0','ymax=1']
for i in range(1,9):dynamic += [f'if(nseg>={i})','{',f'sumt=sumt+hd{i}',f'if(tg{i}>ymax)','{',f'ymax=tg{i}','}','}']
dynamic += ['ymax=ymax+30',f'covx ymax,{m["yt"][2]}.txt,0,0','tmp=ymax/2',f'covx tmp,{m["yt"][1]}.txt,0,0',f'covx sumt,{m["xt"][2]}.txt,0,0','tmp=sumt/2',f'covx tmp,{m["xt"][1]}.txt,0,0','fill 526,160,247,127,65535']
dynamic += [draw_command(op) for op in p.draw[m['drawstart']:m['gridend']]]
dynamic += ['elapsed=0','px=526','py=286']
for i in range(1,9):
    dynamic += [f'if(nseg>={i})','{',f'ny=tg{i}*126/ymax','ny=286-ny',f'line px,py,px,ny,{rgb565(BLUE)}','py=ny',f'elapsed=elapsed+hd{i}','nx=elapsed*246/sumt+526',f'line px,py,nx,py,{rgb565(BLUE)}','px=nx','}']
p.load+=['\r\n'.join(dynamic)]

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
''')
# Timer record uses the verified native type-51 attribute widths from reference .HMI.
s=s.replace("    h=bytearray(FILES['0.pa'][:56]);", "    timer=OrderedDict(type=b'3',id=b'\\0',objname=b'tmRun',vscope=b'\\0',lockobj=b'\\0',groupid0=bytes(4),groupid1=bytes(4),tim=(1000).to_bytes(2,'little'),en=b'\\1')\n    chunks.append(component(timer,dict(id=len(chunks)),[('codestimer',page.timer)]))\n    h=bytearray(FILES['0.pa'][:56]);")
s=s.replace("(2,20,False,False)]", "(2,20,False,False),(3,16,False,False),(4,32,True,True)]")
s=s.replace("for o in page.items:\n        x,y,w,h", "for o in page.items:\n        if page.pid==1 and (o['name'].startswith('bOpt') or o['name'] in HIDDEN_INITIAL):continue\n        x,y,w,h")
s=s.replace("# Page post-initialization drawing occurs", "# Page post-initialization drawing occurs")
s=s.replace("def preview(page):", "HIDDEN_INITIAL={f'{prefix}{i}' for i in range(3,8) for prefix in ['fr','bRow']}|{prefix+pr['var'] for row in STAGES[3:] for pr in row for prefix in ['f','b']}\n\ndef preview(page):")
s=s.replace("items=p.items,draw=p.draw,load=p.load", "items=p.items,draw=p.draw,load=p.load,timer=p.timer")
s=s.replace("lines=['# '+p.name,", "lines=['# '+p.name,'\\n## 1 秒定时器\\n',p.timer,")
p.write_text(s,'utf8')
print('Updated',p)
