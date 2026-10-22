"""Reorganize the existing Markdown manual around complete user operations."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parent
path=ROOT/'指令调试说明.md'
old=path.read_text('utf8')
reference=old[old.index('## 2 总览测量数据'):]
reference=re.sub(r'^### ', '#### ',reference,flags=re.M)
reference=re.sub(r'^## (\d+) (.+)$',r'### 附录 A\1 \2',reference,flags=re.M)
reference=re.sub(r'第 (\d+) 节',r'附录 A\1',reference)
parts=[];cases=[]
def p(s):parts.extend([s,''])
def block(lines,lang='text'):p('```'+lang+'\n'+'\n'.join(lines)+'\n```')
def case(key,title,description,commands,result,expect=None,hexdata=False):
    p('## '+title);p(description);p('依次发送：');block(commands)
    p('**预期结果：**'+result)
    if hexdata:
        p('如果使用通用串口助手的 **HEX 发送模式**，下面每行是一条完整发送帧，已带结束字节。按行逐条发送，不要再追加换行或结束字节。')
        block([(s.encode('ascii')+b'\xff\xff\xff').hex(' ').upper() for s in commands],'hex')
    cases.append(dict(id=key,commands=commands,expect=expect or {},hex=hexdata))

p('# 感应加热 HMI 操作与调试手册')
p('适用工程：`加热1_调试版_V7.HMI`。这份文件按“我现在想做什么”组织。先找到操作，按顺序发送整组指令，再看屏幕是否出现预期结果。变量的详细说明放在文末附录；后续继续修改本文件。')
p('## 先选对发送方式')
p('**USART HMI 模拟器输入区：**选择“指令发送到 当前模拟器”和 String 模式。使用下面的文本指令，一行一条；`page` 切页命令先单独执行，再发送后面的指令。这里不需要手工输入 `FF FF FF`。')
p('**串口调试助手连接实际屏幕：**工程初始波特率为 9600，串口参数与屏幕一致。每条命令是 ASCII 文本，末尾追加三个二进制 `0xFF`，不追加回车换行。普通助手不会把文本 `FF FF FF` 或 `\\xFF` 自动变成结束字节。')
p('如果助手支持“文本发送 + 自定义 HEX 帧尾”，帧尾设置为 `FF FF FF`，每次只发一条文本命令。如果不支持这种混合发送，就选 HEX 模式，使用示例中的完整十六进制帧。多行代码不是一个字符串命令，也不是仅在最后一行追加一次结束符。')
p('下面“触发母线过压”和“画温度曲线”同时提供文本及完整 HEX 指令。其余示例使用相同规则：逐条 ASCII 编码，再追加 `FF FF FF`。通用串口助手发送到的是它连接的串口；要调试电脑里的模拟器，请使用模拟器输入区或已配置好的串口调试连接。')
p('### 按目的查找')
p('1. 显示母线过压报警\n2. 确认报警\n3. 解除母线过压报警\n4. 触发其他报警并检查六行记录\n5. 画温度波形\n6. 画功率波形\n7. 画频率波形\n8. 画相位波形\n9. 改变曲线时间范围\n10. 冻结或恢复曲线\n11. 清空曲线\n12. 更新母线电压或其他测量值\n13. 显示运行状态和当前温段\n14. 设置加热方案与控制算法\n15. 设置保护参数\n16. 读取按钮请求和参数\n17. 指令发出后没有反应时检查什么')

case('alarm_trigger','1 我想让屏幕显示母线过压报警',
     '下面这组表示：在运行第 123 秒发生母线过压。报警代码固定是 `3`，触发状态是 `1`。只发送 `ov=360` 或 `bus10=3700` 不会生成报警，它们分别是阈值和测量值。',
     ['page 5','alarmIn=3','alarmOn=1','alarmSec=123','alarmPush=1','click bInput,0'],
     '顶部显示“母线过压”，最新一行显示“123 s / 母线过压 / 未确认”。若同一个母线过压已经是当前报警，重复发送不会新增一行；解除后再触发才会新增。',
     {'tAlarm.txt':'母线过压','a00.txt':'123 s','a01.txt':'母线过压','a02.txt':'未确认'},True)
p('这组指令各行的作用：`page 5` 打开报警页；`alarmIn=3` 选择母线过压；`alarmOn=1` 表示故障发生；`alarmSec=123` 指定记录时间；`alarmPush=1` 标记一条新事件；`click bInput,0` 立即处理事件。')

case('alarm_ack','2 我想确认这条报警',
     '先完成第 1 项触发报警，再发送下面两条。相当于点击屏幕上的“确认报警”。',
     ['page 5','click bAck,0','click bInput,0'],
     '未确认记录变为“已确认”；顶部仍显示“母线过压”。确认不等于故障解除，同时会产生按钮请求 `cmd=7`。',{'cmd':7})
case('alarm_restore','3 故障已经解除 我想让母线过压恢复正常',
     '先触发第 1 项，再发送这组。与触发时最关键的区别是 `alarmOn=0`。',
     ['page 5','alarmIn=3','alarmOn=0','alarmPush=1','click bInput,0'],
     '如果当前报警是母线过压，顶部变为“无”；历史中的母线过压记录改为“已恢复”，记录本身不删除。',{'alarmCode':0})
p('“故障复位”按钮只给主控发出 `cmd=6` 请求，不会直接清除报警。STM32 确认故障条件已经解除后，再发本组恢复指令。')

p('## 4 我想触发其他报警 或检查六行记录')
p('使用第 1 项完整模板，只修改 `alarmIn` 和 `alarmSec`。每组都要包含 `alarmOn=1`、`alarmPush=1` 和 `click bInput,0`。')
p('| 想显示的报警 | 使用的命令 |\n| --- | --- |\n| 测温数据中断 | `alarmIn=1` |\n| 锁相失败 | `alarmIn=2` |\n| 母线过压 | `alarmIn=3` |\n| 母线欠压 | `alarmIn=4` |\n| 谐振过流 | `alarmIn=5` |\n| 功率管过温 | `alarmIn=6` |\n| 整流桥过温 | `alarmIn=7` |\n| 工件超温 | `alarmIn=8` |')
p('例如我要显示“谐振过流”，直接依次发送：')
other=['page 5','alarmIn=5','alarmOn=1','alarmSec=200','alarmPush=1','click bInput,0'];block(other)
cases.append(dict(id='other_alarm',commands=other,expect={'tAlarm.txt':'谐振过流','a00.txt':'200 s'},hex=False))
p('要测试六行记录，可按上表依次触发 1～8 号报警，每次改变运行时间并完整提交。最新记录在最上方；超过六条会移出最旧一条。正式使用中，故障解除后发送同码 `alarmOn=0`。顶部当前报警只有一个代码，六行是历史记录，不表示六个故障同时处于活动状态。')

def curve_commands(pid,prefix,points):
    out=[f'page {pid}','frozen=0',f'{prefix}from=0',f'{prefix}to=20','clearPlot=1','click bInput,0']
    for sec,values in points:
        out += [f'sampleSec={sec}']+[f'{var}={val}' for var,val in values.items()]+['push=1','click bInput,0']
    return out
temp=curve_commands(2,'t',[(0,{'target10':600,'mean10':550,'high10':580}), (10,{'target10':900,'mean10':850,'high10':920}), (20,{'target10':1200,'mean10':1196,'high10':1248})])
case('temperature_curve','5 我想画出温度波形',
     '下面是一组完整的三点测试：0 秒、10 秒、20 秒。执行后画出目标、平均、最高三条温度曲线。开头会清空共享曲线缓存，包括其他曲线页的历史；这些测试数据只有你发送后才会进入工程。',temp,
     '温度页横轴为 0～20 s，三条曲线各有三个点、两段连线。20 秒处目标为 120.0℃、平均为 119.6℃、最高为 124.8℃。',{'nBuf':3,'lastSec':20,'tMean.txt':'119.6','dataErr':0},True)
p('不需要真的等待 10 秒再发送下一组：`sampleSec` 决定横轴位置。手工测试可依次发送；实时运行时，应填主控实际采样秒数。每组最后的 `push=1` 和 `click bInput,0` 不能省略。只发温度数值或 `dirty=1` 会改读数，不会增加曲线点。')
p('继续追加第 4 点，例如把时间范围扩大到 30 s 后，发送：')
append=['tto=30','sampleSec=30','target10=1200','mean10=1205','high10=1250','push=1','click bInput,0'];block(append)
cases.append(dict(id='temperature_append',commands=temp+append,expect={'nBuf':4,'lastSec':30},hex=False,combined=True))

power=curve_commands(3,'p',[(0,{'powerW':1200}),(10,{'powerW':1000}),(20,{'powerW':860})])
case('power_curve','6 我想画出功率波形','这是独立完整示例，会先清空共享曲线缓存。功率按 W 发送，不乘 10。',power,
     '功率曲线经过 0 s / 1200 W、10 s / 1000 W、20 s / 860 W，右侧当前功率显示 860 W。',{'nBuf':3,'tPower.txt':'860','pw2':860})
freq=curve_commands(4,'f',[(0,{'freq100':2100}),(10,{'freq100':2050}),(20,{'freq100':2010})])
case('frequency_curve','7 我想画出频率波形','这是独立完整示例，会先清空共享曲线缓存。频率发送值是实际 kHz × 100。',freq,
     '频率依次为 21.00、20.50、20.10 kHz，右侧显示 20.10 kHz。',{'nBuf':3,'tFreq.txt':'20.10','fq2':2010})
phase=curve_commands(6,'a',[(0,{'phase10':100}),(10,{'phase10':40}),(20,{'phase10':-15})])
case('phase_curve','8 我想画出相位波形','这是独立完整示例，会先清空共享曲线缓存。相位发送值是实际角度 × 10，允许负数。',phase,
     '相位依次为 10.0°、4.0°、-1.5°，曲线最后一个点在零线下方，右侧显示 -1.5°。',{'nBuf':3,'tAngle.txt':'-1.5','ph2':-15})
p('### 我想四个曲线页同时都有数据')
p('四类曲线共用每次采样快照。实际 STM32 程序应每个采样周期把六个通道全部写完，再提交一次，而不是重复执行上面四个会清空缓存的独立测试。下面是一组数据，可在任意正常页面提交；后续采样要增大 sampleSec。')
allchannels=['sampleSec=40','target10=1200','mean10=1196','high10=1248','powerW=860','freq100=2010','phase10=-15','push=1','click bInput,0'];block(allchannels)
p('先把各曲线的时间终点设到 40 s 或更大，才能看到这个点。右侧最低温度 `low10`、总览实际温度 `temp10`、调功移相角 `angle10` 需要另行赋值；它们不从以上六个通道自动计算。')

case('time_window','9 我想只查看 5 s 到 15 s 的温度曲线','先用第 5 项产生数据，再执行：',
     ['page 2','tfrom=5','tto=15','dirty=1','click bInput,0'],
     '横轴改为 5～15 s，只绘制缓存中这一时间范围的曲线部分。不会补出未采集的数据。',{'tfrom':5,'tto':15})
p('| 曲线 | 先进入页面 | 起点变量 | 终点变量 |\n| --- | --- | --- | --- |\n| 温度 | `page 2` | `tfrom` | `tto` |\n| 功率 | `page 3` | `pfrom` | `pto` |\n| 频率 | `page 4` | `ffrom` | `fto` |\n| 相位 | `page 6` | `afrom` | `ato` |')
p('其他曲线照同样方法替换页号和起止变量，最后仍发 `dirty=1`、`click bInput,0`。起止值均为整数秒，范围 0～86400，且起点必须小于终点。')

p('## 10 我想冻结或恢复曲线')
p('停留在曲线页，冻结只发这一条：');block(['frozen=1'])
p('恢复时依次发送：');block(['frozen=0','dirty=1','click bInput,0'])
p('冻结只暂停曲线重画，采样仍进入缓存。切换页面会按当前缓存重新初始化画面。')
case('clear_curve','11 我想清空曲线重新测试','在任意正常页面都可清空共享缓存。下面以温度页为例。',
     ['page 2','clearPlot=1','click bInput,0'],
     '四类曲线的历史点被清空，nBuf 和 lastSec 变为 0，可以重新从 sampleSec=0 输入。右侧已有读数不会清空。',{'nBuf':0,'lastSec':0})

case('bus_reading','12 我想让总览显示母线电压 308.0 V','直接依次发送：',
     ['page 0','bus10=3080','valid=1','dirty=1','click bInput,0'],
     '总览母线电压显示 308.0 V。这只更新读数，不会自动触发母线过压。',{'tBus.txt':'308.0'})
p('其他测量值沿用同样的发送顺序，把赋值行替换成下面对应的一行；保留 `valid=1`、`dirty=1`、`click bInput,0`。')
p('| 我想显示 | 替换成这条赋值指令 |\n| --- | --- |\n| 实际温度 119.6℃ | `temp10=1196` |\n| 当前目标 120.0℃ | `target10=1200` |\n| 电功率 860 W | `powerW=860` |\n| 工作频率 20.10 kHz | `freq100=2010` |\n| 谐振电流 18.2 A | `amp10=182` |')

case('running','13 我想显示运行中 第 2 段 已运行 123 s','运行反馈由 STM32 提供；下面只模拟反馈显示。',
     ['page 0','nseg=3','run=1','stage=2','phaseState=2','locked=1','totalSec=123','remain=456','dirty=1','click bInput,0'],
     '运行中、当前温段 2 / 3、保温、已锁定、已运行 123 s、预计剩余 456 s。屏幕不会自行递增时间或切段。',{'tStage.txt':'2 / 3','tPhase.txt':'保温','tElapsed.txt':'123 s'})

case('programme','14 我想设置两段加热并选择 PID','第一段 80℃ 保温 60 s；第二段 120℃ 保温 180 s；功率上限 1500 W。',
     ['page 1','run=0','nseg=2','sel=1','stagepage=0','mode=1','plim=1500','tg1=80','hd1=60','tg2=120','hd2=180','click bTable,0'],
     '表格显示两段，算法显示 PID，程序预览同步更新。这些是设置值，不会自行开始加热。',{'nseg':2,'bMode.txt':'PID','btg2.txt':'120','bhd2.txt':'180'})
p('LADRC 用 `mode=0`，模糊LADRC 用 `mode=2`。最多 12 段：`tg1`～`tg12` 为整数℃，`hd1`～`hd12` 为整数秒。`click bTable,0` 仅在分段温控页使用。')
case('protection','15 我想设置保护门限','把六项参数写完后，单独执行最后的 page 5 重画输入框。',
     ['run=0','ov=360','uv=200','oc=30','ot1=85','ot2=90','ot3=150','page 5'],
     '显示过压 360 V、欠压 200 V、过流 30 A、功率管过温 85℃、整流桥过温 90℃、工件超温 150℃。',{'bov.txt':'360','buv.txt':'200','bot3.txt':'150'})
p('设置阈值本身不会执行保护或产生报警。STM32 比较实测值和阈值，确认故障后再发送第 1～4 项的报警事件。')

case('button_read','16 我想知道屏幕是否点击了启动按钮','下面先用指令模拟点击，再查询请求；接入 STM32 后直接周期查询 get cmd 即可。',
     ['page 0','run=0','dirty=1','click bInput,0','click bStart,0','get cmd'],
     '返回请求号 1，串口数值帧为 71 01 00 00 00 FF FF FF。主控执行后更新反馈，处理完成再清理请求。',{'cmd':1})
p('清理请求发送 `cmd=0`。其他编号：2 停止、3 应用参数、4 保存、5 搜频、6 故障复位、7 确认报警。`cmd` 是单个槽位，正式通信需要防止连续点击覆盖请求及清理旧请求时误清新请求。')
p('读取配置同样用 get，例如查询母线过压阈值：');block(['get ov'])
p('如果 ov=360，数值回包为 `71 68 01 00 00 FF FF FF`。四字节数值按小端排列。每次只等待一个查询返回，返回帧没有变量名。')

p('## 17 我发了指令 但没有出现预期结果')
p('| 现象 | 先检查什么 |\n| --- | --- |\n| 串口助手发送后完全无反应 | 是否选对串口、波特率；每条命令是否有二进制 FF FF FF；是否误把文本 FF FF FF 或换行当成帧尾。 |\n| 读数仍显示 -- | 是否发送 valid=1、dirty=1、click bInput,0。 |\n| 温度变了但没有线 | 数值更新不等于采样；还要 sampleSec、push=1、click bInput,0，至少两个递增时间点。 |\n| 重复测试后点不增加 | 时间必须递增；重新从 0 开始前先 clearPlot=1 并提交。 |\n| 有点但当前范围看不到 | 检查起止时间是否包含这些 sampleSec，以及 frozen 是否为 0。 |\n| 突然少了其他曲线的数据 | 四个曲线共用缓存；clearPlot 会一起清空。 |\n| 只保留最近一小段曲线 | 当前最多 32 点；第 33 点会移出最旧点，扩大时间窗不会增加历史容量。 |\n| 报警重复发送却不增加行 | 相同的当前报警不重复入表，恢复后再次触发才新增。 |')
p('可在曲线测试后依次查询以下变量，每条查询等返回后再发下一条：');block(['get nBuf','get lastSec','get dataErr'])
p('第 5 项三点测试后应分别得到 3、20、0。dataErr=1 表示最近一次采样因时间超范围或未严格递增被拒绝。')
p('这些步骤已使用工程的离线事件解释器核对，真实串口的发送节奏、回包及屏幕效果仍需在官方模拟器或实屏验证。')
p('## 变量参考附录')
p('需要查单个变量时再看这里。前面的操作示例优先用于直接调试，以下保留完整变量、倍率及独立示例。')
path.write_text('\n'.join(parts)+reference,'utf8')
(ROOT/'操作示例.json').write_text(json.dumps(cases,ensure_ascii=False,indent=2),'utf8')
print(path)
