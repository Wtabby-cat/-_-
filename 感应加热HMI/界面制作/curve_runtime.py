"""Render MCU-owned one-second history without a short, scrolling HMI buffer.

Packets contain up to 16 nonnegative int32 values: 7-bit time delta and three
8-bit pixel heights. 255 breaks a trace. Only horizontal pixel runs compress;
all samples remain in MCU RAM. Epochs reject packets queued before navigation.
"""
def install(pages, g, windows, rgb, draw, blue, green, teal):
    g.update(curvePage=255,curveEpoch=0,freezeSec=0,plotCut=-1,
             plotPage=255,plotEpoch=0,plotReset=0,plotStart=0,plotCount=0,plotRun=0,
             runId=0,seenRun=0,axisLo=0,axisHi=1500,plotYLo=0,plotYHi=1500,plotTick=0,
             plotSec=0,plotPrevX=0,plotY0=255,plotY1=255,plotY2=255)
    for i in range(16):g[f'cv{i}']=0
    expands={}
    def axis_label(dst,scale):
        code=['tNum.txt=""','if(plotTick<0)','{','tNum.txt="-"','plotTick=0-plotTick','}',
              f'tmp=plotTick/{scale}','covx tmp,tFmt.txt,0,0','tNum.txt=tNum.txt+tFmt.txt',f'plotTick=plotTick%{scale}']
        if scale>1:
            code += ['if(plotTick>0)','{','tNum.txt=tNum.txt+"."']
            if scale==100:code += ['if(plotTick<10)','{','tNum.txt=tNum.txt+"0"','}']
            code += ['covx plotTick,tFmt.txt,0,0','tNum.txt=tNum.txt+tFmt.txt','}']
        return code+[f'{dst}.txt=tNum.txt']
    for p in pages:
        p.load.insert(0,f'curvePage={p.pid if p.pid in windows else 255}')
        if p.pid not in windows:
            # TJC resolves component references even in a branch that will not
            # execute. bInput's clearPlot branch exists on every page.
            p.button('bView','',0,0,16,16,font=3)
            p.button('bCurve','',0,0,16,16,font=3)
            p.load[:0]=['vis bView,0','vis bCurve,0']
            continue
        m=p.plots[0];x,y,w,h=[m[k] for k in ('x','y','w','h')]
        lo,hi=[pr['var'] for pr in windows[p.pid]]
        period=600 if p.pid in (2,3) else 60
        scale={2:10,3:1,4:100,6:10}[p.pid]
        def axes(yl,yh):
            result=[f'fill {x},{y},{w+1},{h+1},65535']
            result += [draw(op) for op in p.draw[m['drawstart']:m['gridend']]]
            for index,dst in enumerate(m['yt']):
                result += [f'plotTick={yh}-{yl}',f'plotTick=plotTick*{index}/{len(m["yt"])-1}',f'plotTick=plotTick+{yl}']
                result += axis_label(dst,scale)
            return result
        if p.pid==2:
            setup=['axisLo=0','axisHi=0']
            for index in range(1,13):
                setup += [f'if(nseg>={index}&&tg{index}>axisHi)','{',f'axisHi=tg{index}','}']
            setup += ['axisHi=axisHi+30','axisHi=axisHi*10']
        elif p.pid==3:setup=['axisLo=0','axisHi=plim']
        elif p.pid==4:setup=['axisLo=flo*100','axisHi=fhi*100']
        else:setup=['axisLo=0-300','axisHi=300']
        # Dedicated view frames use the existing signed parameter framing.
        view=setup+['curveEpoch=curveEpoch+1','if(curveEpoch>65535)','{','curveEpoch=1','}',
              'plotCut=-1','if(frozen==1)','{','plotCut=freezeSec','}',
              f'covx {lo},b{lo}.txt,0,0',f'covx {hi},b{hi}.txt,0,0']
        view += axes('axisLo','axisHi')
        for field,src in ((42,lo),(43,hi),(44,'plotCut'),(46,'axisLo'),(47,'axisHi'),(48,'runId'),(45,'curveEpoch')):
            view += [f'printh A5 5A 02 {p.pid:02X} {field:02X}',f'prints {src},4','printh FF FF FF']
        p.button('bView','',0,0,16,16,font=3,code='\r\n'.join(view))
        p.load += ['vis bView,0','click bView,0']
        expand=['if(follow==1&&frozen==0&&'+f'lastSec>{hi})','{',
                f'{lo}=0',f'{hi}=lastSec/{period}',f'{hi}={hi}+1',f'{hi}={hi}*{period}',
                'click bView,0','}']
        expands[p.pid]=expand
        for o in p.items:
            if o['name']=='bFreeze':
                o['code']='frozen=1\r\nfreezeSec=lastSec\r\nclick bView,0\r\ntFoot.txt="已冻结显示"'
            if o['name']=='bLive':
                o['code']='\r\n'.join(['frozen=0','follow=1',f'{lo}=0',
                    f'{hi}=lastSec/{period}',f'{hi}={hi}+1',f'{hi}={hi}*{period}',
                    'click bView,0','tFoot.txt="已恢复实时"'])
        code=[f'if(plotPage=={p.pid}&&curvePage=={p.pid}&&plotEpoch==curveEpoch&&plotRun==runId&&plotCount>=0&&plotCount<=16)',
              '{',f'if({lo}>=0&&{hi}<=86400&&{hi}>{lo})','{',f'span={hi}-{lo}',
              'if(plotReset==1)','{','plotY0=255','plotY1=255','plotY2=255']
        code += axes('plotYLo','plotYHi')
        code += [f'covx {lo},{m["xt"][0]}.txt,0,0',f'covx {hi},{m["xt"][2]}.txt,0,0',
                 f'tmp={lo}+{hi}','tmp=tmp/2',f'covx tmp,{m["xt"][1]}.txt,0,0',
                 f'tmp={lo}+{hi}','tmp=tmp%2','if(tmp==1)','{',
                 f'{m["xt"][1]}.txt={m["xt"][1]}.txt+".5"','}',
                 '}','plotSec=plotStart']
        colors=[blue,green,'#ff7c43'] if p.pid==2 else [green if p.pid==4 else teal]
        for i in range(16):
            code += [f'if(plotCount>{i})','{',f'tmp=cv{i}/16777216','plotSec=plotSec+tmp',
                     f'if(plotSec>={lo}&&plotSec<={hi})','{',f'nx=plotSec-{lo}',f'nx=nx*{w}/span+{x}']
            for j,color in enumerate(colors):
                code += [f'py=cv{i}/{256**j}','py=py%256','if(py<=176)','{',f'ny={y+h}-py',
                         f'if(plotY{j}<=176)','{',f'px=plotPrevX',f'tmp={y+h}-plotY{j}',
                         f'line px,tmp,nx,ny,{rgb(color)}','}else','{',
                         f'line nx,ny,nx,ny,{rgb(color)}','}',f'plotY{j}=py','}else','{',f'plotY{j}=255','}']
            code += ['plotPrevX=nx','}','}']
        code += ['}','}']
        p.button('bCurve','',0,0,16,16,font=3,code='\r\n'.join(code))
        p.load.insert(0,'vis bCurve,0')
    return expands
