"""Gem menu vocabulary, bounded avatar colour extraction and shared layout data.

Shapes use normalized move/line/cubic/circle/close commands. Qt and the native
Wayland popup draw the same paths without an SVG or image-generation dependency.
"""
import base64
import colorsys
from collections import defaultdict
import json
import math
from pathlib import Path
import struct
from urllib.parse import unquote, urlsplit

from PySide6.QtCore import QByteArray, QBuffer, QIODevice, QSize
from PySide6.QtGui import QImage, QImageReader

WIDTH, HEIGHT = 448, 472
CENTER = (224, 220)
PAGE_SIZE, MAX_ACTIONS = 6, 64
MAX_PAGES = 16

ICONS = {
    'walk': [['O',14,3,2],['M',13,7],['L',10,14],['L',16,17],['L',17,23],['M',10,14],['L',6,21],['M',12,9],['L',17,12],['L',21,11],['M',11,8],['L',7,8],['L',4,12]],
    'left': [['M',11,4],['L',3,12],['L',11,20],['M',3,12],['L',22,12]],
    'right': [['M',13,4],['L',21,12],['L',13,20],['M',21,12],['L',2,12]],
    'chat': [['M',5,4],['L',19,4],['C',21,4,22,5,22,7],['L',22,15],['C',22,17,20,18,18,18],['L',10,18],['L',4,22],['L',5,17],['C',3,17,2,16,2,14],['L',2,7],['C',2,5,3,4,5,4],['Z'],['O',8,10,0.6],['O',16,10,0.6],['M',8,14],['C',10,16,14,16,16,14]],
    'studio': [['M',4,20],['L',17,7],['L',20,10],['L',7,23],['Z'],['M',18,1],['L',18,4],['M',22,5],['L',20,5],['M',12,2],['L',13,4],['M',3,9],['L',3,13],['M',1,11],['L',5,11]],
    'pin': [['M',7,2],['L',17,2],['M',9,2],['L',9,9],['L',5,14],['L',19,14],['L',15,9],['L',15,2],['M',12,14],['L',12,23]],
    'hide': [['M',19,3],['C',8,-1,0,7,4,16],['C',7,24,19,24,22,15],['C',14,19,8,10,19,3],['Z']],
    'power': [['M',7,4],['C',-3,11,3,23,12,23],['C',22,23,27,11,17,4],['M',12,1],['L',12,12]],
    'colours': [['M',12,2],['C',-1,2,-2,22,10,22],['C',16,22,8,15,17,15],['C',27,15,25,2,12,2],['Z'],['O',7,8,1],['O',13,6,1],['O',19,9,1],['O',5,14,1]],
    'avatar': [['O',12,7,4],['M',3,22],['C',3,11,21,11,21,22],['Z']],
    'spark': [['M',12,1],['L',15,9],['L',23,12],['L',15,15],['L',12,23],['L',9,15],['L',1,12],['L',9,9],['Z']],
    'back': [['M',11,4],['L',3,12],['L',11,20],['M',3,12],['L',22,12]],
    'close': [['M',5,5],['L',19,19],['M',19,5],['L',5,19]],
    'play': [['M',6,3],['L',21,12],['L',6,21],['Z']],
    'stop': [['M',5,5],['L',19,5],['L',19,19],['L',5,19],['Z']],
}

PRESETS = {
    'rose': ('Rose quartz', '#c75bbd', '#f3c78c'),
    'moon': ('Moonstone', '#7c81dc', '#c0e4ff'),
    'mint': ('Jade', '#46ad95', '#d9e9a0'),
    'amber': ('Amber', '#cc8450', '#ffe3a1'),
    'pearl': ('Pearl', '#8f89a1', '#ffe4d3'),
}


def rgb_hex(rgb):
    return '#' + ''.join(f'{max(0,min(255,round(c*255))):02x}' for c in rgb)


def palette(primary='#c75bbd', accent='#f3c78c', name='Rose quartz'):
    rgb = tuple(int(primary[i:i+2], 16)/255 for i in (1,3,5))
    hue, saturation, _value = colorsys.rgb_to_hsv(*rgb)
    colour = lambda s,v: rgb_hex(colorsys.hsv_to_rgb(hue,s,v))
    return dict(primary=primary, accent=accent, face=colour(min(.65,saturation+.1),.24),
                shadow=colour(.45,.065), ink='#fff8ee', muted=colour(.15,.84), name=name)


def layout(items):
    """At most six stationary charms per page, with room for labels and paging."""
    count = len(items)
    for i, entry in enumerate(items):
        angle = -math.pi/2 + i * 2*math.pi/max(1,count)
        yield dict(entry, x=round(CENTER[0]+142*math.cos(angle),3),
                   y=round(CENTER[1]+142*math.sin(angle),3), radius=49 if entry.get('hero') else 42)


def _document(path):
    with path.open('rb') as stream:
        head = stream.read(20)
        if head[:4] == b'glTF':
            if len(head)!=20 or struct.unpack_from('<I',head,4)[0]!=2 or head[16:20]!=b'JSON':
                raise ValueError('Unsupported GLB')
            size=struct.unpack_from('<I',head,12)[0]
            if size > 4*1024*1024:
                raise ValueError('Avatar JSON is too large')
            data=head[20:]+stream.read(size)
            if len(data)!=size:
                raise ValueError('Truncated GLB')
            chunk=stream.read(8)
            offset=stream.tell()
            binary_size,kind=struct.unpack('<II',chunk) if len(chunk)==8 else (0,0)
            return json.loads(data), offset, binary_size if kind==0x004e4942 else 0
        if path.stat().st_size > 4*1024*1024:
            raise ValueError('Avatar JSON is too large')
        stream.seek(0)
        return json.load(stream), 0, 0


