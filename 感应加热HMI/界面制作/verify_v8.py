"""Regression tests using the emitted project, including serial bytes and draw ordering.
This is an offline interpreter, not an official compilation/simulator result.
"""
import json,re
from pathlib import Path
import verify_generated as v
root=Path(__file__).resolve().parent
_,files,_=v.unpack(v.TARGET)
pages={int(n[:-3]):v.page(b) for n,b in files.items() if n.endswith('.pa')}
g={k:int(n) for k,n in re.findall(r'^int (\w+)=(-?\d+)\r?$',files['Program.s'].decode('gbk'),re.M)}
mapping=json.loads((root/'串口事件映射.json').read_text('utf8'))
md=(root/'指令调试说明.md').read_text('utf8')
examples=json.loads((root/'双向通信例子.json').read_text('utf8'))['parameter_examples']
def fresh(pid):
    rt=v.Runtime(pages,g);rt.g.update(nseg=12,ov=360,uv=200,oc=30,ot1=85,ot2=90,ot3=150)
    rt.enter(pid);return rt
def touch(pid,name):return bytes.fromhex(next(b['touch'] for b in mapping['buttons'] if b['page']==pid and b['name']==name))
def req(pid,q):return bytes([0xa5,0x5a,1,pid,q,255,255,255])

for b in mapping['buttons']:
    pid,name=b['page'],b['name'];rt=fresh(pid)
    if pid==7:
        rt.enter(1);rt.click('bplim');rt.tx.clear()
    if name.startswith('bOpt'):rt.click('bMode')
    match=re.fullmatch(r'b(?:tg|hd)(\d+)',name)
    row=re.fullmatch(r'bRow(\d+)',name)
    index=int(match[1]) if match else int(row[1])+1 if row else 1
    if pid==1 and index>6:
        rt.g.update(stagepage=1,sel=index);rt.enter(1)
    rt.tx.clear();rt.click(name)
    assert rt.tx.startswith(touch(pid,name)),(pid,name,rt.tx)
    assert b['touch'] in md
    attr=next(o['attr'] for o in pages[pid] if o['attr']['objname']==name)
    assert attr['bco2']!=attr['bco'] and attr['bco2']!=5116,(pid,name)
    # Press color must also differ from the dynamically selected row blue.
    assert attr['bco2']!=((0x13>>3)<<11|(0x7d>>2)<<5|(0xe1>>3))
    if b['requests']:
        assert len(b['requests'])==1
        frame=req(pid,b['requests'][0]);assert rt.tx==touch(pid,name)+frame,(pid,name,rt.tx)
        assert frame.hex(' ').upper() in md
        assert '发送' in rt.get('tFoot.txt')

for pid,name in [(1,'bApply'),(3,'bApply'),(4,'bLock'),(5,'bSave')]:
    rt=fresh(pid);rt.g['run']=1;rt.tx.clear();rt.click(name)
    assert rt.tx==touch(pid,name) and rt.g['cmd']==0
    assert '停止加热' in rt.get('tFoot.txt')
rt=fresh(0);rt.g.update(run=1,dirty=1);rt.execute(['click bInput,0']);rt.tx.clear();rt.click('bStart');assert not rt.tx
for values in [dict(ov=0),dict(ov=100,uv=200)]:
    rt=fresh(5);rt.g.update(values);rt.click('bSave');assert rt.tx==touch(5,'bSave')

