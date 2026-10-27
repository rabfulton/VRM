"""Build-time glTF motion baking. No Blender or numerical Python packages needed."""
from __future__ import annotations

import bisect
import copy
import math
import struct
from .vrma import accessor_values


def mul(a, b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z)


def inv(q):
    return (-q[0],-q[1],-q[2],q[3])


def rotate(q, p):
    return mul(mul(q,(*p,0)),inv(q))[:3]


def slerp(a,b,t):
    dot=sum(x*y for x,y in zip(a,b))
    if dot<0: b=tuple(-v for v in b); dot=-dot
    if dot>0.9995:
        q=tuple(x+(y-x)*t for x,y in zip(a,b))
    else:
        angle=math.acos(max(-1,min(1,dot)))
        q=tuple((x*math.sin((1-t)*angle)+y*math.sin(t*angle))/math.sin(angle) for x,y in zip(a,b))
    length=math.sqrt(sum(v*v for v in q))
    return tuple(v/length for v in q)


def rig_mapping(doc):
    names={n.get('name'):i for i,n in enumerate(doc['nodes'])}
    mapping={}
    def bone(key,*aliases):
        for name in aliases:
            if name in names: mapping[key]=names[name]; return
        raise ValueError('Missing source humanoid bone: '+key)
    for key,a,b in [('hips','DEF-hips','pelvis'),('spine','DEF-spine.001','spine_01'),
                    ('chest','DEF-spine.002','spine_02'),('upperChest','DEF-spine.003','spine_03'),
                    ('neck','DEF-neck','neck_01'),('head','DEF-head','Head')]: bone(key,a,b)
    for side,suffix,ue in [('left','L','l'),('right','R','r')]:
        for key,a,b in [('Shoulder','shoulder','clavicle'),('UpperArm','upper_arm','upperarm'),
                        ('LowerArm','forearm','lowerarm'),('Hand','hand','hand'),
                        ('UpperLeg','thigh','thigh'),('LowerLeg','shin','calf'),
                        ('Foot','foot','foot'),('Toes','toe','ball')]:
            bone(side+key,f'DEF-{a}.{suffix}',f'{b}_{ue}')
        for finger,a,b in [('Index','f_index','index'),('Middle','f_middle','middle'),
                           ('Ring','f_ring','ring'),('Little','f_pinky','pinky'),('Thumb','thumb','thumb')]:
            segments=('Metacarpal','Proximal','Distal') if finger=='Thumb' else ('Proximal','Intermediate','Distal')
            for i,segment in enumerate(segments,1):
                bone(side+finger+segment,f'DEF-{a}.{i:02}.{suffix}',f'{b}_{i:02}_{ue}')
    return mapping


class MotionSource:
    def __init__(self, doc, binary):
        self.doc=doc
        self.clips={a['name']:a for a in doc['animations']}
        self.parents={child:i for i,node in enumerate(doc['nodes']) for child in node.get('children',[])}
        self.values={i:accessor_values(doc,binary,i) for a in doc['animations']
                     for s in a['samplers'] for i in (s['input'],s['output'])}
        for a in doc['animations']:
            if any(s.get('interpolation','LINEAR') not in ('LINEAR','STEP') for s in a['samplers']):
                raise ValueError('Bake cubic source channels before importing.')

    def duration(self, name):
        return max(self.values[s['input']][-1][0] for s in self.clips[name]['samplers'])

    def pose(self,name,time):
        local=[(tuple(n.get('translation',(0,0,0))), tuple(n.get('rotation',(0,0,0,1))),
                tuple(n.get('scale',(1,1,1)))) for n in self.doc['nodes']]
        for ch in self.clips[name]['channels']:
            slot={'translation':0,'rotation':1,'scale':2}.get(ch['target']['path'])
            if slot is None: continue
            s=self.clips[name]['samplers'][ch['sampler']]
            times=[row[0] for row in self.values[s['input']]]
            i=max(0,min(len(times)-1,bisect.bisect_right(times,time)-1)); j=min(i+1,len(times)-1)
            t=0 if i==j or s.get('interpolation')=='STEP' else max(0,min(1,(time-times[i])/(times[j]-times[i])))
            a,b=self.values[s['output']][i],self.values[s['output']][j]
            value=slerp(a,b,t) if slot==1 else tuple(x+(y-x)*t for x,y in zip(a,b))
            node=ch['target']['node']; tr=list(local[node]); tr[slot]=value; local[node]=tr
        world={}
        def visit(i):
            if i in world:return world[i]
            p,q,scale=local[i]
            if i in self.parents:
                pp,pq,ps=visit(self.parents[i])
                p=tuple(a+b for a,b in zip(pp,rotate(pq,tuple(a*b for a,b in zip(p,ps)))))
                q=mul(pq,q); scale=tuple(a*b for a,b in zip(scale,ps))
            world[i]=(p,q,scale);return world[i]
        for i in range(len(local)):visit(i)
        return world


def append_accessor(doc, binary, values, kind):
    components={'SCALAR':1,'VEC3':3,'VEC4':4}[kind]
    binary.extend(bytes(-len(binary)%4))
    raw=b''.join(struct.pack('<'+'f'*components,*row) for row in values)
    view=len(doc['bufferViews'])
    doc['bufferViews'].append(dict(buffer=0,byteOffset=len(binary),byteLength=len(raw)))
    binary.extend(raw)
    index=len(doc['accessors'])
    doc['accessors'].append(dict(bufferView=view,componentType=5126,count=len(values),type=kind,
                                 min=[min(v[k] for v in values) for k in range(components)],
                                 max=[max(v[k] for v in values) for k in range(components)]))
    return index


