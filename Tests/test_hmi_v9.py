"""Read the emitted HMI container and execute its actual load/timer code.

This uses the existing offline interpreter, extended for bitwise AND; it is
not an official TJC compilation or a hardware test.
"""
import re
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / '感应加热HMI' / '界面制作'))
import hmi_verify as v
from feedback_runtime import MESSAGES, validate_ids

class Runtime(v.Runtime):
    def value(self, expr):
        if '&' in expr:
            a, b = expr.split('&')
            return super().value(a) & super().value(b)
        return super().value(expr)

target = Path(sys.argv[1]) if len(sys.argv)>1 else root / '感应加热HMI' / '加热1_调试版_V9.HMI'
_, files, spans = v.unpack(target)
assert all(a + n == b for (a, n), (b, _) in zip(spans, spans[1:]))
v.checksum(files['main.HMI'], True)
pages = {int(n[:-3]): v.page(b) for n, b in files.items() if n.endswith('.pa')}
for pid, objects in pages.items():
    names={o['attr']['objname'] for o in objects}
    for obj in objects:
        for code in obj['events'].values():
            for line in code:
                match=re.match(r'^(?:click|vis|tsw|ref) ([A-Za-z_]\w*)',line)
                if match: assert match[1] in names, (pid, obj['attr']['objname'],line)
g = {k: int(n) for k, n in re.findall(r'^int (\w+)=(-?\d+)\r?$', files['Program.s'].decode('gbk'), re.M)}
rt = Runtime(pages, g)
# Exercise emitted keypad events and compare their limits with the MCU.
limit_config=(root/'Core/Inc/ih_config.h').read_text('utf8')
keyrt=Runtime(pages,g)
for var,lo,define in (
    ('ov',2,'IH_LIMIT_OV_MAX_V'),('uv',1,'IH_LIMIT_UV_MAX_V'),
    ('oc',1,'IH_LIMIT_OC_PEAK_MAX_A'),('ot1',1,'IH_LIMIT_OT_MAX_C'),
    ('ot2',1,'IH_LIMIT_OT_MAX_C'),('ot3',1,'IH_LIMIT_OT_MAX_C')):
    hi=int(re.search(r'^#define\s+'+define+r'\s+(\d+)\s*$',limit_config,re.M)[1])
    keyrt.enter(5); keyrt.click('b'+var)
    assert keyrt.pid==7 and keyrt.g['editmin']==lo and keyrt.g['editmax']==hi
    keyrt.g['editval']=hi+1; keyrt.click('bOK'); assert keyrt.pid==7
    keyrt.g['editval']=lo-1; keyrt.click('bOK'); assert keyrt.pid==7
    keyrt.g['editval']=hi; keyrt.click('bOK')
    assert keyrt.pid==5 and keyrt.g[var]==hi
assert keyrt.get('loc.txt')=='谐振峰值过流'

overview={o['attr']['objname']:o['attr'] for o in pages[0]}
assert overview['tPower']['font']==overview['tActual']['font']==overview['tTarget']['font']==overview['tBus']['font']
# Fixed unit columns and solid backgrounds prevent alignment shifts/old glyphs.
assert overview['utPower']['x']==overview['utCurrent']['x']
assert overview['utPower']['w']==overview['utCurrent']['w']
assert overview['tPower']['sta']==1
number_widths={}
for fid in (1,4):
    font_data=files[f'{fid}.zi']; widths={}
    for i in range(struct.unpack_from('<I',font_data,12)[0]):
        row=44+font_data[17]+i*10
        widths[font_data[row:row+2]]=font_data[row+2]
    number_widths[fid]=widths
assert overview['tPower']['x']+overview['tPower']['w']<=overview['utPower']['x']
assert overview['utPower']['x']+overview['utPower']['w']<=800
validate_ids()
font = files['3.zi']
glyphs = {}
for i in range(struct.unpack_from('<I', font, 12)[0]):
    row = 44 + font[17] + i * 10
    glyphs[font[row:row+2]] = font[row+2]