for p in mapping['params']:
    pid=next(b['page'] for b in mapping['buttons'] if b['name']=='b'+p['var'])
    rt=fresh(pid)
    if p['var'].startswith(('tg','hd')):
        index=int(p['var'][2:]);rt.g.update(stagepage=(index-1)//6,sel=index);rt.enter(pid)
    rt.click('b'+p['var']);assert rt.pid==7
    val=examples[p['var']];rt.g['editval']=val;rt.tx.clear();rt.click('bOK')
    frame=bytes([0xa5,0x5a,2,pid,p['id']])+val.to_bytes(4,'little')+b'\xff'*3
    assert rt.tx==touch(7,'bOK')+frame,(p,rt.tx)
    assert frame.hex(' ').upper() in md
    assert rt.g[p['var']]==val and rt.pid==pid
    rt.click('b'+p['var']);rt.tx.clear();rt.g['editval']=999999;rt.click('bOK')
    assert rt.pid==7 and rt.tx==touch(7,'bOK') and rt.g[p['var']]==val
    rt.tx.clear();rt.click('bCancel');assert rt.tx==touch(7,'bCancel') and rt.g[p['var']]==val
# Other source of the shared power limit.
rt=fresh(3);rt.click('bplim');rt.g['editval']=1800;rt.tx.clear();rt.click('bOK')
assert rt.tx==touch(7,'bOK')+bytes.fromhex('A5 5A 02 03 01 08 07 00 00 FF FF FF')
# Each accepted click is sent even if cmd was not cleared between clicks.
rt=fresh(0);rt.click('bStart');rt.click('bStop')
assert rt.tx==touch(0,'bStart')+req(0,1)+touch(0,'bStop')+req(0,2)
for pid in range(8):
    rt=fresh(pid);rt.tx.clear();rt.tick();rt.execute(['click bInput,0']);assert not rt.tx
rt=fresh(1);rt.execute(['click bTable,0']);assert not rt.tx
for pid in [2,3,4,6]:
    rt=fresh(pid);rt.click('bFreeze');count=rt.enter_count;rt.tx.clear();rt.click('bLive')
    assert rt.tx==touch(pid,'bLive') and rt.g['frozen']==0
    assert rt.enter_count==count and rt.get('tFoot.txt')=='已恢复实时'

# Actual GUI commands must follow the flush barrier. ref_stop does NOT defer
# line/fill according to the vendor help; the previous version drew too early.
def assert_graph_order(rt):
    lines=[(s,stopped) for pid,s,stopped in rt.trace if pid==1]
    flush=-1;last_control=-1;count=0
    for i,(s,stopped) in enumerate(lines):
        if s=='doevents':flush=i
        if s.startswith(('line ','fill ')):
            assert flush>last_control and not stopped,(i,s,flush,last_control,stopped)
            count+=1
        # Only controls intersecting the graph may erase it; all known controls
        # are flushed before plotting, except the non-overlapping tFoot hint.
        if s.startswith(('ref ','vis ','covx ')) or ('.' in s and '=' in s and not s.startswith('tFoot.')):
            last_control=i
    assert count>8
blue=(0x13>>3)<<11|(0x7d>>2)<<5|(0xe1>>3)
for temps,durations in [([120,60],[300,300]),([60,120],[1,9999]),([999],[9999]),
                       ([1]*12,[1]*12),([999,1]*6,[9999]*12)]:
    rt=fresh(1);rt.g.update(nseg=len(temps),stagepage=0,sel=1)
    for i,(t,h) in enumerate(zip(temps,durations),1):rt.g.update({f'tg{i}':t,f'hd{i}':h})
    rt.trace.clear();rt.drawing.clear();rt.execute(['click bTable,0']);assert_graph_order(rt)
    if temps==[120,60]:
        expected=[('line',[526,186,649,186,blue]),('line',[649,186,649,236,blue]),('line',[649,236,772,236,blue])]
        assert all(x in rt.drawing for x in expected),rt.drawing
    for mode in range(3):
        rt.click('bMode');rt.trace.clear();rt.click('bOpt'+str(mode));assert_graph_order(rt)
    if len(temps)<12:
        rt.trace.clear();rt.click('bAdd');assert_graph_order(rt)
    rt.trace.clear();rt.click('bRemove');assert_graph_order(rt)
print('PASS V8: 144 touch frames; accepted/rejected requests; 41 parameter frames + shared field; silent timers; press colors and hints; screenshot descending graph and 12-stage redraw order.')
