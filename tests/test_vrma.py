"""Mirroring fixtures exercise rotations, mapping, shared accessors and controls."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest

from vrm_overlay.vrma import accessor_values, load_glb, mirrored_document, mirror_vrma, write_glb


def fixture():
    values=[(0.0,),(1.0,),(0.4,1.0,0.2),(0.8,1.0,0.3),(0.1,0.2,0.3,0.9),(0.2,0.1,-0.3,0.9)]
    binary=b''.join(struct.pack('<'+'f'*len(row),*row) for row in values)
    doc=dict(asset=dict(version='2.0'),buffers=[dict(byteLength=len(binary))],
             bufferViews=[dict(buffer=0,byteOffset=0,byteLength=8),dict(buffer=0,byteOffset=8,byteLength=24),dict(buffer=0,byteOffset=32,byteLength=32)],
             accessors=[dict(bufferView=0,componentType=5126,count=2,type='SCALAR'),dict(bufferView=1,componentType=5126,count=2,type='VEC3'),dict(bufferView=2,componentType=5126,count=2,type='VEC4')],
             nodes=[dict(name='Hips',translation=[0.4,1,0],children=[1,2]),dict(name='LeftHand',translation=[0.5,0,0],rotation=[0.1,0.2,0.3,0.9]),dict(name='RightHand',translation=[-0.5,0,0]),dict(name='Strength')],
             extensions=dict(VRMC_vrm_animation=dict(specVersion='1.0',humanoid=dict(humanBones=dict(hips=dict(node=0),leftHand=dict(node=1),rightHand=dict(node=2))),expressions=dict(preset=dict(blinkLeft=dict(node=3))))),
             animations=[dict(samplers=[dict(input=0,output=1,interpolation='LINEAR'),dict(input=0,output=2,interpolation='LINEAR')],channels=[dict(sampler=0,target=dict(node=0,path='translation')),dict(sampler=1,target=dict(node=1,path='rotation')),dict(sampler=0,target=dict(node=3,path='translation'))])])
    return doc,binary


class MirrorTests(unittest.TestCase):
    def test_mirror_swaps_bones_and_reflects_rotations_root_but_not_expression_strength(self):
        doc,binary=fixture();original=copy.deepcopy(doc)
        mirrored,output=mirrored_document(doc,binary)
        self.assertEqual(doc,original)
        bones=mirrored['extensions']['VRMC_vrm_animation']['humanoid']['humanBones']
        self.assertEqual(bones['rightHand']['node'],1)
        self.assertEqual(bones['leftHand']['node'],2)
        self.assertEqual(mirrored['nodes'][1]['name'],'RightHand')
        self.assertEqual(mirrored['nodes'][1]['rotation'],[0.1,-0.2,-0.3,0.9])
        channels=mirrored['animations'][0]['channels'];samplers=mirrored['animations'][0]['samplers']
        for c in channels:
            values=accessor_values(mirrored,output,samplers[c['sampler']]['output'])
            if c['target']['node']==0:self.assertAlmostEqual(values[0][0],-0.4)
            if c['target']['node']==1:self.assertAlmostEqual(values[0][1],-0.2)
            if c['target']['node']==3:self.assertAlmostEqual(values[0][0],0.4)
        self.assertIn('blinkRight',mirrored['extensions']['VRMC_vrm_animation']['expressions']['preset'])

    def test_double_mirror_restores_samples_and_file_output_does_not_overwrite(self):
        doc,binary=fixture();first,data=mirrored_document(doc,binary);second,data2=mirrored_document(first,data)
        self.assertEqual(second['nodes'],doc['nodes'])
        for original,restored in zip(doc['animations'][0]['channels'],second['animations'][0]['channels']):
            before=doc['animations'][0]['samplers'][original['sampler']]['output']
            after=second['animations'][0]['samplers'][restored['sampler']]['output']
            self.assertEqual(accessor_values(doc,binary,before),accessor_values(second,data2,after))
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'right.vrma';target=Path(directory)/'left.vrma'
            write_glb(source,doc,binary)
            source.with_suffix('.cfg').write_text('# Keep attribution\nspring.drag=1.250\nbounds.world_w=1.0\nstartup=1\nllm_enabled=1\n')
            original=source.read_bytes();mirror_vrma(source,target)
            self.assertEqual(source.read_bytes(),original)
            self.assertTrue(load_glb(target)[0]['animations'])
            config=target.with_suffix('.cfg').read_text()
            self.assertIn('spring.drag=1.250',config);self.assertIn('Keep attribution',config)
            self.assertIn('startup=0',config);self.assertNotIn('bounds.world',config)
            self.assertIn('llm_enabled=0',config)
            with self.assertRaises(ValueError):mirror_vrma(source,target)

    def test_bad_geometry_and_buffer_layouts_fail_explicitly(self):
        doc,binary=fixture();doc['meshes']=[{}]
        with self.assertRaises(ValueError):mirrored_document(doc,binary)

    def test_cubic_rotation_tangents_are_reflected_with_the_keys(self):
        doc,binary=fixture()
        rows=[(0.1,0.2,0.3,0.4),(0,0,0,1),(0.5,0.6,0.7,0.8)]*2
        raw=b''.join(struct.pack('<ffff',*row) for row in rows)
        doc['bufferViews'].append(dict(buffer=0,byteOffset=len(binary),byteLength=len(raw)))
        doc['accessors'][2].update(bufferView=3,count=6)
        doc['animations'][0]['samplers'][1]['interpolation']='CUBICSPLINE'
        binary+=raw;doc['buffers'][0]['byteLength']=len(binary)
        mirrored,data=mirrored_document(doc,binary)
        channel=mirrored['animations'][0]['channels'][1]
        sampler=mirrored['animations'][0]['samplers'][channel['sampler']]
        self.assertEqual(sampler['interpolation'],'CUBICSPLINE')
        values=accessor_values(mirrored,data,sampler['output'])
        for before,after in zip(rows,values):
            for index,value in enumerate(before):
                self.assertAlmostEqual(after[index],-value if index in (1,2) else value)
        doc,binary=fixture();doc['bufferViews'][1]['byteLength']=4
        with self.assertRaises(ValueError):mirrored_document(doc,binary)
