"""Markdown interface manual. The output file is maintained in place."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'指令调试说明.md'
lines=[];examples=[];current='';setup=[];page_titles=[]
def p(text,style=None):
    lines.extend([('# ' if style=='Title' else '')+text,''])
def h(text,level=2):lines.extend(['#'*level+' '+text,''])
def block(text):lines.extend(['```text',text,'```',''])
def table(headers,rows,widths=None,codecol=None):
    def esc(x):return str(x).replace('|','\|').replace('\n','<br>')
    lines.append('| '+' | '.join(map(esc,headers))+' |')
    lines.append('| '+' | '.join('---' for _ in headers)+' |')
    for row in rows:lines.append('| '+' | '.join(map(esc,row))+' |')
    lines.append('')
def page(title,prep=None,note=''):
    global current,setup
    current=title;setup=prep or [];page_titles.append(title)
    h(str(len(page_titles)+1)+' '+title)
    if setup:
        p('本节准备：先依次执行下面的指令，再分别测试本节各项。每个示例按行依次发送；如需从相同初态单独重测，请再次执行本节准备。')
        block('\n'.join(setup))
    if note:p(note)
def row(label,var,rules,commands,result,expect=None):
    examples.append(dict(category=current,var=var,setup=setup.copy(),commands=commands.splitlines(),expect=expect or {}))
    return (f'{label}\n{var}\n{rules}',commands,result)
def measured(var,value):return f'{var}={value}\nvalid=1\ndirty=1\nclick bInput,0'
def status(var,value):return f'{var}={value}\ndirty=1\nclick bInput,0'
def fields(rows):
    for first,commands,result in rows:
        label,var,rules=first.split('\n',2)
        h(label+' `'+var+'`',3)
        p('变量：`'+var+'`。'+rules.replace('\n','；')+'。')
        block(commands)
        p('解释与结果：'+result)
def setting(var,value,pid):return f'{var}={value}\n'+('click bTable,0' if pid==1 else f'page {pid}')
p('感应加热 HMI 变量与逐项调试手册','Title')
p('适用工程  加热1_调试版_V7.HMI')
p('本文件为持续维护的调试手册，后续直接修改本文件。按界面和数据类别查找变量，复制对应示例即可调试。每项都写明变量名称、单位或倍率、发送内容及预期结果。屏幕不会自动产生测量值、运行时间或故障。')
h('1 使用与发送方法',2)
p('模拟器选择“指令发送到 当前模拟器”和 String 模式。示例一行一条命令；page 命令单独执行后，再发其余行。不要把多行代码粘成一条命令。')
p('STM32 发送相同的 ASCII 文本，每条命令后追加三个二进制 0xFF 字节；模拟器 String 输入区不需要手工追加。工程初始波特率为 9600，两端串口配置保持一致。变量区分大小写，赋值使用整数。')
p('例如母线电压 308.0 V：先发送 page 0，再依次发送：')
block('bus10=3080\nvalid=1\ndirty=1\nclick bInput,0')
p('bus10 的倍率是 10，所以 308.0 × 10 = 3080。valid=1 允许显示读数；dirty=1 标记刷新；click bInput,0 立即提交。其他变量按各自倍率换算。')
h('分类索引',3)
table(['章节','类别','章节','类别'],[
('2','总览测量数据','3','总览运行状态'),('4','温段方案设置','5','12 段目标温度'),('6','12 段保温时间','7','温度曲线数据'),('8','功率曲线数据','9','频率曲线数据'),('10','相位曲线数据','11','保护阈值'),('12','报警输入','13','曲线采样与缓存'),('14','按钮请求','15','六条报警记录读取')],(1.2,7.5,1.2,7.5))
p('设置值和实际反馈相互独立：tg1～tg12 是方案目标，target10 是当前目标反馈；plim 是功率上限，powerW 是当前功率。保护检测、加热执行和故障恢复判断由 STM32 完成。')
p('本手册给出当前工程的调试接口。已做离线校验，仍需在官方编辑器编译、模拟器和实屏验证。')

page('总览测量数据',['page 0'])
rows=[]
for label,var,value,unit,scale,obj,display in [
('实际温度','temp10',1196,'℃',10,'tActual','119.6'),('目标温度','target10',1200,'℃',10,'tTarget','120.0'),('电功率','powerW',860,'W',1,'tPower','860'),('母线电压','bus10',3080,'V',10,'tBus','308.0'),('工作频率','freq100',2010,'kHz',100,'tFreq','20.10'),('谐振电流','amp10',182,'A',10,'tCurrent','18.2')]:
    rows.append(row(label,var,f'单位 {unit}；倍率 {scale}',measured(var,value),f'显示 {display} {unit}。发送整数 = 实际值 × {scale}。',{obj+'.txt':display}))
fields(rows)
p('这六项只改变读数，不会自动增加曲线点。添加曲线点见第 13 节。频率单位是 kHz：20.10 kHz 要发 2010，不是 20100。')

page('总览运行状态',['page 0','nseg=3'])
fields([
row('读数有效标志','valid','0 无效；1 有效',status('valid',0),'六项测量读数显示 --；改为 1 后恢复显示已存的数值。',{'tActual.txt':'--'}),
row('运行反馈','run','0 待机；1 运行',status('run',1),'顶部显示运行中；这是主控反馈，不会真的启动加热。',{'tState.txt':'运行中'}),
row('当前温段','stage','0 未开始；1～nseg',status('stage',2),'当前温段显示 2 / 3；总段数来自 nseg。',{'tStage.txt':'2 / 3'}),
row('当前阶段','phaseState','0 待机；1 升温\n2 保温；3 已完成',status('phaseState',2),'显示保温。温段和阶段由主控更新，屏幕不自动切换。',{'tPhase.txt':'保温'}),
row('锁相反馈','locked','0 未锁定；1 已锁定',status('locked',1),'显示已锁定；频率页和相位页使用同一个变量。',{'tLock.txt':'已锁定'}),
row('累计运行时间','totalSec','单位 s；整数秒',status('totalSec',123),'显示 123 s；屏幕不会自动累加。',{'tElapsed.txt':'123 s'}),
row('预计剩余时间','remain','单位 s；整数秒',status('remain',456),'显示 456 s；剩余时间由主控计算后发送。',{'tRemain.txt':'456 s'}),
row('提交读数刷新','dirty','写 1 后处理程序自动清零','valid=1\nbus10=3080\ndirty=1\nclick bInput,0','把已赋值的母线电压刷新为 308.0 V；处理后 dirty 回到 0。',{'tBus.txt':'308.0','dirty':0})])
p('dirty 是刷新标志：设置 dirty=1 后，发送 click bInput,0 立即更新。每页还有 200 ms 处理定时器，但逐组发送数据时仍建议立即提交。')

page('温段方案设置',['page 1','run=0'], '最多 12 段，每页 6 段。所有设置示例均在停止状态执行。当前页是分段温控时，click bTable,0 更新表格和程序预览。')
fields([
row('温段数量','nseg','1～12', 'nseg=12\nsel=1\nstagepage=0\nclick bTable,0','方案改成 12 段，当前显示 1～6 段。新增段已有默认值，可再逐项设置。',{'nseg':12,'bRow5.txt':'6'}),
row('控制算法','mode','0 LADRC；1 PID\n2 模糊LADRC',setting('mode',2,1),'下拉框显示模糊LADRC。选择算法只改变参数，算法在 STM32 中执行。',{'bMode.txt':'模糊LADRC'}),
row('功率上限','plim','单位 W；1～9999',setting('plim',1500,1),'功率上限显示 1500 W，不会改变当前功率反馈 powerW。',{'bplim.txt':'1500'}),
row('选中温段','sel','1～nseg','sel=2\nstagepage=0\nclick bTable,0','选中第 2 段；插入会加在它后面，删除会删除它。',{'sel':2}),
row('温段页号','stagepage','0 第一页；1 第二页','nseg=12\nsel=7\nstagepage=1\nclick bTable,0','显示 7～12 段，选中第 7 段。只设置页号不会增加温段数量。',{'bRow11.txt':'12'})])
p('指令改参数时不会自动检查所有配置关系，请按范围赋值。按钮请求“应用参数”和“保存方案”见第 14 节；当前版本没有掉电保存。')

page('十二段目标温度',['page 1','run=0','nseg=12','sel=1','stagepage=0','click bTable,0'],'全部为整数 ℃，范围 1～999，不乘 10。修改第 7～12 段后，用“下一页”查看。每条示例只修改对应温段。')
fields([row(f'第 {i} 段目标温度',f'tg{i}','整数 ℃；1～999',setting(f'tg{i}',50+i*5,1),f'第 {i} 段目标温度改为 {50+i*5}℃；程序预览同步更新。',{f'tg{i}':50+i*5}) for i in range(1,13)])
p('tg1～tg12 是设定温度；运行时的当前目标显示要由主控另发 target10，例如 120℃ 对应 target10=1200。')

page('十二段保温时间',['page 1','run=0','nseg=12','sel=1','stagepage=0','click bTable,0'],'全部为整数秒，范围 1～9999。修改第 7～12 段后，用“下一页”查看。界面没有升温时间参数。')
fields([row(f'第 {i} 段保温时间',f'hd{i}','整数 s；1～9999',setting(f'hd{i}',i*60,1),f'第 {i} 段保温 {i*60} s（{i} 分钟）；程序预览总时长同步改变。',{f'hd{i}':i*60}) for i in range(1,13)])
p('hd1～hd12 是方案设置，不会自动改变 totalSec、remain 或 stage。实际计时、达到温度后开始保温等逻辑由 STM32 实现。')

page('温度曲线数据',['page 2','tfrom=0','tto=60'])
fields([
row('当前目标温度','target10','℃ × 10',measured('target10',1200),'右侧目标温度显示 120.0℃；也是目标曲线采样来源。',{'tTarget.txt':'120.0'}),
row('区域平均温度','mean10','℃ × 10',measured('mean10',1196),'右侧平均温度显示 119.6℃；也是平均曲线采样来源。',{'tMean.txt':'119.6'}),
row('区域最高温度','high10','℃ × 10',measured('high10',1248),'右侧最高温度显示 124.8℃；也是最高曲线采样来源。',{'tHigh.txt':'124.8'}),
row('区域最低温度','low10','℃ × 10',measured('low10',1142),'右侧最低温度显示 114.2℃；当前没有最低温度曲线。',{'tLow.txt':'114.2'}),
row('时间起点','tfrom','整数 s；0～86399',status('tfrom',10),'时间范围改成 10 s 至 60 s，只重画已有缓存。',{'tfrom':10}),
row('时间终点','tto','整数 s；1～86400',status('tto',120),'终点改成 120 s；起点保持当前值，且必须小于终点。',{'tto':120})])
p('上述读数赋值不会自动产生曲线。按第 13 节发送 sampleSec 和 push，至少输入两个递增时间点才会出现线段。温度曲线纵轴固定 0～150℃。')

page('功率曲线数据',['page 3','run=0','pfrom=0','pto=60'])
fields([
row('当前电功率','powerW','单位 W；不缩放',measured('powerW',860),'显示 860 W；也是功率曲线采样来源。',{'tPower.txt':'860'}),
row('调功移相角','angle10','角度 × 10',measured('angle10',620),'调功移相角显示 62.0；与锁相相位差 phase10 是不同变量。',{'tAngle.txt':'62.0'}),
row('功率上限','plim','单位 W；1～9999',setting('plim',1800,3),'功率上限输入框显示 1800 W；page 3 单独执行以更新输入框。',{'bplim.txt':'1800'}),
row('时间起点','pfrom','整数 s；0～86399',status('pfrom',10),'起点改为 10 s，终点保留 60 s。',{'pfrom':10}),
row('时间终点','pto','整数 s；1～86400',status('pto',120),'终点改为 120 s；必须大于当前起点。',{'pto':120})])
p('纵轴固定 0～1500 W，超过范围的点画在边缘。修改 plim 不会改变纵轴刻度。当前功率始终由主控发送。')

page('频率曲线数据',['page 4','run=0','flo=15','fhi=25','ffrom=0','fto=60'])
fields([
row('当前工作频率','freq100','kHz × 100',measured('freq100',2010),'显示 20.10 kHz；也是频率曲线采样来源。',{'tFreq.txt':'20.10'}),
row('锁相状态','locked','0 未锁定；1 已锁定',status('locked',1),'频率下方显示已锁定，不改变频率数值。',{'tLocked.txt':'已锁定'}),
row('搜频下限','flo','整数 kHz；1～99',setting('flo',16,4),'下限输入框显示 16 kHz；必须小于 fhi。',{'bflo.txt':'16'}),
row('搜频上限','fhi','整数 kHz；1～99',setting('fhi',24,4),'上限输入框显示 24 kHz；必须大于 flo。',{'bfhi.txt':'24'}),
row('时间起点','ffrom','整数 s；0～86399',status('ffrom',10),'起点改为 10 s，终点保留 60 s。',{'ffrom':10}),
row('时间终点','fto','整数 s；1～86400',status('fto',120),'终点改为 120 s；必须大于当前起点。',{'fto':120})])
p('纵轴固定 15～25 kHz。flo / fhi 不乘 100；只有 freq100 乘 100。点击重新搜频只产生 cmd=5，请求主控执行搜频。')

page('相位曲线数据',['page 6','afrom=0','ato=60'])
fields([
row('当前锁相相位差','phase10','角度 × 10；支持负数',measured('phase10',-15),'显示 -1.5°；也是相位差曲线采样来源。',{'tAngle.txt':'-1.5'}),
row('锁相状态','locked','0 未锁定；1 已锁定',status('locked',1),'显示已锁定，与总览和频率页共用此变量。',{'tLocked.txt':'已锁定'}),
row('时间起点','afrom','整数 s；0～86399',status('afrom',10),'起点改为 10 s，终点保留 60 s。',{'afrom':10}),
row('时间终点','ato','整数 s；1～86400',status('ato',120),'终点改为 120 s；必须大于当前起点。',{'ato':120})])
p('相位差纵轴固定 -30～30°。发送 phase10=-15 是 -1.5°；发送 phase10=-150 才是 -15.0°。不要与功率页的调功移相角 angle10 混用。')

page('保护阈值',['page 5','run=0'],'以下都是保护设置值，不是测量值。全部为整数，不乘 10。每项示例中的 page 5 单独执行，用来重画输入框。')
fields([row(label,var,f'整数 {unit}；{limits}',setting(var,value,5),f'{label}输入框显示 {value} {unit}。'+extra,{f'b{var}.txt':str(value)}) for label,var,value,unit,limits,extra in [
('母线过压阈值','ov',360,'V','1～9999','必须大于 uv。'),('母线欠压阈值','uv',200,'V','1～9999','必须小于 ov。'),('谐振过流阈值','oc',30,'A','1～9999','对应电流保护门限。'),('功率管过温阈值','ot1',85,'℃','1～999','对应功率管温度保护。'),('整流桥过温阈值','ot2',90,'℃','1～999','对应整流桥温度保护。'),('工件超温阈值','ot3',150,'℃','1～999','对应工件温度保护。')]])
p('要测试“保存参数”，请先把六个阈值都设置为有效正数，且 uv < ov，再点击保存。屏幕只保存内存中的参数并产生 cmd=4；主控负责接收、校验、存储和执行保护。')
p('例如 bus10=3700 表示实测母线电压 370.0 V，ov=360 表示过压门限 360 V。即使实测值高于门限，当前 HMI 也不会自行生成报警；主控检测后按第 12 节输入报警。')

def alarm(code,sec,on=1):return f'alarmIn={code}\nalarmOn={on}\nalarmSec={sec}\nalarmPush=1\nclick bInput,0'
page('报警输入',['page 5'],'输入报警时四个变量配合使用。下面每个示例均给出完整的一组指令，不是只改一个变量就会自动报警。示例按需单独执行。')
fields([
row('报警内容代码','alarmIn','1～8，含义见下表',alarm(2,12),'触发锁相失败；记录时间 12 s，状态未确认。',{'tAlarm.txt':'锁相失败','a00.txt':'12 s'}),
row('触发或恢复','alarmOn','1 触发；0 恢复',alarm(2,12)+'\nalarmIn=2\nalarmOn=0\nalarmPush=1\nclick bInput,0','前五条先触发锁相失败；后四条发送 alarmOn=0 表示恢复。执行完当前报警变为无，记录改为已恢复。',{'alarmCode':0,'a02.txt':'已恢复'}),
row('发生时的运行秒数','alarmSec','单位 s；非负整数',alarm(3,123),'新增母线过压，运行时间列显示 123 s。它不使用电脑时钟。',{'a00.txt':'123 s'}),
row('提交报警事件','alarmPush','写 1 后自动清零',alarm(5,30),'提交谐振过流报警。只改 alarmIn / alarmOn 不提交时，记录不会改变。',{'tAlarm.txt':'谐振过流','alarmPush':0}),
row('当前报警代码','alarmCode','只读反馈；0 表示无','get alarmCode','读取当前代码；返回值按下表解释。不要直接写它代替完整报警事件。')])
table(['代码','报警内容','代码','报警内容'],[('1','测温数据中断','2','锁相失败'),('3','母线过压','4','母线欠压'),('5','谐振过流','6','功率管过温'),('7','整流桥过温','8','工件超温')],(1.2,7.5,1.2,7.5))
p('记录显示最近 6 条，新记录在顶部，超出后移出最旧一条；跨页保留，重启清空。连续重复触发同一个当前代码不会新增行，恢复后再次触发会新增行。')
p('确认报警只把未确认改成已确认；故障复位只产生 cmd=6。主控确认故障解除后才发 alarmOn=0。顶部当前报警仍为单个代码，不代表可同时维护多个活动故障。')

page('曲线采样与缓存',['page 2','frozen=0','tfrom=0','tto=60','clearPlot=1','click bInput,0'],'每次 push 保存同一时间点的 target10、mean10、high10、powerW、freq100、phase10。请先更新需要的通道，再提交。以下控制项和查询各有独立示例。本节准备会清空已有曲线。')
fields([
row('采样时间戳','sampleSec','整数 s；0～86400','sampleSec=0','只设置下一点的横坐标，不会画线。后续已接收时间戳必须严格递增。',{'sampleSec':0}),
row('提交一个采样点','push','写 1 后自动清零','sampleSec=1\nmean10=600\npush=1\nclick bInput,0','保存 1 s、平均 60.0℃ 等通道的快照。再次输入须增大 sampleSec。',{'nBuf':1,'push':0}),
row('清空曲线缓存','clearPlot','写 1 后自动清零','clearPlot=1\nclick bInput,0','曲线清空，nBuf 和 lastSec 归零。测量读数不会清空。',{'nBuf':0,'lastSec':0}),
row('冻结曲线重画','frozen','0 实时；1 冻结','frozen=1','冻结时仍接收和保存采样。恢复时发送 frozen=0、dirty=1、click bInput,0 三条命令。',{'frozen':1}),
row('已缓存点数','nBuf','只读；0～32','get nBuf','返回当前点数。第 33 点进入时会移出最旧点，最多保留 32 点。'),
row('最后接收的时间戳','lastSec','只读；单位 s','get lastSec','返回最后一个成功保存的 sampleSec；清空后为 0。'),
row('采样输入错误','dataErr','只读；0 正常；1 拒绝','get dataErr','时间超范围、相同或倒退时为 1；每次新的 push 会重新判断。')])
p('最小画线步骤：清空缓存 → sampleSec=0、mean10=600、push=1、提交 → sampleSec=10、mean10=800、push=1、提交。将温度页时间范围设为 0～60 s，可看到平均温度从 60℃ 到 80℃ 的线段。')
p('valid=0 后提交 dirty 可让读数显示 --。32 点缓存与时间范围是两回事：增大时间范围不会补出未保存的数据。')

page('按钮请求',['page 0','run=0'],'按钮把请求号写入 cmd；STM32 用 get cmd 查询并处理。下面的 click 用来模拟点击，只产生请求，不代替主控执行。每组示例的 page 命令单独执行。')
fields([
row('启动加热请求','cmd','1','page 0\nclick bStart,0\nget cmd','返回 1；主控处理后更新 run 等反馈。',{'cmd':1}),
row('停止加热请求','cmd','2','page 0\nclick bStop,0\nget cmd','返回 2；主控执行停止并更新反馈。',{'cmd':2}),
row('应用参数请求','cmd','3','page 1\nclick bApply,0\nget cmd','返回 3；读取 mode、plim、nseg、tg1～tg12、hd1～hd12 等需要的参数。',{'cmd':3}),
row('保存方案请求','cmd','4','page 1\nclick bSave,0\nget cmd','返回 4；由主控实现掉电保存。',{'cmd':4}),
row('重新搜频请求','cmd','5','page 4\nclick bLock,0\nget cmd','返回 5；主控读取 flo / fhi 后搜频。',{'cmd':5}),
row('故障复位请求','cmd','6','page 5\nclick bReset,0\nget cmd','返回 6；不会自动清除故障。',{'cmd':6}),
row('确认报警请求','cmd','7','page 5\nclick bAck,0\nget cmd','返回 7；未确认记录会改为已确认，活动报警不被清除。',{'cmd':7}),
row('清理已处理请求','cmd','0','cmd=0\nget cmd','返回 0。确认请求处理完再清理；正式协议需防止覆盖新请求。',{'cmd':0})])
p('get 数值返回 8 字节：71 + 四字节小端整数 + FF FF FF。例如 71 02 00 00 00 FF FF FF 表示 2。同一时刻只保留一个待返回查询，因为响应不包含变量名。cmd 是单个槽位，连续点击可能覆盖前一个请求。')

page('六条报警记录读取',['page 5'],'这些变量是事件生成的历史记录，通常只需要读取，不要直接改写。第 1 行最新，第 6 行最旧；空行的代码为 0。每个变量都可单独用 get 查询。')
rows=[]
for i in range(6):
    rows += [row(f'第 {i+1} 行报警代码',f'ac{i}','只读',f'get ac{i}','返回 0 表示空行；1～8 对照第 12 节报警代码表。'),row(f'第 {i+1} 行发生时间',f'at{i}','只读；整数 s',f'get at{i}','返回该记录的运行秒数，例如 123 表示 123 s。'),row(f'第 {i+1} 行记录状态',f'as{i}','只读',f'get as{i}','0 空行；1 未确认；2 已确认；3 已恢复。')]
fields(rows)
p('例如触发 alarmIn=3、alarmOn=1、alarmSec=123 并提交后，get ac0 返回 3，get at0 返回 123，get as0 返回 1。后续新报警到来会使这条记录向下移动。')


OUT.write_text('\n'.join(lines),'utf8')
(ROOT/'手册示例.json').write_text(json.dumps(examples,ensure_ascii=False,indent=2),'utf8')
print(OUT)