for pid in range(7):
    rt.enter(pid)
    for code, message in enumerate(MESSAGES, 1):
        rt.g.update(msgCode=code, msgA=170, msgB=18, msgPush=1)
        rt.execute(['click bInput,0'])
        text = rt.get('tFoot.txt')
        if message.get('format') in ('uv', 'uv_history', 'oc'):
            unit='A' if message.get('format')=='oc' else 'V'
            assert text == message['text'] + f'：17.0{unit}，门限18{unit}，请复位'
        else:
            assert text == message['text']
        width = sum(glyphs[c.encode('gbk').ljust(2, b'\0')] for c in text)
        assert width <= rt.objs['tFoot']['attr']['w'] - 4, (pid, text, width)
        assert rt.g['msgPush'] == 0
    rt.g['dirty'] = 1
    rt.tick(2)
    assert rt.get('tFoot.txt') == text
    for code in (10, 32):
        for bus10, threshold in ((6, 1), (0, 1), (70, 1), (1, 1)):
            rt.g.update(msgCode=code, msgA=bus10, msgB=threshold, msgPush=1)
            rt.execute(['click bInput,0'])
            expected = f'{MESSAGES[code-1]["text"]}：{bus10//10}.{bus10%10}V，门限{threshold}V，请复位'
            assert rt.get('tFoot.txt') == expected
rt.enter(5)
rt.g.update(internalFault=1024, dirty=1)
rt.tick()
assert rt.get('tAlarm.txt') == '采样或通信异常'
rt.g.update(internalFault=0, dirty=1)
rt.tick()
assert rt.get('tAlarm.txt') == '无'
rt.g.update(internalFault=4,dirty=1); rt.tick()
assert rt.get('tAlarm.txt')=='搜频或锁相异常'
rt.g.update(internalFault=0,dirty=1); rt.tick()
for pid in (0,5):
    rt.g.update(fwVersion=110201,tripCount=0,dirty=1)
    rt.enter(pid)
    assert rt.get('tDiag.txt') == '固件号：110201'
    for gate, elapsed, raw1, raw2 in ((0,1,82,79),(1,60,82,79),(1,99999,4095,4095)):
        rt.g.update(tripCount=1,tripGate=gate,tripMs=elapsed,tripRaw1=raw1,tripRaw2=raw2,dirty=1)
        rt.tick()
        line=f'驱动{"后" if gate else "前"} {elapsed}ms 原码{raw1}/{raw2}'
        assert rt.get('tDiag.txt') == line
        assert sum(glyphs[c.encode('gbk').ljust(2,b'\0')] for c in line)<=rt.objs['tDiag']['attr']['w']-4
        rt.g.update(bus10=70,dirty=1)
        rt.tick(2)
        assert rt.get('tDiag.txt') == line
rt.enter(0)
def submit(sec, flags, **values):
    rt.g.update(sampleSec=sec, dataFlags=flags, dirty=1, push=1, **values)
    rt.execute(['click bInput,0'])
def text(name):
    return rt.get(name + '.txt')

assert text('tActual') == text('tTarget') == text('tPower') == '--'
submit(1, 8 | 16 | 32, bus10=240, amp10=12, freq100=2010)
assert text('tBus') == '24.0' and text('tFreq') == '20.10'
for _ in range(10):
    rt.g['dirty'] = 1
    rt.tick()
    assert text('tActual') == text('tTarget') == text('tPower') == '--'
# Per-channel validity survives page load, timer refresh, and alarm ingestion.
for pid in (2, 3, 0):
    rt.enter(pid)
    if pid == 2:
        assert text('tMean') == text('tHigh') == text('tLow') == text('tTarget') == '--'
    elif pid == 3:
        assert text('tPower') == text('tAngle') == '--'
