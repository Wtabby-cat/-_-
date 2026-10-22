"""Render initial/event states from the emitted HMI and its embedded fonts.
Offline visualization only; this is not the vendor's simulator.
"""
import re,json
from PIL import Image,ImageDraw
import build_hmi as build
import verify_generated as verify

raw,files,_=verify.unpack(verify.TARGET)
pages={int(name[:-3]):verify.page(blob) for name,blob in files.items() if name.endswith('.pa')}
variables={k:int(v) for k,v in re.findall(r'^int (\w+)=(-?\d+)\r?$',files['Program.s'].decode('gbk'),re.M)}
for fid,height,ascii_only,bold in [(0,24,False,False),(1,44,True,True),(2,20,False,False),(3,16,False,False),(4,32,True,True)]:build.makefont(fid,height,ascii_only,bold)
rt=verify.Runtime(pages,variables)
fits=[]
def render():
    im=Image.new('RGB',(800,480),'white');d=ImageDraw.Draw(im)
    def paint(name,obj):
        a=obj['attr']
        if 'txt' not in a or not rt.visible[name]:return
        x,y,w,h=[a[k] for k in ('x','y','w','h')]
        d.rectangle((x,y,x+w-1,y+h-1),fill=build.rgb888(a['bco']))
        if a.get('borderw'):d.rectangle((x,y,x+w-1,y+h-1),outline=build.rgb888(a['borderc']),width=a['borderw'])
        rgb=build.rgb888(a['pco']);fg='#%02x%02x%02x'%rgb
        build.draw_text(im,a['txt'],x,y,w,h,a['font'],fg,a['xcen'])
        for line in a['txt'].replace('\r','').split('\n'):
            widths=[build.GLYPHS[a['font']].get(ch.encode('gbk').ljust(2,b'\0')) for ch in line]
            assert all(g is not None for g in widths),(rt.pid,name,line,'missing glyph')
            width=sum(g.width for g in widths)
            if width+2>w:fits.append((rt.pid,name,line,width,w))
    for name,obj in rt.objs.items():
        if not name.startswith('bOpt'):paint(name,obj)
    for kind,a in rt.drawing:
        if kind=='line':d.line(a[:4],fill=build.rgb888(a[-1]),width=1)
        else:x,y,w,h,c=a;d.rectangle((x,y,x+w-1,y+h-1),fill=build.rgb888(c))
    for name,obj in rt.objs.items():
        if name.startswith('bOpt'):paint(name,obj)
    return im

sheet=Image.new('RGB',(1600,1440),'white')
for p in build.PAGES:
    if p.pid==7:
        rt.enter(1);rt.click('bplim')
    else:rt.enter(p.pid)
    im=render();im.save(build.ROOT/'预览'/f'{p.pid:02d}_{p.name}.png')
    if p.pid<6:sheet.paste(im,((p.pid%2)*800,(p.pid//2)*480))
sheet.save(build.ROOT/'界面总览.png')
rt.enter(1);rt.click('bMode');render().save(build.ROOT/'预览'/'下拉菜单展开.png')
rt.click('bOpt2')
for _ in range(9):rt.click('bAdd')
rt.click('bPrev');render().save(build.ROOT/'预览'/'一页六个温段.png')
rt.click('bNext');render().save(build.ROOT/'预览'/'第2页温段.png')
rt=verify.Runtime(pages,variables)
rt.g.update(run=1,stage=2,totalSec=123,remain=456,phaseState=2,locked=1,valid=1,temp10=1196,target10=1200,mean10=1196,high10=1248,low10=1142,powerW=860,freq100=2010,bus10=3080,amp10=182,dirty=1)
rt.tick();render().save(build.ROOT/'预览'/'指令输入后总览.png')
for pid,prefix in [(2,'t'),(3,'p'),(4,'f'),(6,'a')]:
    rt.g[prefix+'from']=86399;rt.g[prefix+'to']=86400;rt.enter(pid);render()
rt.enter(5);rt.g.update(alarmIn=3,alarmOn=1,alarmSec=12,alarmPush=1);rt.tick()
render().save(build.ROOT/'预览'/'指令输入后报警.png')
for fault in range(1,9):
    rt.g.update(alarmIn=fault,alarmOn=1,alarmSec=fault*10,alarmPush=1);rt.execute(['click bInput,0'])
    if fault<8:
        rt.g.update(alarmOn=0,alarmPush=1);rt.execute(['click bInput,0'])
render().save(build.ROOT/'预览'/'六行报警记录.png')
# V8 regressions: actual descending plan from the user's screenshot, and the
# visible release/pressed states. These are QA images, not project defaults.
rt=verify.Runtime(pages,variables)
rt.g.update(nseg=2,tg1=120,hd1=300,tg2=60,hd2=300,sel=2)
rt.enter(1);render().save(build.ROOT/'预览'/'V8_120降至60程序预览.png')
rt.click('bApply');render().save(build.ROOT/'预览'/'V8_应用请求反馈.png')
rt.enter(0);rt.click('bStart');render().save(build.ROOT/'预览'/'V8_启动请求反馈.png')
pressed=rt.objs['bStart']['attr'];pressed['bco']=pressed['bco2'];pressed['pco']=pressed['pco2']
render().save(build.ROOT/'预览'/'V8_启动按钮按下颜色.png')
rt.enter(2);rt.click('bFreeze');rt.click('bLive');render().save(build.ROOT/'预览'/'V8_恢复实时反馈.png')
for pid in (0,1,3,4,5):
    rt.enter(pid);rt.g.update(run=0,ov=360,uv=200,oc=30,ot1=85,ot2=90,ot3=150)
    for name in ('bStart','bStop','bApply','bSave','bLock','bAck','bReset'):
        if name in rt.objs:rt.click(name);render()
print('Text overflow:',json.dumps(fits,ensure_ascii=False))
assert not fits
