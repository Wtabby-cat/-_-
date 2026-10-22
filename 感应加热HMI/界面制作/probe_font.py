from pathlib import Path
import struct
from PIL import Image

ROOT=Path(__file__).resolve().parent
font=(ROOT.parent/'24gb2312.zi').read_bytes()
base=44+font[17]
glyphs={}
for i in range(struct.unpack_from('<I',font,12)[0]):
    r=font[base+10*i:base+10*i+10]
    glyphs[r[:2]]=(r[2],font[base+int.from_bytes(r[5:8],'little'):base+int.from_bytes(r[5:8],'little')+int.from_bytes(r[8:10],'little')])
def decode(data):
    pixels=[]
    for b in data[1:]:
        if b<32:pixels.extend([0]*(b&31))
        elif b<64:pixels.extend([255]*(b&31))
        elif b<128:pixels.extend([0]*(b&31)+[255]*(1+bool(b&32)))
        elif b<192:pixels.extend([0]*((b>>3)&7)+[round((b&7)*255/7)])
        else:pixels.extend([round(((b>>3)&7)*255/7),round((b&7)*255/7)])
    return pixels
im=Image.new('RGB',(400,120),'white')
for idx,ch in enumerate('119.6运行'):
    key=ch.encode('gbk').ljust(2,b'\0');w,data=glyphs[key];pixels=decode(data)
    print(ch,w,len(pixels),w*24)
    a=Image.new('L',(w,24));a.putdata(pixels[:w*24]);im.paste(Image.merge('RGB',(a,a,a)),(idx*48,0))
    a=Image.new('L',(24,w));a.putdata(pixels[:w*24]);a=a.transpose(Image.Transpose.TRANSPOSE);im.paste(Image.merge('RGB',(a,a,a)),(idx*48,60))
im.save(ROOT/'font_probe.png')