rt.g.update(alarmIn=4, alarmOn=1, alarmPush=1)
rt.execute(['click bInput,0'])
assert text('tActual') == text('tTarget') == text('tPower') == '--'
# A connected/calibrated sensor enables only that field; data loss hides it.
submit(2, 1 | 8 | 16 | 32, temp10=253)
assert text('tActual') == '25.3' and text('tPower') == '--'
submit(3, 32)
assert text('tActual') == text('tBus') == text('tCurrent') == '--'
assert text('tFreq') == '20.10'
# Source separation and restored screenshot layouts.
rt.enter(2)
assert text('tMean') == text('tHigh') == text('tLow') == '--'
assert all(name not in rt.objs for name in ('tAdc1','tAdc2','tSensor'))
assert text('ltMean')=='区域平均' and text('ltHigh')=='区域最高' and text('ltLow')=='区域最低'
assert not any('器件' in o['attr'].get('txt','') for o in pages[2])
submit(4,2|1|512|2048,target10=800,temp10=253,temp110=310,temp210=410)
assert text('tMean')==text('tHigh')==text('tLow')=='--'
submit(5,2|1|128,target10=800,temp10=800,mean10=800,high10=900,low10=700)
assert text('tMean')=='80.0' and text('tHigh')=='90.0' and text('tLow')=='70.0'
rt.enter(0); assert text('tActual')=='80.0' and text('ltActual')=='实际温度'
# Fractional and negative active power must not be rounded to whole watts.
for n, expected in ((0,'0.00'),(1,'0.01'),(-1,'-0.01'),(-3,'-0.03'),(24,'0.24'),(-24,'-0.24'),
                    (-30,'-0.30'),(-1240,'-12.40'),(150000,'1500.00'),(-150000,'-1500.00'),(3,'0.03')):
    before=len(rt.trace)
    submit(6,4,power100=n)
    assert text('tPower')==expected
    assert sum(cmd.startswith('tPower.txt=') for _,cmd,_ in rt.trace[before:])==1
    assert not text('tPower').endswith('-')
    actual_font=rt.objs['tPower']['attr']['font']
    assert actual_font==(1 if len(expected)<=6 else 4)
    assert sum(number_widths[actual_font][c.encode().ljust(2,b'\0')] for c in expected)<=overview['tPower']['w']-4
    assert rt.objs['utPower']['attr']['x']==overview['utCurrent']['x']
    rt.enter(3); assert text('tPower')==expected
    rt.enter(0)
submit(7,0,power100=0); assert text('tPower')=='--'
assert rt.objs['tPower']['attr']['font']==1
# Self-prepending sign or zero padding must never reappear in emitted events.
for objects in pages.values():
    for obj in objects:
        for code in obj['events'].values():
            for line in code:
                match=re.match(r'^(\w+\.txt)="(?:-|0)"\+(.+)$',line)
                if match:assert match[1] not in match[2], line
# The timer and field snapshots must never erase existing strokes.
rt.g.update(lastSec=150,frozen=0,follow=1,pfrom=0,pto=600,runId=1,seenRun=1)
rt.enter(3)
rt.g.update(curveEpoch=2)
rt.drawing.clear()
packet_file=root/'tmp/test_hmi_curve_packets.txt'
assert packet_file.exists(), 'run Tests/run.ps1 to capture real MCU replay packets'
rt.execute(packet_file.read_text().splitlines())
strokes=[a for k,a in rt.drawing if k=='line' and a[-1]==((0x00>>3)<<11 | (0x5c>>2)<<5 | (0x65>>3))]  # replaced below
assert len(strokes)>10
assert any(a[0]==60 and a[1]==316 for a in strokes), 'early data must survive >32 seconds and navigation'
assert max(a[2] for a in strokes)>170, 'replay must also reach the newest points'
assert len([1 for k,_ in rt.drawing if k=='fill'])==1
before=len(rt.drawing)
for _ in range(10):
    rt.g['dirty']=1; rt.tick()
