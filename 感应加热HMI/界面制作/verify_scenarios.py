import json,re
from pathlib import Path
import verify_generated as v
root=Path(__file__).resolve().parent
_,files,_=v.unpack(v.TARGET)
pages={int(name[:-3]):v.page(blob) for name,blob in files.items() if name.endswith('.pa')}
variables={k:int(n) for k,n in re.findall(r'^int (\w+)=(-?\d+)\r?$',files['Program.s'].decode('gbk'),re.M)}
cases=json.loads((root/'操作示例.json').read_text('utf8'))
md=(root/'指令调试说明.md').read_text('utf8')
byid={c['id']:c for c in cases}
def send(rt,commands):
    for line in commands:
        try:
            if line.startswith('get '):rt.get(line[4:])
            else:rt.command(line)
        except v.Jump as jump:rt.enter(jump.target)
for c in cases:
    rt=v.Runtime(pages,variables)
    if c['id'] in ('alarm_ack','alarm_restore'):send(rt,byid['alarm_trigger']['commands'])
    if c['id']=='time_window':send(rt,byid['temperature_curve']['commands'])
    if not c.get('combined'):assert '```text\n'+'\n'.join(c['commands'])+'\n```' in md,c['id']
    send(rt,c['commands'])
    for name,value in c['expect'].items():assert rt.get(name)==value,(c['id'],name,rt.get(name),value)
    if c['id']=='alarm_ack':assert rt.get('a02.txt')=='已确认' and rt.get('alarmCode')==3
    if c['id']=='alarm_restore':assert rt.get('a02.txt')=='已恢复'
    if c['id'].endswith('_curve') and c['id']!='clear_curve':
        # Distinct samples are retained; line drawing includes coloured traces.
        assert rt.g['tt0']==0 and rt.g['tt1']==10 and rt.g['tt2']==20
        assert any(kind=='line' and values[-1] in (5116,748,1298) for kind,values in rt.drawing),c['id']
    if c.get('hex'):
        encoded=[(s.encode('ascii')+b'\xff\xff\xff').hex(' ').upper() for s in c['commands']]
        assert '```hex\n'+'\n'.join(encoded)+'\n```' in md
        for s,h in zip(c['commands'],encoded):assert bytes.fromhex(h)==s.encode('ascii')+b'\xff\xff\xff'
assert md.count('```')%2==0
print('PASS:',len(cases),'operation examples; alarm trigger/ack/restore; four curves; text/HEX framing.')
