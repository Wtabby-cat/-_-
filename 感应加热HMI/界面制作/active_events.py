"""V8: explicit release notifications and accepted business/parameter frames.

65 page component 00 FF FF FF: touch release (not acceptance).
A5 5A 01 page request FF FF FF: accepted business request.
A5 5A 02 source_page field value_le32 FF FF FF: committed numeric input.
Do not additionally enable sendkey, which would duplicate touch notifications.
"""
import re

def install_events(pages, params):
    def item(pid,name):return next(o for o in pages[pid].items if o['name']==name)
    item(1,'tFoot').update(y=424,h=16,w=770)
    # Curve controls request the MCU archive; preserve their epoch and range.
    item(5,'bSave')['code']=item(5,'bSave')['code'].replace('\r\ntFoot.txt="参数已暂存"','')
    item(0,'bStart')['code']='if(run==0)\r\n{\r\ncmd=1\r\n}else\r\n{\r\ntFoot.txt="正在运行"\r\n}'
    for pid,name in [(1,'bApply'),(3,'bApply'),(4,'bLock')]:
        item(pid,name)['code']+='else\r\n{\r\ntFoot.txt="请停止加热后操作"\r\n}'
    item(5,'bSave')['code']+='else\r\n{\r\ntFoot.txt="请停止加热后操作"\r\n}'
    item(1,'bAdd')['code']+='else\r\n{\r\ntFoot.txt="运行中或已达12段，不能插入"\r\n}'
    item(1,'bRemove')['code']+='else\r\n{\r\ntFoot.txt="运行中或仅剩1段，不能删除"\r\n}'
    for name,tip in [('bAdd','已插入温段'),('bRemove','已删除温段')]:
        o=item(1,name);o['code']=o['code'].replace('click bTable,0',f'click bTable,0\r\ntFoot.txt="{tip}"')
    for index in range(3):
        item(1,'bOpt'+str(index))['code']+='\r\ntFoot.txt="算法已暂选；闭环尚未启用"'
    # These mutations do not go through the keypad. Notify the MCU too, so
    # a pending multi-get profile cannot mix fields from different edits.
    # Field 0 is a profile-structure/mode edit, never a numeric parameter.
    for name in ['bAdd','bRemove','bOpt0','bOpt1','bOpt2']:
        o=item(1,name)
        variable='mode' if name.startswith('bOpt') else 'nseg'
        notice=f'printh A5 5A 02 01 00\r\nprints {variable},4\r\nprinth FF FF FF'
        o['code']=o['code'].replace('click bTable,0','click bTable,0\r\n'+notice)
    ok=item(7,'bOK')
    ok['code']=ok['code'].replace('page backpg','\r\n'.join([
        'printh A5 5A 02','prints backpg,1','prints field,1','prints editval,4',
        'printh FF FF FF','page backpg']))
    hints={1:'启动请求已发送；等待主控',2:'停止请求已发送；等待主控',
           3:'应用请求已发送；等待主控',4:'保存请求已发送；等待主控',
           5:'搜频请求已发送；等待主控',6:'复位请求已发送；等待主控',
           7:'已确认记录；通知已发送'}
    result=[]
    for p in pages:
        for cid,o in enumerate(p.items,1):
            if o['kind']!='button' or o['name'] in ('bInput','bTable','bView','bCurve'):continue
            def emit(match):
                cmd=int(match[1])
                return f'cmd={cmd}\r\nprinth A5 5A 01 {p.pid:02X} {cmd:02X} FF FF FF\r\ntFoot.txt="{hints[cmd]}"'
            # splitlines avoids CR characters affecting the regex anchors.
            o['code']='\r\n'.join(emit(re.fullmatch(r'cmd=([1-7])',line)) if re.fullmatch(r'cmd=([1-7])',line) else line for line in o['code'].splitlines())
            touch=bytes([0x65,p.pid,cid,0,255,255,255]).hex(' ').upper()
            o['code']='printh '+touch+'\r\n'+o['code']
            result.append(dict(page=p.pid,id=cid,name=o['name'],label=o['txt'],touch=touch,
                               requests=sorted(set(int(n) for n in re.findall(r'cmd=([1-7])(?:\r|\n|$)',o['code'])))))
    return dict(version=8,buttons=result,params=params)