assert len(rt.drawing)==before, 'ordinary refresh must not clear or repaint the curve'
# Queued packets from an old page/epoch cannot draw into the current view.
rt.g.update(plotEpoch=1,plotReset=1); rt.execute(['click bCurve,0'])
assert len(rt.drawing)==before
rt.g.update(plotEpoch=2,plotPage=4); rt.execute(['click bCurve,0'])
assert len(rt.drawing)==before

def metadata():
    raw=bytes(rt.tx); result={}
    for field in (42,43,44,46,47,48,45):
        prefix=bytes([0xA5,0x5A,2,rt.pid,field]); pos=raw.rfind(prefix)
        assert pos>=0 and raw[pos+9:pos+12]==b'\xff\xff\xff'
        result[field]=int.from_bytes(raw[pos+5:pos+9],'little',signed=True)
    return result

rt.tx.clear(); rt.click('bFreeze'); meta=metadata()
assert rt.g['frozen']==1 and meta[44]==150
rt.g.update(lastSec=1001,dirty=1); rt.tick()
assert rt.g['pfrom']==0 and rt.g['pto']==600
rt.enter(4); assert metadata()[44]==150, 'frozen cutoff must survive navigation'
rt.tx.clear(); rt.click('bLive'); meta=metadata()
assert meta[42]==0 and meta[43]==1020 and meta[44]==-1
assert rt.g['ffrom']==0, 'live following expands the right edge, never discards the beginning'
rt.g.update(lastSec=1100,dirty=1); rt.tick()
assert rt.g['ffrom']==0 and rt.g['fto']==1140
# Time input keeps a manually chosen range, with validation before acceptance.
rt.enter(7)
rt.g.update(field=30,backpg=4,editval=900,editmin=0,editmax=86399)
rt.click('bOK'); assert rt.g['follow']==0 and rt.g['ffrom']==900
rt.g.update(lastSec=1200,dirty=1); rt.tick(); assert rt.g['ffrom']==900 and rt.g['fto']==1140
rt.click('bLive'); assert rt.g['ffrom']==0 and rt.g['fto']==1260
# Three temperature traces come only from region statistics; invalid gaps break
# a line instead of drawing a false zero or bridging missing data.
rt.g.update(lastSec=3,tfrom=0,tto=600,frozen=0,follow=0)
rt.enter(2); rt.drawing.clear()
rt.g.update(plotPage=2,plotEpoch=rt.g['curveEpoch'],plotRun=1,plotYLo=0,plotYHi=1500,plotReset=1,plotStart=0,plotCount=4)
ys=[(70,50,80),(70,255,255),(70,60,90),(70,70,100)]
for i,(a,b,c) in enumerate(ys): rt.g[f'cv{i}']=((0 if i==0 else 1)<<24)|a|(b<<8)|(c<<16)
rt.execute(['click bCurve,0'])
mean_color=((0x00>>3)<<11)|((0xa1>>2)<<5)|(0x91>>3)
mean_lines=[a for k,a in rt.drawing if k=='line' and a[-1]==mean_color]
assert len(mean_lines)==3
assert mean_lines[1][0:2]==mean_lines[1][2:4], 'first valid point after a gap must not connect backwards'
assert mean_lines[2][1]==256 and mean_lines[2][3]==246
assert not any(a[-1]==0x8b5cf6 for k,a in rt.drawing if k=='line')
rt.g.update(dirty=1); before=len(rt.drawing); rt.tick(); assert len(rt.drawing)==before
# Axes use the active stage targets, configured power ceiling and search bounds.
rt.g.update(nseg=2,tg1=60,tg2=170,tg3=900)
rt.enter(2); meta=metadata()
assert (meta[46],meta[47])==(0,2000), meta
def y_labels():
    labels=[o['attr'] for o in rt.objs.values() if re.fullmatch(r'ty\d+',o['attr']['objname'])]
    return [o['txt'] for o in sorted(labels,key=lambda a:a['y'],reverse=True)]
