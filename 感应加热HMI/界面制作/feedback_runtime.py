"""Chinese UI messages; STM32 sends IDs and numeric arguments only."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MESSAGES = json.loads((ROOT / 'feedback_messages.json').read_text('utf8'))

def validate_ids():
    header = (ROOT.parent.parent / 'Core/Inc/ih_hmi_messages.h').read_text('utf8')
    names = re.findall(r'^\s*HMI_MSG_(\w+)(?:\s*=\s*1)?\s*[,\n]', header, re.M)
    assert names == [m['name'] for m in MESSAGES], 'MCU and HMI message IDs differ'
    for m in MESSAGES:
        m['text'].encode('gb2312')

def render():
    validate_ids()
    lines = ['if(msgPush==1)', '{', 'msgPush=0']
    for code, m in enumerate(MESSAGES, 1):
        lines += [f'if(msgCode=={code})', '{']
        if m.get('format') in ('uv', 'uv_history', 'oc'):
            unit='A' if m.get('format')=='oc' else 'V'
            # Only append to the existing destination. A prefix followed by
            # that same destination on the RHS can reorder text on the HMI.
            lines += [f'tFoot.txt="{m["text"]}："',
                      'tmp=msgA/10', 'covx tmp,tFmt.txt,0,0',
                      'tFoot.txt=tFoot.txt+tFmt.txt',
                      'tFoot.txt=tFoot.txt+"."',
                      'tmp=msgA%10', 'covx tmp,tFmt.txt,0,0',
                      'tFoot.txt=tFoot.txt+tFmt.txt',
                      f'tFoot.txt=tFoot.txt+"{unit}，门限"',
                      'covx msgB,tFmt.txt,0,0',
                      'tFoot.txt=tFoot.txt+tFmt.txt',
                      f'tFoot.txt=tFoot.txt+"{unit}，请复位"']
        else:
            lines += [f'tFoot.txt="{m["text"]}"']
        lines += ['}']
    return lines + ['}']
