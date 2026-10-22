"""Migrate the current manual in place; preserve all data/alarm/wave examples."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parent
path=root/'指令调试说明.md'
md=(root/'指令调试说明.md.before_v8.bak').read_text('utf8').replace('加热1_调试版_V7.HMI','加热1_调试版_V8.HMI')
mapping=json.loads((root/'串口事件映射.json').read_text('utf8'))
spec=json.loads((root/'界面定义.json').read_text('utf8'))
names={p['id']:p['name'] for p in spec['pages']}
buttons=mapping['buttons']
def replace_section(start,end,body):
    global md
    a=md.index(start);b=md.index(end,a);md=md[:a]+body.strip()+'\n\n'+md[b:]
def hexbytes(data):return bytes(data).hex(' ').upper()
section1='''## 一 V8 现在怎样双向通信

**V8 在按钮松开时主动发送数据，STM32 不必轮询 cmd 才能发现请求。** 模拟器下方“模拟器返回数据”就是这个输出方向；请选择 **H（十六进制）** 查看。S 是字符串显示，二进制帧在 S 模式下可能显示乱码或不可见字符。新版需要打开并重新编译 `加热1_调试版_V8.HMI` 再启动模拟器，旧模拟器不会自动更新。

一次操作可能收到两帧：先是“哪个按钮松开了”的 65 触摸帧；有效业务操作随后发送 A5 5A 01 请求帧。**STM32 以业务帧执行启动/停止等操作，不能收到 65 后执行一次、收到业务帧又执行一次。** 数字键盘确认合法参数后则发送 A5 5A 02，携带新值。

| 方向/帧类型 | 完整格式（HEX） | 长度 | 解释 |
| --- | --- | --- | --- |
| STM32 → HMI 指令 | ASCII 文本 + FF FF FF | 不固定 | 如 bus10=3080，每条命令独立加三个二进制 FF，不发送字符串结束符或额外回车换行。 |
| HMI → STM32 触摸松开 | 65 PP CC 00 FF FF FF | 7 字节 | PP=源页面，CC=控件 ID，00=松开。所有可见按钮及输入框均上报，表示触摸事件发生，不保证业务条件通过。 |
| HMI → STM32 业务请求 | A5 5A 01 PP QQ FF FF FF | 8 字节 | PP=源页面，QQ=请求号1～7。仅接受操作后发送。 |
| HMI → STM32 数字确认 | A5 5A 02 PP II VV VV VV VV FF FF FF | 12 字节 | PP=输入框所属原页面，II=字段编号，VV=确认值，4字节小端。不是键盘页7；只有校验通过才发送。 |
| get 数值的查询应答 | 71 VV VV VV VV FF FF FF | 8 字节 | 4字节小端有符号整数；不带变量名，同一时刻只保留一个未完成 get。 |
| sendme 的查询应答 | 66 PP FF FF FF | 5 字节 | 查询时的当前页面。 |
| bkcmd 指令应答 | 状态码 FF FF FF | 通常4字节 | bkcmd=3 时有效指令可回 01 FF FF FF；这是屏幕指令执行应答，不是启动请求，也不是主控业务完成。 |

串口初值：**9600 波特率，8 数据位，无校验，1 停止位，recmod=0**。V8 自定义 A5 帧是本工程约定，不是陶晶驰通用协议。65 帧格式沿用标准触摸返回格式，但由按钮松开事件里的 `printh` 明确发送；控件 sendkey 保持关闭，避免重复。隐藏的 bInput、bTable 和周期刷新不主动发触摸帧。按住按钮只改变按下颜色，松开才返回数据；run=1 时已禁用的启动按钮无触摸回包。

`get cmd` 完整发送字节：`67 65 74 20 63 6D 64 FF FF FF`。`cmd=0`：`63 6D 64 3D 30 FF FF FF`。`cmd` 仍保留最后请求号，供检查或旧程序兼容；推荐的 V8 事件模式不以它触发业务，不必每次清零。不要同时启用业务帧处理与 cmd 轮询执行，否则会重复处理。

模拟器 String 输入区一行一条命令，按回车执行最后一条或点“执行所有代码”，不手工加 FF。外部串口助手和 STM32 必须逐条追加 FF FF FF。接收按类型和固定长度解析，不能简单把第一次出现的三个 FF 当作整帧结尾；例如负数 -15 的 get 返回为 `71 F1 FF FF FF FF FF FF`。触摸、业务、参数确认、get 应答可能连续到达，DMA 一次回调不等于一帧。

“请求已发送；等待主控”只说明 HMI 已执行串口发送语句。它不代表 STM32 收到，也不代表已启动、保存或复位成功。实际状态仍由主控反馈 run、locked、alarmOn 等变量。
'''
replace_section('## 一 ','## 二 ',section1)
section2='''## 二 每个按钮主动返回什么

### 业务请求：STM32 以这一帧执行控制

点击下表按钮时先有 65 触摸帧，条件通过后再有下表业务帧。请求号仍写入 cmd，但主动帧额外带有来源页，因此两处“应用”和两处“保存”可以区分。

| 页/操作 | 条件 | 主动业务帧（HEX） | STM32 后续处理 |
| --- | --- | --- | --- |
'''
business=[(0,'bStart',1,'run=0','读取已确认方案，判断是否允许启动；执行后反馈运行状态。'),
          (1,'bApply',3,'run=0','读取 nseg、mode、plim、tg1～tgN、hd1～hdN，校验并应用。'),
          (3,'bApply',3,'run=0','读取 plim，校验并应用功率上限。'),
          (1,'bSave',4,'按钮无运行状态限制','读取方案，由 STM32 持久保存；saved=1 不是保存成功回执。'),
          (5,'bSave',4,'run=0，六个保护值>0，uv<ov','读取 ov、uv、oc、ot1～ot3，校验并持久保存。'),
          (4,'bLock',5,'run=0','读取 flo、fhi，执行搜频并反馈 freq100 和 locked。'),
          (5,'bReset',6,'按钮无运行状态限制','判断故障能否复位；实际解除后发 alarmOn=0 事件。'),
          (5,'bAck',7,'无运行状态限制','记录确认操作；屏幕六行未确认项已变成已确认，故障不会被清除。')]
for pid in range(7):business.insert(pid,(pid,'bStop',2,'无运行状态限制','执行停止，并发送 run=0、phaseState=0 等实际反馈。'))
for pid,name,request,cond,action in business:
    b=next(b for b in buttons if b['page']==pid and b['name']==name)
    frame=hexbytes([0xa5,0x5a,1,pid,request,255,255,255])
    section2+=f'| {pid} {names[pid]} / {b["label"]} `{name}` | {cond} | `{frame}` | {action} |\n'
start=next(b for b in buttons if b['page']==0 and b['name']=='bStart')
section2+=f'''
例如在总览点一次“启动加热”，返回区 H 模式应能看到：

```text
{start['touch']}
A5 5A 01 00 01 FF FF FF
```

第一行是按钮松开，第二行才是启动请求。STM32 只按第二行业务帧执行启动，不应等收到 71 帧才响应。发送 `click bStart,0` 也会执行同样的松开事件，适合在当前总览页测试。软件 click 可能绕过触摸禁用状态，业务仍按事件中明确的条件判断。

条件不满足时只有 65 触摸帧，没有业务帧；运行中应用/搜频会显示“请停止加热后操作”，保护保存还会提示缺失参数或欠压阈值错误。启动、停止、应用、保存、搜频、复位会显示发送提示；它们不会冒充主控的成功反馈。

### 每页全部按钮的 65 触摸帧

下面的 ID 和 HEX 从 V8 实际生成工程提取。切页按钮的 PP 是点击前的源页面；主控应使用 **页面+控件 ID** 共同识别，不只看控件 ID。修改工程控件顺序后须同步更新映射。表中参数框文字为初始值，实际值可能已经被你修改。

本表覆盖数字键、导航、算法、温段、输入框和业务按钮。只有上述业务按钮额外发 A5 5A 01；合法数字确认额外发 A5 5A 02。算法选择、插入/删除、冻结/恢复等本地操作只发 65，可在收到后查询第三节所列变量；这些编辑不会直接向主控应用方案。
'''
for pid in range(8):
    section2+=f'\n#### page {pid} {names[pid]}\n\n| 控件 / 按钮 | ID（十进制） | 松开返回（HEX） |\n| --- | --- | --- |\n'
    for b in buttons:
        if b['page']==pid:section2+=f'| `{b["name"]}` {b["label"]} | {b["id"]} | `{b["touch"]}` |\n'
section2+='''
### cmd 兼容查询

只用于检查或旧程序兼容：发送 `get cmd` 会返回下表数值。采用主动业务帧的 STM32 不再用这些查询结果二次执行业务。

| cmd | get cmd 返回（HEX） |
| --- | --- |
'''
for n in range(8):section2+=f'| {n} | `{hexbytes([0x71,n,0,0,0,255,255,255])}` |\n'
replace_section('## 二 ','## 三 ',section2)
md=md.replace('以下操作不会主动上报业务帧，也不会产生新的 cmd。需要主控了解变化时，查询相应变量。它们与上一节的业务按钮不同，但仍可双向读取和写入。','以下操作均主动上报第二节的 65 触摸帧，不产生新的 cmd 或 A5 5A 01 业务请求。主控如需了解操作结果，可在收到触摸帧后查询相应变量；数字键盘合法确认还携带 A5 5A 02 新值帧。')
md=md.replace('**这一步不会设置新的 cmd。** 主控可以主动 get 读取，也可以等用户点击“应用/保存/搜频”后集中读取。','**合法确认会主动上报 A5 5A 02 参数帧，包含原页面、字段编号和新值；不设置新的 cmd。** 主控可更新待应用参数缓存，或等用户点击“应用/保存/搜频”后集中读取。收到单字段确认不等于收到应用请求。')
md=md.replace('HMI → STM32 的统一回包为 `71 + 4字节小端值 + FF FF FF`。','主动确认帧见下面编号表；若 STM32 另外发 get 查询，查询应答为 `71 + 4字节小端值 + FF FF FF`。')
paramtext='''### 41 个输入字段确认帧：直接拿到新数值

确认成功先返回键盘 bOK 的 65 帧，再返回下表 12 字节帧，之后返回原页面。确认非法数值只返回 bOK 的 65 并停留键盘，取消只返回 bCancel 的 65 且不修改参数。字段编号与变量一一对应，不是控件 ID。

| 字段编号（十进制） | 变量 / 单位 | 示例原页与新值 | 主动确认帧（HEX） |
| --- | --- | --- | --- |
'''
examples=json.loads((root/'双向通信例子.json').read_text('utf8'))['parameter_examples']
for p in mapping['params']:
    pid=next(b['page'] for b in buttons if b['name']=='b'+p['var'])
    val=examples[p['var']]
    frame=bytes([0xa5,0x5a,2,pid,p['id']])+val.to_bytes(4,'little')+b'\xff'*3
    paramtext+=f'| {p["id"]} | `{p["var"]}` / {p["unit"]} | page {pid}，{val} | `{frame.hex(" ").upper()}` |\n'
paramtext+='\n功率页和分段页共用 plim（字段1）；在功率页确认1800 W时收到 `A5 5A 02 03 01 08 07 00 00 FF FF FF`，与分段页的差别是 PP=03。主控按原页面+字段号识别，按小端读取4字节新值。\n\n'
md=md.replace('### 键盘每个按键的行为',paramtext+'### 键盘每个按键的行为')
for n,label in [(1,'启动'),(2,'停止'),(3,'应用'),(4,'保存'),(5,'搜频'),(6,'故障复位'),(7,'报警确认')]:
    md=md.replace(f'### {n} {label}请求 get cmd 返回 {n}',f'### {n} {label}请求 QQ={n}')
md=md.replace('3 同时来自分段页和功率页；没有来源码。','请求3来自分段页（PP=01）或功率页（PP=03），现在可以通过主动帧来源页区分；功率页只需处理 plim。')
md=md.replace('与应用一样读取上面的配置集合，按需求将方案和有效保护参数写入 STM32 的非易失存储。屏幕上的“已暂存”只表示它保存到了内存，','按来源区分：PP=01 保存方案，PP=05 保存保护参数；读取对应配置并写入 STM32 的非易失存储。屏幕上的“保存请求已发送”仅表示发出了请求，')
a=md.index('一次正常交互：轮询 get cmd');b=md.index('\n\n',a)
md=md[:a]+'''一次正常交互：STM32 收到 A5 5A 01 → 按 PP 和 QQ 分发 → 逐条查询需要的配置 → 校验、执行 → 回传实际结果。接收中断/DMA持续收帧，解析后的业务请求放入 STM32 队列；不要等一次业务处理结束才重新开启接收。cmd 只是最后请求的镜像，连续点击会覆盖它，主动帧则在每次接受操作时发送。V8 没有请求序号、重传或业务确认帧，因此链路断开不会补发；连续操作的可靠处理仍需主控队列和超时策略。不要把 `cmd=0` 当成业务成功回执。'''+md[b:]
section7='''## 七 STM32 程序如何组织收发

| 工作 | 具体处理 |
| --- | --- |
| 串口接收 | UART中断/DMA持续接收至环形缓冲；按固定帧长拆包，处理分包、粘包。不要在接收中断里等待业务执行。 |
| 65 触摸帧 | 记录源页、控件 ID。对导航/编辑/冻结等本地操作按需查询；对已有业务帧的按键不要在这里再次执行加热操作。 |
| A5 5A 01 业务帧 | 按 PP+QQ 入主控请求队列；停止请求应及时处理，不能被一长串参数查询阻塞。 |
| A5 5A 02 确认帧 | 按字段编号读取小端新值，更新待应用参数缓存。参数逐项确认不代表整组应用。 |
| 71 查询应答 | 按唯一正在等待的 get 变量保存结果，收到后才能发下一条 get。期间来的65/A5帧走各自分支，不当作查询返回。 |
| 66 页面应答 | 更新当前页面；主动业务帧自带源页面，不依赖 sendme 判断历史来源。 |
| 数据发布 | 将真实测量/运行状态写入 HMI，再提交 dirty、push 或 alarmPush；不要假造成功状态。 |

解析长度：65=7字节，66=5字节，71=8字节，A5 5A 01=8字节，A5 5A 02=12字节；核对各帧最后三字节为 FF。其他屏幕标准返回按厂商指令集处理，不要一律按固定4字节（如字符串返回70是可变长）。帧头A5后必须匹配5A及类型，不匹配则重新寻找合法帧头。一次UART回调可能包含多个帧，也可能只到半帧。

STM32 发文本可以用 HAL_UART_Transmit 分两次发送：先命令的 strlen 长度，再发送 `uint8_t end[3]={0xFF,0xFF,0xFF};`。发送要由同一任务/互斥队列管理，不能让别的命令插在文本与结束符之间。不要发送C字符串的末尾0；屏幕上与中文有关的文本指令需使用工程的GBK/GB2312编码。

`bkcmd=3` 适合调试指令应答；01 FF FF FF 只能证明屏幕执行了收到的指令，不证明 STM32 的业务已执行。正式通信按所用 bkcmd 设置解析并处理错误和超时。V8没有业务序号、重传、参数快照锁定或主控成功专用变量；读取整组参数期间用户可能继续编辑，主控应校验读到的配置并按产品需求管理编辑/应用过程。

当前曲线保存最新32组时间点；当前报警是单一活动代码和六行历史；没有自动实测、自动控温、自动停机、掉电保存或虚构演示数据。保护和控温由 STM32 实现，HMI 负责输入与显示。

实现依据：本机编辑器 `help/help.pdf` 的 printh、prints、ref_stop/ref_star、doevents、返回数据章节。V8 阶梯预览先更新控件、恢复刷新并执行 doevents，再画网格和线段，避免控件重绘遮住曲线。蓝色按钮按下颜色改为深蓝，停止按钮为深红；本地冻结/恢复及请求操作还有文字反馈。
'''
replace_section('## 七 ','## 八 ',section7)
md=md.replace('下面先用指令模拟点击，再查询请求；接入 STM32 后直接周期查询 get cmd 即可。','下面用指令模拟点击，V8 会立即返回 65 触摸帧和 A5 5A 01 业务帧。随后 get cmd 只是查看最后请求号；接入 STM32 后优先按第二节的主动业务帧处理，不再周期查询来触发相同操作。')
md=md.replace('cmd 是单个槽位，连续点击可能覆盖前一个请求。','cmd 是单个槽位，连续点击可能覆盖前一个请求；V8 应以第二节主动业务帧入主控队列，get cmd 仅用于查看最后请求。')
md=md.replace('只给主控发出 `cmd=6` 请求','主动向主控发送复位请求帧（QQ=6，同时写 cmd=6）')
md=md.replace('点击重新搜频只产生 cmd=5，请求主控执行搜频。','点击重新搜频主动发送业务请求帧（PP=04、QQ=05，同时写 cmd=5），由主控执行搜频。')
md=md.replace('屏幕只保存内存中的参数并产生 cmd=4；','屏幕保存内存中的参数并发送保存请求帧（PP=05、QQ=04，同时写 cmd=4）；')
md=md.replace('故障复位只产生 cmd=6。','故障复位发送复位请求帧（QQ=6，同时写 cmd=6）。')
path.write_text(md,'utf8')
print('Updated existing MD:',len(md),'characters;',len(buttons),'button mappings;',len(mapping['params']),'parameter frames')