assert y_labels()==['0','66.6','133.3','200']
rt.g.update(plim=300); rt.enter(3); meta=metadata()
assert (meta[46],meta[47])==(0,300)
assert y_labels()==['0','100','200','300']
rt.g.update(flo=17,fhi=23); rt.enter(4); meta=metadata()
assert (meta[46],meta[47])==(1700,2300)
assert y_labels()==['17','20','23']
# A new successful run resets all windows/freeze state, and old-run packets
# cannot paint into the new record even if an epoch number is reused.
rt.g.update(runId=2,lastSec=0,frozen=1,freezeSec=1000,ffrom=900,fto=1200,dirty=1)
rt.execute(['click bInput,0'])
assert rt.g['seenRun']==2 and rt.g['ffrom']==0 and rt.g['fto']==60 and rt.g['frozen']==0
before=len(rt.drawing)
rt.g.update(plotPage=4,plotEpoch=rt.g['curveEpoch'],plotRun=1,plotReset=1)
rt.execute(['click bCurve,0']); assert len(rt.drawing)==before
# Original business request IDs remain intact.
for pid in (0,4,6):
    rt.enter(pid)
    dest='tLock' if pid==0 else 'tLocked'
    for state,label in ((1,'启动中'),(2,'搜频中'),(3,'锁相捕获'),(4,'已锁定')):
        rt.g.update(run=1,trackState=state,locked=int(state==4),dirty=1)
        rt.execute(['click bInput,0']); assert text(dest)==label
rt.g.update(run=0,trackState=0,locked=0)
rt.g.update(run=0); rt.enter(6)
submit(1,0,phase10=0)
assert text('tAngle')=='--' and text('tInfo')=='启动后显示相位'
rt.g.update(run=1)
for phase, expected in ((0,'0.0'),(123,'12.3'),(-123,'-12.3'),(1700,'170.0'),(-1700,'-170.0')):
    submit(2,64,phase10=phase)
    assert text('tAngle')==expected
    assert text('tLocked')=='未锁定'  # measurement does not claim closed-loop lock
    assert text('tInfo')==('超出±30°曲线范围' if abs(phase)>300 else '电压超前电流为正')
submit(3,0,phase10=123)
assert text('tAngle')=='--' and text('tInfo')=='等待有效相位'
# Phase replay preserves gaps instead of connecting across invalid samples.
rt.g.update(plotPage=6,plotEpoch=rt.g['curveEpoch'],plotRun=rt.g['runId'],
            plotYLo=-300,plotYHi=300,plotReset=1,plotStart=0,plotCount=4)
for i,h in enumerate((100,255,70,80)):
    rt.g[f'cv{i}']=((0 if i==0 else 1)<<24)|h|(255<<8)|(255<<16)
rt.drawing.clear(); rt.execute(['click bCurve,0'])
phase_lines=[a for k,a in rt.drawing if k=='line' and a[-1]==((0x00>>3)<<11 | (0x5c>>2)<<5 | (0x65>>3))]
assert len(phase_lines)==3 and phase_lines[1][0:2]==phase_lines[1][2:4]
rt.g.update(run=0); rt.enter(0); rt.tx.clear(); rt.click('bStart')
assert bytes([0xA5,0x5A,1,0,1,255,255,255]) in rt.tx
rt.enter(1); rt.tx.clear(); rt.click('bAdd')
assert bytes([0xA5,0x5A,2,1,0]) in rt.tx
rt.click('bMode'); rt.tx.clear(); rt.click('bOpt1')
assert bytes([0xA5,0x5A,2,1,0,1,0,0,0,255,255,255]) in rt.tx
assert (g['hd1'],g['hd2'],g['hd3'])==(600,1800,3600)
assert (g['tto'],g['pto'],g['fto'],g['ato'])==(600,600,60,60)
assert len(g)<256, 'history must live in MCU RAM, not thousands of HMI globals'
print('PASS: Chinese layouts, 0.01 W display, source validity, actual MCU replay, gaps, epoch rejection, freeze and expanding/manual time windows')
