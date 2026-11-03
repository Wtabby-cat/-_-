"""Native command-driven display code. No fabricated samples or elapsed clock."""
ALARM_ROWS=6
CHANNEL_VALIDITY=True
def install(pages,g,windows,rgb,draw,blue,green,teal):
    for k in ['stageSec','hold','tgAct','done']:g.pop(k,None)
    g.update(cmd=0,dirty=0,valid=0,temp10=0,target10=0,mean10=0,high10=0,low10=0,bus10=0,amp10=0,freq100=0,power100=0,phase10=0,angle10=0,locked=0,phaseState=0,
             sampleSec=0,push=0,clearPlot=0,nBuf=0,lastSec=0,dataErr=0,fmtval=0,fmtfrac=0,span=1,ta=0,tb=0,ya=0,yb=0,delta=0,
             alarmIn=0,alarmOn=0,alarmSec=0,alarmPush=0,alarmCode=0)
    g.update(msgCode=0,msgA=0,msgB=0,msgPush=0,internalFault=0,trackState=0,powerSet100=5000)
    g.update(temp110=0,temp210=0,temp1mv=0,temp2mv=0,follow=1,adcClipped=0)
    g.update(fwVersion=0,tripCount=0,tripGate=0,tripMs=0,tripRaw1=0,tripRaw2=0)
    for i in range(ALARM_ROWS):
        for prefix in ['ac','at','as']:g[f'{prefix}{i}']=0
    if CHANNEL_VALIDITY:
        g.update(dataFlags=0,fieldOK=0,va=0,vb=0)
    from curve_runtime import install as install_curves
    expands=install_curves(pages,g,windows,rgb,draw,blue,green,teal)
    def obj(p,name):return next(o for o in p.items if o['name']==name)
    def dec(src,dst,places=1,suffix='',fit=False):
        scale=10**places
        # Build sign, integer part and fraction in a hidden string. Never
        # prepend to a string while also using that string as the destination;
        # the visible control receives only the completed value, once.
        code=[f'fmtval={src}','tNum.txt=""','if(fmtval<0)','{','tNum.txt="-"','fmtval=0-fmtval','}',
              f'tmp=fmtval/{scale}','covx tmp,tFmt.txt,0,0','tNum.txt=tNum.txt+tFmt.txt']
        if places:
            code += ['tNum.txt=tNum.txt+"."',f'fmtfrac=fmtval%{scale}']
            for limit in reversed([10**d for d in range(1,places)]):
                code += [f'if(fmtfrac<{limit})','{','tNum.txt=tNum.txt+"0"','}']
            code += ['covx fmtfrac,tFmt.txt,0,0','tNum.txt=tNum.txt+tFmt.txt']
        if suffix:code += [f'tNum.txt=tNum.txt+"{suffix}"']
        if fit:
            # With the bundled fonts, up to six signed/decimal ASCII chars
            # fit at 44 px. Seven or more need the existing 32 px font.
            code += ['strlen tNum.txt,tmp','if(tmp>6)','{',f'if({dst}.font!=4)','{',f'{dst}.font=4','}',
                     '}else','{',f'if({dst}.font!=1)','{',f'{dst}.font=1','}','}']
        code += [f'{dst}.txt=tNum.txt']
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
    obj(pages[5],'bAck')['code']='\r\n'.join(['cmd=7']+[f'if(as{i}==1)\r\n{{\r\nas{i}=2\r\n}}' for i in range(ALARM_ROWS)]+['dirty=1'])
    # Stage configuration changes do not forge measured stage feedback.
    for name in ['bAdd','bRemove']:
        o=obj(pages[1],name);o['code']=o['code'].replace('stage=0\r\ndone=0\r\n','')
    ingest=['if(runId!=seenRun)','{','seenRun=runId','clearPlot=1','}',
            'if(clearPlot==1)','{','clearPlot=0','frozen=0','follow=1','dirty=1']
    for pair in windows.values():
        ingest += [f'{pair[0]["var"]}=0',f'{pair[1]["var"]}={pair[1]["initial"]}']
    ingest += ['if(curvePage!=255)','{','click bView,0','}','}']
    # Alarm updates are explicit. Restoring a fault preserves its history entry.
    ingest += ['if(alarmPush==1)','{','alarmPush=0','if(alarmIn>=1&&alarmIn<=8)','{','if(alarmOn==1)','{','if(alarmCode!=alarmIn)','{']
    for i in range(ALARM_ROWS-1,0,-1):
        ingest += [f'{prefix}{i}={prefix}{i-1}' for prefix in ['ac','at','as']]
    ingest += ['ac0=alarmIn','at0=alarmSec','as0=1','}','alarmCode=alarmIn','}else','{','if(alarmCode==alarmIn)','{','alarmCode=0','}']
    for i in range(ALARM_ROWS):ingest += [f'if(ac{i}==alarmIn)','{',f'as{i}=3','}']
    ingest += ['}','dirty=1','}','}']
    names=['','测温数据中断','锁相失败','母线过压','母线欠压','谐振过流','功率管过温','整流桥过温','工件超温']
    def alarmtext(src,dst,empty=''):
        code=[f'{dst}.txt="{empty}"']
        for i,label in enumerate(names[1:],1):code += [f'if({src}=={i})','{',f'{dst}.txt="{label}"','}']
        return code
    for p in pages:
        p.text('tFmt','',0,0,16,16,font=3)
        p.text('tNum','',0,0,16,16,font=3)
        p.load[:0]=['vis tFmt,0','vis tNum,0']
        refresh=[]
        if p.pid!=7:
            refresh += ['tState.txt="待机"','if(run==1)','{','tState.txt="运行中"','}']
        fields={
            0:[('temp10','tActual',1),('target10','tTarget',1),('power100','tPower',2),('bus10','tBus',1),('freq100','tFreq',2),('amp10','tCurrent',1)],
            2:[('target10','tTarget',1),('mean10','tMean',1),('high10','tHigh',1),('low10','tLow',1)],
            3:[('power100','tPower',2),('angle10','tAngle',1)],4:[('freq100','tFreq',2)],6:[('phase10','tAngle',1)]}.get(p.pid,[])
        if fields:
            if CHANNEL_VALIDITY:
                masks={'temp10':1,'target10':2,'power100':4,'mean10':128,'high10':128,'low10':128,'bus10':8,'amp10':16,'freq100':32,
                       'phase10':64,'angle10':256,'temp110':512,'temp210':2048,'temp1mv':1024,'temp2mv':1024}
                for src,dst,places in fields:
                    fit=p.pid==0 and dst=='tPower'
                    refresh += [f'fieldOK=dataFlags&{masks[src]}','if(fieldOK>0)','{']+dec(src,dst,places,fit=fit)+['}else','{']
                    if fit:refresh += [f'{dst}.font=1']
                    refresh += [f'{dst}.txt="--"','}']
            else:
                refresh += ['if(valid==1)','{']
                for src,dst,places in fields:refresh+=dec(src,dst,places)
                refresh+=['}else','{']+[f'{dst}.txt="--"' for _,dst,_ in fields]+['}']
        if p.pid==0:
            refresh+=integer('stage','tStage')+['tStage.txt=tStage.txt+" / "','covx nseg,tFmt.txt,0,0','tStage.txt=tStage.txt+tFmt.txt']
            refresh+=integer('totalSec','tElapsed',' s')+integer('remain','tRemain',' s')
            refresh+=['tPhase.txt="待机"']
            for value,label in [(1,'升温'),(2,'保温'),(3,'已完成'),(4,'定频输出'),(5,'正在搜频'),(6,'低功率启动'),(7,'锁相捕获'),(8,'锁相跟踪')]:refresh +=[f'if(phaseState=={value})','{',f'tPhase.txt="{label}"','}']
            refresh += ['tsw bStart,1',f'bStart.bco={rgb(blue)}','if(run==1)','{','tsw bStart,0','bStart.bco=54970','}']
        if p.pid in (0,4,6):
            dest='tLock' if p.pid==0 else 'tLocked'
            refresh += [f'{dest}.txt="未锁定"','if(locked==1)','{',f'{dest}.txt="已锁定"','}']
            refresh += ['if(run==1&&trackState==1)','{',f'{dest}.txt="启动中"','}',
                        'if(run==1&&trackState==2)','{',f'{dest}.txt="搜频中"','}',
                        'if(run==1&&trackState==3)','{',f'{dest}.txt="锁相捕获"','}']
        if p.pid==3:
            refresh += dec('powerSet100','tSetpoint',2,' W')
        if p.pid==6:
            refresh += ['tInfo.txt="启动后显示相位"','if(run==1)','{',
                        'tInfo.txt="等待有效相位"','fieldOK=dataFlags&64','if(fieldOK>0)','{',
                        'tInfo.txt="电压超前电流为正"','tmp=0-300','if(phase10<tmp||phase10>300)','{',
                        'tInfo.txt="超出±30°曲线范围"','}','}','}']
        if p.pid==5:
            refresh+=alarmtext('alarmCode','tAlarm','无')
            refresh+=['tAlarm.pco='+str(rgb(green)),'if(alarmCode>0)','{','tAlarm.pco=63488','}']
            for i in range(ALARM_ROWS):
                refresh += [f'a{i}0.txt=""',f'a{i}1.txt=""',f'a{i}2.txt=""',f'if(ac{i}>0)','{']
                refresh+=integer(f'at{i}',f'a{i}0',' s')+alarmtext(f'ac{i}',f'a{i}1')
                for status,label in [(1,'未确认'),(2,'已确认'),(3,'已恢复')]:refresh += [f'if(as{i}=={status})','{',f'a{i}2.txt="{label}"','}']
                refresh+=['}']
            refresh += ['if(internalFault>0)','{','tAlarm.txt="采样或通信异常"','tAlarm.pco=63488','}']
            refresh += ['fieldOK=internalFault&4','if(fieldOK>0)','{','tAlarm.txt="搜频或锁相异常"','}']
            refresh += ['fieldOK=internalFault&4096','if(fieldOK>0)','{','tAlarm.txt="功率闭环异常"','}']
        if p.pid in (0,5):
            # Append after all original visible controls to retain their IDs.
            p.text('tDiag','',408,416,380,20,font=3)
            refresh += ['tDiag.txt="固件号："','covx fwVersion,tFmt.txt,0,0',
                        'tDiag.txt=tDiag.txt+tFmt.txt','if(tripCount>0)','{',
                        'tDiag.txt="驱动前 "','if(tripGate==1)','{','tDiag.txt="驱动后 "','}',
                        'covx tripMs,tFmt.txt,0,0','tDiag.txt=tDiag.txt+tFmt.txt',
                        'tDiag.txt=tDiag.txt+"ms 原码"','covx tripRaw1,tFmt.txt,0,0',
                        'tDiag.txt=tDiag.txt+tFmt.txt','tDiag.txt=tDiag.txt+"/"',
                        'covx tripRaw2,tFmt.txt,0,0','tDiag.txt=tDiag.txt+tFmt.txt','}']
        # Field refresh never clears a plot. Only a new view epoch resets it.
        chart=expands.get(p.pid,[])
        from feedback_runtime import render
        messages=render() if p.pid!=7 else []
        p.load+=refresh+chart+messages
        # No clock advancement: the timer only consumes explicitly submitted input.
        p.timer='\r\n'.join(ingest+['if(dirty==1)','{']+refresh+(['if(frozen==0)','{']+chart+['}'] if chart else [])+['dirty=0','}']+messages)
        p.button('bInput','',0,0,16,16,font=3,code=p.timer)
        p.load.insert(0,'vis bInput,0')
    # Validate paired time inputs before assigning either endpoint.
    ok=obj(pages[7],'bOK')
    # Manual time windows disable following only on an accepted edit.
    for pair in windows.values():
        for pr in pair:
            token=f'{pr["var"]}=editval'
            ok['code']=ok['code'].replace(token,token+'\r\nfollow=0')
    for pair in windows.values():
        lo,hi=pair
        for cond in [f'field=={lo["id"]}&&editval>={hi["var"]}',f'field=={hi["id"]}&&editval<={lo["var"]}']:
            ok['code']=f'if({cond})\r\n{{\r\ntRange.txt="起点必须小于终点"\r\n}}else\r\n{{\r\n'+ok['code']+'\r\n}'