def humanoid_clip(source, clip, name, *, reverse=False, hold=False, fps=30):
    """Collapse helper/root bones, normalize T-pose axes and bake at fixed rate.

    Root locomotion is removed; hips retain pose displacement (sit/get-up/bob).
    Hips height is normalized to 1 and adapted to the target at native import.
    """
    mapping=rig_mapping(source.doc)
    rest=source.pose('A_TPose',0)
    height=rest[mapping['hips']][0][1]
    if height<=0.1:raise ValueError('Expected a metre-scale upright source rig.')
    # Use the same canonical T-pose orientation as VRM 0 avatars (left = +X).
    basis=(0,1,0,0) if rest[mapping['leftHand']][0][0]<rest[mapping['rightHand']][0][0] else (0,0,0,1)
    parents={}
    for bone,node in mapping.items():
        ancestor=source.parents.get(node)
        while ancestor is not None and ancestor not in mapping.values(): ancestor=source.parents.get(ancestor)
        parents[bone]=next((key for key,value in mapping.items() if value==ancestor),None)
    keys=list(mapping); indices={key:i for i,key in enumerate(keys)}
    positions={key:tuple(v/height for v in rotate(basis,rest[node][0])) for key,node in mapping.items()}
    doc=dict(asset=dict(version='2.0',generator='vrm-overlay starter asset builder'),
             scenes=[dict(nodes=[indices['hips']])],scene=0,nodes=[],animations=[],
             buffers=[dict(byteLength=0)],bufferViews=[],accessors=[],
             extensionsUsed=['VRMC_vrm_animation'],
             extensions=dict(VRMC_vrm_animation=dict(specVersion='1.0',humanoid=dict(humanBones={key:dict(node=i) for key,i in indices.items()}))),
             extras=dict(vrmOverlay=dict(normalizedHips=True,sourceClip=clip,reverse=reverse,hold=hold)))
    for key in keys:
        parent=parents[key]; p=positions[key]
        t=tuple(a-b for a,b in zip(p,positions[parent])) if parent else p
        doc['nodes'].append(dict(name=key,translation=t,children=[indices[k] for k in keys if parents[k]==key]))
    duration=1.0 if hold else source.duration(clip)
    count=max(2,math.ceil(duration*fps)+1)
    times=[duration*i/(count-1) for i in range(count)]
    tracks={key:[] for key in keys}; translations=[]
    # Strip translation of the source locomotion root while preserving hips motion.
    root=next((i for i,n in enumerate(source.doc['nodes']) if n.get('name')=='root'),None)
    for time in times:
        sample=0 if hold else duration-time if reverse else time
        pose=source.pose(clip,sample)
        changes={key:mul(mul(basis,mul(pose[node][1],inv(rest[node][1]))),inv(basis)) for key,node in mapping.items()}
        for key in keys:
            q=mul(inv(changes[parents[key]]),changes[key]) if parents[key] else changes[key]
            if tracks[key] and sum(a*b for a,b in zip(q,tracks[key][-1]))<0:q=tuple(-v for v in q)
            tracks[key].append(q)
        p=pose[mapping['hips']][0]
        if root is not None:
            delta=tuple(a-b for a,b in zip(pose[root][0],rest[root][0]))
            p=tuple(a-b for a,b in zip(p,delta))
        translations.append(tuple(v/height for v in rotate(basis,p)))
    binary=bytearray(); inputs=append_accessor(doc,binary,[(t,) for t in times],'SCALAR')
    anim=dict(name=name,channels=[],samplers=[])
    for key in keys:
        output=append_accessor(doc,binary,tracks[key],'VEC4')
        anim['channels'].append(dict(sampler=len(anim['samplers']),target=dict(node=indices[key],path='rotation')))
        anim['samplers'].append(dict(input=inputs,output=output,interpolation='LINEAR'))
    output=append_accessor(doc,binary,translations,'VEC3')
    anim['channels'].append(dict(sampler=len(anim['samplers']),target=dict(node=indices['hips'],path='translation')))
    anim['samplers'].append(dict(input=inputs,output=output,interpolation='LINEAR'))
    doc['animations']=[anim];doc['buffers'][0]['byteLength']=len(binary)
    return doc,bytes(binary)


def select_animation(doc,binary,clip):
    """Animation-only file retaining original node names for a generic animal."""
    result={key:copy.deepcopy(doc[key]) for key in ('asset','nodes','scenes','scene') if key in doc}
    animation=copy.deepcopy(next(a for a in doc['animations'] if a['name']==clip))
    result.update(animations=[animation],buffers=[dict(byteLength=0)],bufferViews=[],accessors=[])
    for node in result['nodes']:
        node.pop('mesh',None);node.pop('skin',None)
    output=bytearray();remap={}
    for sampler in animation['samplers']:
        for key in ('input','output'):
            index=sampler[key]
            if index not in remap:
                remap[index]=append_accessor(result,output,accessor_values(doc,binary,index),doc['accessors'][index]['type'])
            sampler[key]=remap[index]
    result['buffers'][0]['byteLength']=len(output)
    return result,bytes(output)
