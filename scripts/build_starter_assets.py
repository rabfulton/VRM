#!/usr/bin/env python3
"""Rebuild the curated CC0 starter pack from pinned public source archives."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
from vrm_overlay.asset_build import MotionSource,humanoid_clip,select_animation
from vrm_overlay.vrma import load_glb,write_glb

SOURCES={
 'UAL1':('https://opengameart.org/sites/default/files/universal_animation_librarystandard.zip','18ff1a7215f4852b320203e8aaf02a1578b5c8eef9027fbaedfcedc7b85a3ac2'),
 'UAL2':('https://opengameart.org/sites/default/files/universal_animation_library_2standard.zip','ec0e40d6d78fe9aaad59e322f40865a8675c22f0745e291622e54520391a9217'),
 'CubePets':('https://kenney.nl/media/pages/assets/cube-pets/44e58e945f-1774520254/kenney_cube-pets_1.0.zip','b3bdc99a2ec92c687b875718c5d01e9231d2711ed0e2845f295b474bb42a1283'),
 'Anime':('https://opengameart.org/sites/default/files/avatarsample_d_0.zip','311a383b0b63b55102c82efa5ab2afec0dfcccc1cc6fa2db10fd5583981b3167')}

# output, pack, source clip, loopable, idle, entry/exit pose, floor contact, reverse/hold
CLIPS=[
 ('idle','UAL1','Idle_Loop',True,True,'standing','standing',True,False,False),
 ('idle_talking','UAL1','Idle_Talking_Loop',True,True,'standing','standing',True,False,False),
 ('idle_folded_arms','UAL2','Idle_FoldArms_Loop',True,True,'standing','standing',True,False,False),
 ('walk','UAL1','Walk_Loop',True,False,'standing','standing',False,False,False),
 ('walk_formal','UAL1','Walk_Formal_Loop',True,False,'standing','standing',False,False,False),
 ('jog','UAL1','Jog_Fwd_Loop',True,False,'standing','standing',False,False,False),
 ('push','UAL1','Push_Loop',True,False,'standing','standing',True,False,False),
 ('sit_down','UAL1','Sitting_Enter',False,False,'standing','sitting',False,False,False),
 ('sitting_idle','UAL1','Sitting_Idle_Loop',True,True,'sitting','sitting',False,False,False),
 ('stand_up','UAL1','Sitting_Exit',False,False,'sitting','standing',False,False,False),
 ('get_up','UAL2','LayToIdle',False,False,'lying','standing',False,False,False),
 ('lie_down','UAL2','LayToIdle',False,False,'standing','lying',False,True,False),
 ('lying_idle','UAL2','LayToIdle',True,True,'lying','lying',False,False,True),
 ('climb_up','UAL2','ClimbUp_1m_RM',False,False,'standing','standing',False,False,False)]


def unpack_glb(data):
    length,kind=struct.unpack_from('<II',data,12)
    if kind!=0x4e4f534a:raise ValueError('Missing JSON chunk')
    doc=json.loads(data[20:20+length]); offset=20+length
    size,kind=struct.unpack_from('<II',data,offset)
    if kind!=0x004e4942:raise ValueError('Missing binary chunk')
    return doc,data[offset+8:offset+8+size]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources',type=Path,default=ROOT/'build/assets/sources')
    parser.add_argument('--output',type=Path,default=ROOT/'assets/starter')
    parser.add_argument('--download',action='store_true',help='Download missing pinned source archives')
    parser.add_argument('--force',action='store_true',help='Replace generated starter files whose contents changed')
    args=parser.parse_args(); args.sources.mkdir(parents=True,exist_ok=True)
    archives={}; provenance={}; artifacts={}
    for name,(url,digest) in SOURCES.items():
        path=args.sources/(name+'.zip')
        if not path.exists():
            if not args.download:parser.error(f'Missing {path}; pass --download to obtain the CC0 source archives.')
            data=urllib.request.urlopen(url,timeout=60).read(64*1024*1024)
            if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Source hash mismatch: '+name)
            path.write_bytes(data)
        data=path.read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Source hash mismatch: '+name)
        archives[name]=zipfile.ZipFile(io.BytesIO(data))
        provenance[name]=dict(url=url,sha256=digest,license='CC0-1.0')
    def store(relative,data,source,**changes):
        path=args.output/relative;path.parent.mkdir(parents=True,exist_ok=True)
        if path.exists() and path.read_bytes()!=data and not args.force:raise ValueError(f'Refusing to overwrite {path}; review changes then use --force.')
        path.write_bytes(data)
        artifacts[relative]=dict(source=source,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),**changes)
    def glb(relative,doc,binary,source,**changes):
        # Use the same exclusive writer as the mirroring tool without modifying
        # existing artifacts until their replacement has been constructed.
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'clip.glb';write_glb(path,doc,binary)
            store(relative,path.read_bytes(),source,**changes)
    def config(relative,loop,idle,entry,exit,floor):
        text=f'loopable={int(loop)}\nstartup=0\nis_idle={int(idle)}\nentry_pose={entry}\nexit_pose={exit}\nfloor_contact={int(floor)}\n'
        store(relative,text.encode(),'vrm-overlay')
    for pack in ('UAL1','UAL2'):
        source=None
        for name in archives[pack].namelist():
            if not name.endswith('.glb'):continue
            doc,binary=unpack_glb(archives[pack].read(name))
            if doc.get('animations'):source=MotionSource(doc,binary);break
        if source is None:raise ValueError('No source motion: '+pack)
        for output,p,clip,loop,idle,entry,exit,floor,reverse,hold in CLIPS:
            if p!=pack:continue
            doc,binary=humanoid_clip(source,clip,output,reverse=reverse,hold=hold)
            glb(f'animations/humanoid/{output}.vrma',doc,binary,pack,clip=clip,reverse=reverse,hold=hold,root_motion='in_place')
            config(f'animations/humanoid/{output}.cfg',loop,idle,entry,exit,floor)
            print('Baked',output,flush=True)
    animal='Models/GLB format/animal-fox.glb'
    raw=archives['CubePets'].read(animal)
    doc,binary=unpack_glb(raw)
    doc['nodes'][0]['rotation']=[0,1,0,0]
    doc['nodes'][0]['scale']=[.65,.65,.65]
    # Kenney's GLB references a sibling palette PNG. Embed it so the bundled
    # model is self-contained instead of silently rendering with a black texture.
    packed=bytearray(binary[:doc['buffers'][0]['byteLength']])
    for image in doc.get('images',[]):
        if 'uri' not in image:continue
        uri=image.pop('uri')
        data=archives['CubePets'].read(str(Path(animal).parent/uri))
        packed.extend(bytes(-len(packed)%4))
        image['bufferView']=len(doc['bufferViews']);image['mimeType']='image/png'
        doc['bufferViews'].append(dict(buffer=0,byteOffset=len(packed),byteLength=len(data)))
        packed.extend(data)
    doc['buffers'][0]['byteLength']=len(packed)
    glb('models/fox.glb',doc,bytes(packed),'CubePets',member=animal,changes='Palette PNG embedded; container turned 180 degrees and scaled to 0.65; original rig and animations retained.')
    for clip in ('idle','walk','run','eat','dance','gesture-positive','gesture-negative'):
        selected,data=select_animation(doc,binary,clip)
        glb(f'animations/fox/{clip}.glb',selected,data,'CubePets',clip=clip)
        config(f'animations/fox/{clip}.cfg',clip in ('idle','walk','run','eat','dance'),clip=='idle','standing','standing',False)
    store('models/anime.vrm',archives['Anime'].read('AvatarSample_D.vrm'),'Anime',member='AvatarSample_D.vrm',changes='Unmodified source VRM; original metadata retained.')
    store('KENNEY-LICENSE.txt',archives['CubePets'].read('License.txt'),'CubePets')
    catalog=dict(version=1,avatars=[dict(name='VRoid · Sendagaya Shibu',model='models/anime.vrm',animations='animations/humanoid',rig='humanoid',camera=dict(dist=3,target=[0,.8,0],azimuth=3.14,polar=1.57)),
                                     dict(name='Fox · Kenney',model='models/fox.glb',animations='animations/fox',rig='generic',camera=dict(dist=3,target=[0,.55,0],azimuth=3.14,polar=1.45))])
    store('catalog.json',(json.dumps(catalog,indent=2)+'\n').encode(),'vrm-overlay')
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'manifest.json').write_text(json.dumps(dict(version=1,sources=provenance,artifacts=artifacts),indent=2)+'\n')
    if args.output.resolve()!=(ROOT/'assets/starter').resolve():
        for name in ('README.md','NOTICE.md','CC0-1.0.txt'):
            (args.output/name).write_bytes((ROOT/'assets/starter'/name).read_bytes())
    print('Built',len(artifacts),'starter artifacts in',args.output)


if __name__=='__main__':main()
