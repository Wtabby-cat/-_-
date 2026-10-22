"""Execute the documented examples against the emitted project's offline model."""
import json,re
from pathlib import Path
import verify_generated as v
root=Path(__file__).resolve().parent
_,files,_=v.unpack(v.TARGET)
pages={int(name[:-3]):v.page(blob) for name,blob in files.items() if name.endswith('.pa')}
variables={k:int(n) for k,n in re.findall(r'^int (\w+)=(-?\d+)\r?$',files['Program.s'].decode('gbk'),re.M)}
examples=json.loads((root/'手册示例.json').read_text('utf8'))
md=(root/'指令调试说明.md').read_text('utf8')
assert md.count('```')%2==0
assert len(re.findall(r'^#### 附录 A\d+ ',md,re.M))==14
queries=0
for ex in examples:
    assert ex['var'] in variables,ex['var']
    assert '```text\n'+'\n'.join(ex['commands'])+'\n```' in md,ex
    rt=v.Runtime(pages,variables)
    for line in ex['setup']+ex['commands']:
        try:
            if line.startswith('get '):rt.get(line[4:]);queries+=1
            else:rt.command(line)
        except v.Jump as change:rt.enter(change.target)
    for name,expected in ex['expect'].items():
        actual=rt.get(name)
        assert actual==expected,(ex['category'],ex['var'],name,actual,expected)
print(f'PASS: {len(examples)} individual variable examples; {queries} numeric queries; 15 categories.')
