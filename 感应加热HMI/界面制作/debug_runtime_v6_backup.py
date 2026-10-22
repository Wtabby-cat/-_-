"""Native command-driven display code. No fabricated samples or elapsed clock."""
def install(pages,g,windows,rgb,draw,blue,green,teal):
    for k in ['stageSec','hold','tgAct','done']:g.pop(k,None)
    g.update(cmd=0,dirty=0,valid=0,temp10=0,target10=0,mean10=0,high10=0,low10=0,bus10=0,amp10=0,freq100=0,powerW=0,phase10=0,angle10=0,locked=0,phaseState=0,
             sampleSec=0,push=0,clearPlot=0,nBuf=0,lastSec=0,dataErr=0,fmtval=0,fmtfrac=0,span=1,ta=0,tb=0,ya=0,yb=0,delta=0,
             alarmIn=0,alarmOn=0,alarmSec=0,alarmPush=0,alarmCode=0,ac0=0,at0=0,as0=0,ac1=0,at1=0,as1=0)
    channels={'tt':'sampleSec','ct':'target10','av':'mean10','hi':'high10','pw':'powerW','fq':'freq100','ph':'phase10'}
    for i in range(32):
        for prefix in channels:g[f'{prefix}{i}']=0
    def obj(p,name):return next(o for o in p.items if o['name']==name)
    def dec(src,dst,places=1,suffix=''):
        scale=10**places
        code=[f'fmtval={src}','if(fmtval<0)','{','fmtval=0-fmtval','}',f'tmp=fmtval/{scale}',f'covx tmp,{dst}.txt,0,0']
        if places:
            code += [f'fmtfrac=fmtval%{scale}','covx fmtfrac,tFmt.txt,0,0']
            if places==2:code += ['if(fmtfrac<10)','{','tFmt.txt="0"+tFmt.txt','}']
            code += [f'{dst}.txt={dst}.txt+"."+tFmt.txt']
        code += [f'if({src}<0)','{',f'{dst}.txt="-"+{dst}.txt','}']
        if suffix:code += [f'{dst}.txt={dst}.txt+"{suffix}"']
        return code
    def integer(src,dst,suffix=''):
        code=[f'covx {src},{dst}.txt,0,0']
        if suffix:code += [f'{dst}.txt={dst}.txt+"{suffix}"']
        return code
    # Buttons expose requests through cmd; status always comes from input variables.
    for p in pages:
        for o in p.items:
            if o['name']=='bStop':o['code']='cmd=2'
        if p.pid!=7:
            p.load=[s for s in p.load if '演示' not in s]
            obj(p,'tFoot')['txt']=''
    obj(pages[0],'bStart')['code']='cmd=1'
    obj(pages[1],'bApply')['code']='if(run==0)\r\n{\r\ncmd=3\r\n}'
    obj(pages[1],'bSave')['code']='saved=1\r\ncmd=4'
    obj(pages[3],'bApply')['code']='if(run==0)\r\n{\r\ncmd=3\r\n}'
    obj(pages[4],'bLock')['code']='if(run==0)\r\n{\r\ncmd=5\r\n}'
    save=obj(pages[5],'bSave')
    save['code']=save['code'].replace('tFoot.txt="参数已暂存；尚未下发主控"','cmd=4\r\ntFoot.txt="参数已暂存"')
    obj(pages[5],'bReset')['code']='cmd=6'
    obj(pages[5],'bAck')['code']='cmd=7\r\nif(as0==1)\r\n{\r\nas0=2\r\n}\r\nif(as1==1)\r\n{\r\nas1=2\r\n}\r\ndirty=1'
    # Stage configuration changes do not forge measured stage feedback.
    for name in ['bAdd','bRemove']:
        o=obj(pages[1],name);o['code']=o['code'].replace('stage=0\r\ndone=0\r\n','')
    # Process an explicitly supplied snapshot, keeping the newest 32 samples.
    ingest=['if(clearPlot==1)','{','nBuf=0','lastSec=0','clearPlot=0','dirty=1','}',
            'if(push==1)','{','push=0','dataErr=0','if(sampleSec<0||sampleSec>86400)','{','dataErr=1','}else','{',
            'if(nBuf>0&&sampleSec<=lastSec)','{','dataErr=1','}else','{','if(nBuf==32)','{']
    for i in range(31):
        for prefix in channels:ingest += [f'{prefix}{i}={prefix}{i+1}']
    ingest += ['nBuf=31','}']
    for i in range(32):
        ingest += [f'if(nBuf=={i})','{']+[f'{prefix}{i}={src}' for prefix,src in channels.items()]+['}']
    ingest += ['nBuf=nBuf+1','lastSec=sampleSec','valid=1','dirty=1','}','}','}']
    # Alarm updates are explicit. Restoring a fault preserves its history entry.
    ingest += ['if(alarmPush==1)','{','alarmPush=0','if(alarmIn>=1&&alarmIn<=8)','{','if(alarmOn==1)','{',
               'if(alarmCode!=alarmIn)','{','ac1=ac0','at1=at0','as1=as0','ac0=alarmIn','at0=alarmSec','as0=1','}',
               'alarmCode=alarmIn','}else','{','if(alarmCode==alarmIn)','{','alarmCode=0','}',
               'if(ac0==alarmIn)','{','as0=3','}','if(ac1==alarmIn)','{','as1=3','}','}','dirty=1','}','}']
    names=['','测温数据中断','锁相失败','母线过压','母线欠压','谐振过流','功率管过温','整流桥过温','工件超温']
    def alarmtext(src,dst,empty=''):
        code=[f'{dst}.txt="{empty}"']
        for i,label in enumerate(names[1:],1):code += [f'if({src}=={i})','{',f'{dst}.txt="{label}"','}']
        return code
    for p in pages:
        p.text('tFmt','',0,0,16,16,font=3)
        p.load.insert(0,'vis tFmt,0')
        refresh=[]
        if p.pid!=7:
            refresh += ['tState.txt="待机"','if(run==1)','{','tState.txt="运行中"','}']
        fields={
            0:[('temp10','tActual',1),('target10','tTarget',1),('powerW','tPower',0),('bus10','tBus',1),('freq100','tFreq',2),('amp10','tCurrent',1)],
            2:[('target10','tTarget',1),('mean10','tMean',1),('high10','tHigh',1),('low10','tLow',1)],
            3:[('powerW','tPower',0),('angle10','tAngle',1)],4:[('freq100','tFreq',2)],6:[('phase10','tAngle',1)]}.get(p.pid,[])
        if fields:
            refresh += ['if(valid==1)','{']
            for src,dst,places in fields:refresh+=dec(src,dst,places)
            refresh+=['}else','{']+[f'{dst}.txt="--"' for _,dst,_ in fields]+['}']
        if p.pid==0:
            refresh+=integer('stage','tStage')+['tStage.txt=tStage.txt+" / "','covx nseg,tFmt.txt,0,0','tStage.txt=tStage.txt+tFmt.txt']
            refresh+=integer('totalSec','tElapsed',' s')+integer('remain','tRemain',' s')
            refresh+=['tPhase.txt="待机"']
            for value,label in [(1,'升温'),(2,'保温'),(3,'已完成')]:refresh +=[f'if(phaseState=={value})','{',f'tPhase.txt="{label}"','}']
            refresh += ['tsw bStart,1',f'bStart.bco={rgb(blue)}','if(run==1)','{','tsw bStart,0','bStart.bco=54970','}']
        if p.pid in (0,4,6):
            dest='tLock' if p.pid==0 else 'tLocked'
            refresh += [f'{dest}.txt="未锁定"','if(locked==1)','{',f'{dest}.txt="已锁定"','}']
        if p.pid==5:
            refresh+=alarmtext('alarmCode','tAlarm','无')
            refresh+=['tAlarm.pco='+str(rgb(green)),'if(alarmCode>0)','{','tAlarm.pco=63488','}']
            for i in range(2):
                refresh += [f'a{i}0.txt=""',f'a{i}1.txt=""',f'a{i}2.txt=""',f'if(ac{i}>0)','{']
                refresh+=integer(f'at{i}',f'a{i}0',' s')+alarmtext(f'ac{i}',f'a{i}1')
                for status,label in [(1,'未确认'),(2,'已确认'),(3,'已恢复')]:refresh += [f'if(as{i}=={status})','{',f'a{i}2.txt="{label}"','}']
                refresh+=['}']
        chart=[]
        if p.pid in windows:
            m=p.plots[0];x,y,w,h=[m[k] for k in ['x','y','w','h']]
            lo,hi=[item['var'] for item in windows[p.pid]]
            chart=[f'if({lo}>=0&&{hi}<=86400&&{hi}>{lo})','{',f'span={hi}-{lo}',f'covx {lo},{m["xt"][0]}.txt,0,0',f'covx {hi},{m["xt"][2]}.txt,0,0',f'tmp={hi}+{lo}','tmp=tmp/2',f'covx tmp,{m["xt"][1]}.txt,0,0',f'tmp={hi}+{lo}','tmp=tmp%2','if(tmp==1)','{',f'{m["xt"][1]}.txt={m["xt"][1]}.txt+".5"','}',f'fill {x},{y},{w+1},{h+1},65535']
            chart += [draw(op) for op in p.draw[m['drawstart']:m['gridend']]]
            traces={2:[('ct',0,1500,blue),('av',0,1500,green),('hi',0,1500,'#ff7c43')],3:[('pw',0,1500,teal)],4:[('fq',1500,2500,green)],6:[('ph',-300,300,teal)]}[p.pid]
            for prefix,ylo,yhi,color in traces:
                for i in range(1,32):
                    # Clip both endpoints in time, then map into the visible axes.
                    chart += [f'if(nBuf>{i}&&tt{i}>={lo}&&tt{i-1}<={hi})','{',f'ta=tt{i-1}',f'tb=tt{i}',f'ya={prefix}{i-1}',f'yb={prefix}{i}',
                              f'if(ta<{lo})','{','delta=yb-ya',f'tmp={lo}-ta','delta=delta*tmp','tmp=tb-ta','delta=delta/tmp','ya=ya+delta',f'ta={lo}','}',
                              f'if(tb>{hi})','{','delta=yb-ya',f'tmp={hi}-ta','delta=delta*tmp','tmp=tb-ta','delta=delta/tmp','yb=ya+delta',f'tb={hi}','}']
                    for val in ['ya','yb']:chart +=[f'if({val}<{ylo})','{',f'{val}={ylo}','}',f'if({val}>{yhi})','{',f'{val}={yhi}','}']
                    chart += [f'px=ta-{lo}',f'px=px*{w}/span+{x}',f'nx=tb-{lo}',f'nx=nx*{w}/span+{x}',f'py=ya-{ylo}' if ylo>=0 else f'py=ya+{-ylo}',f'py=py*{h}/{yhi-ylo}',f'py={y+h}-py',f'ny=yb-{ylo}' if ylo>=0 else f'ny=yb+{-ylo}',f'ny=ny*{h}/{yhi-ylo}',f'ny={y+h}-ny',f'line px,py,nx,ny,{rgb(color)}','}']
            chart += ['}else','{','tFoot.txt="时间范围无效：起点必须小于终点"','}']
        p.load+=refresh+chart
        # No clock advancement: the timer only consumes explicitly submitted input.
        p.timer='\r\n'.join(ingest+['if(dirty==1)','{']+refresh+(['if(frozen==0)','{']+chart+['}'] if chart else [])+['dirty=0','}'])
        p.button('bInput','',0,0,16,16,font=3,code=p.timer)
        p.load.insert(0,'vis bInput,0')
    # Validate paired time inputs before assigning either endpoint.
    ok=obj(pages[7],'bOK')
    for pair in windows.values():
        lo,hi=pair
        for cond in [f'field=={lo["id"]}&&editval>={hi["var"]}',f'field=={hi["id"]}&&editval<={lo["var"]}']:
            ok['code']=f'if({cond})\r\n{{\r\ntRange.txt="起点必须小于终点"\r\n}}else\r\n{{\r\n'+ok['code']+'\r\n}'
