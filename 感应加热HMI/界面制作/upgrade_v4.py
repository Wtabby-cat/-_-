from pathlib import Path
p=Path(__file__).with_name('build_hmi.py');s=p.read_text('utf8')
s=s.replace("加热1_界面版_V3.HMI","加热1_调试版_V4.HMI")
a=s.index('WINDOWS={}');b=s.index('FLO=param',a)
s=s[:a]+'''WINDOWS={}
for pid,prefix,value in [(2,'t',600),(3,'p',600),(4,'f',60),(6,'a',60)]:
    pair=[param(prefix+'from','时间起点',0,'s',0,86399),param(prefix+'to','时间终点',value,'s',1,86400)]
    for item in pair:item['view_only']=True
    WINDOWS[pid]=pair

'''+s[b:]
s=s.replace("value='—' if not p['initial'] else str(p['initial'])", "value='—' if not p['initial'] and not p.get('view_only') else str(p['initial'])")
s=s.replace("if not p['initial']:self.load", "if not p['initial'] and not p.get('view_only'):self.load")
s=s.replace("'演示待机'","'待机'").replace('演示运行','运行中').replace("'演示界面 · 主控未连接'","''")
s=s.replace("code=f'run=0\\r\\npage {self.pid}'", "code='cmd=2'")
a=s.index('    def curve_controls(self):');b=s.index('\n\ndef plot(',a)
s=s[:a]+'''    def curve_controls(self):
        self.text('lWindow','时间范围',16,382,70,30,font=3)
        self.field(WINDOWS[self.pid][0],92,380,94,34)
        self.text('uFrom','s 至',192,382,48,30,font=3)
        self.field(WINDOWS[self.pid][1],244,380,94,34)
        self.text('uTo','s',344,382,26,30,font=3)
        self.button('bFreeze','冻结显示',466,380,150,34,font=2,code='frozen=1\\r\\ntFoot.txt="已冻结显示"')
        self.button('bLive','恢复实时',632,380,152,34,font=2,color=WHITE,bg=BLUE,code=f'frozen=0\\r\\npage {self.pid}')
        self.load +=['if(frozen==1)\\r\\n{\\r\\ntFoot.txt="已冻结显示"\\r\\n}']
'''+s[b:]
# Keep parameter-derived programme preview, remove all synthetic trace data.
s=s.replace("    for points,color in series:", "    if page.pid in (2,3,4,6):series=[];meta['series']=[]\n    for points,color in series:")
s=s.replace("(19.8,20.4),[(19.8,'19.8'),(20.1,'20.1'),(20.4,'20.4')]", "(15,25),[(15,'15'),(20,'20'),(25,'25')]")
a=s.index('# Time window controls');b=s.index('# Frequency bounds',a)
s=s[:a]+'''# Command-driven display, alarms and history rendering are installed below.
from debug_runtime import install
install(PAGES,GLOBALS,WINDOWS,rgb565,draw_command,BLUE,GREEN,TEAL)

'''+s[b:]
# Remove fake fixed readings from initial editor canvas as well as runtime.
s=s.replace("'119.6'","'--'").replace("'120.0'","'--'").replace("'860'","'--'").replace("'308'","'--'").replace("'20.10'","'--'").replace("'18.2'","'--'").replace("'124.8'","'--'").replace("'114.2'","'--'").replace("'62.0°'","'--'").replace("'0.8'","'--'")
s=s.replace("'已锁定'","'未锁定'").replace("'1080 s'","'0 s'")
s=s.replace("code='run=1\\r\\nstage=1\\r\\nstageSec=0\\r\\ntotalSec=0\\r\\ndone=0\\r\\npage 0'", "code='cmd=1'")
s=s.replace("'无（演示）'","'无'")
s=s.replace("[('12 s','测温数据中断','已复位'),('8 s','锁相失败','已复位')]", "[('','',''),('','','')]")
s=s.replace("'暂无待处理报警\\r\\n\\r\\n以上为示例记录'", "''")
s=s.replace("'锁相状态与相位反馈\\r\\n等待主控协议接入'", "''")
s=s.replace("objname=b'tmRun'","objname=b'tmInput'").replace("tim=(1000).to_bytes", "tim=(200).to_bytes")
s=s.replace("// 界面演示工程：不向主控发送加热命令。协议确定后接入。", "// 指令调试工程：输入变量后置 dirty 或 push，详见指令调试说明。")
s=s.replace('## 1 秒定时器','## 200ms 输入刷新定时器').replace(' · 数据为演示值',' · 初始数据为空')
p.write_text(s,'utf8')
