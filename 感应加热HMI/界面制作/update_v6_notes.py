from pathlib import Path

root=Path(__file__).resolve().parent
for name in ['create_manual.py','render_manual_windows.py','使用说明.md','指令调试说明.md']:
    path=root/name
    text=path.read_text('utf8').replace('V5','V6')
    if name=='create_manual.py':
        text=text.replace('tg1～tg8','tg1～tg12').replace('hd1～hd8','hd1～hd12')
        text=text.replace('最多 8 段','最多 12 段').replace("'1～8；指令修改时", "'1～12；指令修改时")
        text=text.replace('插入到 8 段','插入到 12 段').replace('第 2 页显示 7～8','第 2 页显示 7～12')
    if name=='使用说明.md':
        text=text.replace('最多 8 段','最多 12 段')
        text=text.replace('## V6 布局调整','## V6 刷新修复和扩容\n\n- 切换算法、选中行、插入/删除温段和翻页改为页内更新，不执行整页重新加载。\n- 表格更新用 `ref_stop` / `ref_star` 合并刷新，避免先显示全部默认行再隐藏。\n- 温段变量扩展为 `tg1`～`tg12` 和 `hd1`～`hd12`；每页 6 段，两页共 12 段。\n\n## 保留的布局调整')
    if name=='指令调试说明.md':
        text += '\n## V6 温段扩容\n\n`nseg` 范围为 1～12。目标温度变量 `tg1`～`tg12`（整数℃），保温时间 `hd1`～`hd12`（整数秒）。`stagepage=0` 显示 1～6 段，`stagepage=1` 显示 7～12 段。设置页内部按钮使用合并刷新，不再重载整页。串口修改参数后可以按原说明重新进入 `page 1`；已在分段页时，也可发送 `click bTable,0` 刷新参数表和程序预览。\n'
    path.write_text(text,'utf8')