def _image_bytes(path, document, image, binary_offset, binary_size):
    limit = 16*1024*1024
    if 'bufferView' in image:
        view = document.get('bufferViews',[])[image['bufferView']]
        start,size=view.get('byteOffset',0),view['byteLength']
        if view.get('buffer',0)!=0 or not binary_size or not (0<size<=limit) or start<0 or start+size>binary_size:
            return b''
        with path.open('rb') as stream:
            stream.seek(binary_offset+start)
            return stream.read(size)
    uri=image.get('uri','')
    if uri.startswith('data:image/'):
        header,sep,data=uri.partition(',')
        if sep and header.endswith(';base64') and len(data)<limit*4/3:
            return base64.b64decode(data,validate=True)
        return b''
    parsed=urlsplit(uri)
    if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
        return b''
    image_path=(path.parent/unquote(parsed.path)).resolve()
    if not image_path.is_relative_to(path.parent.resolve()) or not image_path.is_file() or image_path.stat().st_size>limit:
        return b''
    return image_path.read_bytes()


def _raster_format(data):
    # Select a raster decoder explicitly; do not probe SVG or other plugins
    # whose metadata readers may resolve references before an image is decoded.
    if data.startswith(b'\x89PNG\r\n\x1a\n'):return b'png'
    if data.startswith(b'\xff\xd8\xff'):return b'jpeg'
    if data[:4]==b'RIFF' and data[8:12]==b'WEBP':return b'webp'
    if data.startswith(b'BM'):return b'bmp'
    return b''


def avatar_palette(filename):
    """Sample bounded base-colour resources only; never thumbnails or network URLs."""
    path=Path(filename)
    document,offset,binary_size=_document(path)
    buckets=defaultdict(lambda: [0.,0.,0.,0.])
    def sample(r,g,b,weight):
        h,s,v=colorsys.rgb_to_hsv(r,g,b)
        if s<.12 or v<.12 or weight<=0:
            return
        # Prefer recognisable costume/hair accents; skin-like hues remain a
        # candidate when they are the character's distinctive colour.
        if .02<h<.12 and .2<s<.65 and v>.5:
            weight*=.22
        weight*=s**.7
        bucket=buckets[(int(h*36)%36,int(s*4),int(v*4))]
        bucket[0]+=weight
        for i,c in enumerate((r,g,b),1):bucket[i]+=c*weight
    textures=document.get('textures',[])
    seen=set()
    for material in document.get('materials',[])[:96]:
        pbr=material.get('pbrMetallicRoughness',{})
        factor=pbr.get('baseColorFactor',[1,1,1,1])
        if len(factor)!=4 or not all(type(c) in (int,float) and math.isfinite(c) and 0<=c<=1 for c in factor):
            continue
        name=material.get('name','').lower()
        weight=.25 if any(part in name for part in ('skin','face','body')) else 1.
        info=pbr.get('baseColorTexture',{})
        # VRM0 MToon uses the glTF base-colour texture too.
        index=info.get('index')
        if index is None:
            sample(*factor[:3], weight*32*factor[3])
            continue
        if index in seen or len(seen)>=8:
            continue
        seen.add(index)
        try:
            source=textures[index]['source']
            data=_image_bytes(path,document,document['images'][source],offset,binary_size)
            fmt=_raster_format(data)
            if not fmt:continue
            buffer=QBuffer();buffer.setData(QByteArray(data));buffer.open(QIODevice.ReadOnly)
            reader=QImageReader(buffer,QByteArray(fmt))
            size=reader.size()
            if not size.isValid() or size.width()*size.height()>16*1024*1024:
                continue
            reader.setScaledSize(QSize(64,64))
            image=reader.read()
            if image.isNull():continue
            image=image.scaled(32,32).convertToFormat(QImage.Format_RGBA8888)
            pixels=memoryview(image.constBits()).cast('B')
            stride=image.bytesPerLine()
            for y in range(image.height()):
                for x in range(image.width()):
                    index=y*stride+x*4
                    r,g,b,a=pixels[index:index+4]
                    if a>76:
                        sample(r/255*factor[0],g/255*factor[1],b/255*factor[2],weight*a/255*factor[3])
        except (OSError,ValueError,KeyError,TypeError,IndexError,OverflowError):
            continue
    if not buckets:
        return palette(name='Avatar · neutral')
    ranked=sorted(buckets.values(),key=lambda row:row[0],reverse=True)
    colours=[tuple(row[i]/row[0] for i in (1,2,3)) for row in ranked]
    h,s,v=colorsys.rgb_to_hsv(*colours[0])
    main=rgb_hex(colorsys.hsv_to_rgb(h,max(.4,min(.72,s)),max(.65,min(.85,v))))
    accent=None
    for candidate in colours[1:]:
        ah,asat,_=colorsys.rgb_to_hsv(*candidate)
        if min(abs(h-ah),1-abs(h-ah))>.14:
            accent=rgb_hex(colorsys.hsv_to_rgb(ah,max(.25,min(.55,asat)),.98))
            break
    accent=accent or rgb_hex(colorsys.hsv_to_rgb((h+.13)%1,.35,.98))
    return palette(main,accent,'Avatar colours')
