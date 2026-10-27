"""Dependency-free mirroring for animation-only VRMA/GLB humanoid clips."""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path
import re
import struct

JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def load_glb(path):
    data = Path(path).read_bytes()
    if len(data) < 20 or struct.unpack_from('<III', data) != (0x46546C67, 2, len(data)):
        raise ValueError('Expected a complete glTF 2.0 binary file.')
    chunks, offset = [], 12
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError('Truncated GLB chunk header.')
        length, kind = struct.unpack_from('<II', data, offset)
        offset += 8
        if length % 4 or offset + length > len(data):
            raise ValueError('Invalid GLB chunk length.')
        chunks.append((kind, data[offset:offset + length]))
        offset += length
    if not chunks or chunks[0][0] != JSON_CHUNK:
        raise ValueError('GLB must start with a JSON chunk.')
    document = json.loads(chunks[0][1])
    binaries = [value for kind, value in chunks if kind == BIN_CHUNK]
    if len(binaries) != 1 or len(document.get('buffers', [])) != 1 or document['buffers'][0].get('uri'):
        raise ValueError('Use a VRMA with one embedded binary buffer.')
    return document, binaries[0]


def accessor_values(document, binary, index):
    accessor = document['accessors'][index]
    if accessor['componentType'] != 5126 or accessor.get('sparse'):
        raise ValueError('Animation outputs must use non-sparse float accessors.')
    components = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}.get(accessor['type'])
    if components is None:
        raise ValueError('Unsupported animation accessor type.')
    view = document['bufferViews'][accessor['bufferView']]
    if view.get('buffer', 0) != 0 or view.get('extensions'):
        raise ValueError('Compressed/external animation buffers are not supported.')
    stride = view.get('byteStride', components * 4)
    start = accessor.get('byteOffset', 0)
    count = accessor['count']
    if count < 1 or stride < components * 4 or stride % 4 or start < 0 or view.get('byteOffset',0) < 0:
        raise ValueError('Invalid accessor layout.')
    end = start + (count - 1) * stride + components * 4
    if end > view['byteLength'] or view.get('byteOffset', 0) + end > len(binary):
        raise ValueError('Animation accessor exceeds its buffer view.')
    values = [struct.unpack_from('<' + 'f' * components, binary, view.get('byteOffset', 0) + start + i * stride) for i in range(count)]
    if any(not math.isfinite(value) for row in values for value in row):
        raise ValueError('Animation contains non-finite values.')
    return values


def swap_sides(name):
    replacements = {'left':'right', 'right':'left', 'Left':'Right', 'Right':'Left', 'LEFT':'RIGHT', 'RIGHT':'LEFT'}
    name = re.sub('LEFT|RIGHT|Left|Right|left|right', lambda m: replacements[m[0]], name)
    return re.sub(r'(?<=[_.])([LR])(?=[_.]|$)', lambda m: 'R' if m[0] == 'L' else 'L', name)


def mirrored_document(document, binary):
    result = copy.deepcopy(document)
    extension = result.get('extensions', {}).get('VRMC_vrm_animation')
    if not extension or not extension.get('humanoid', {}).get('humanBones'):
        raise ValueError('The clip must have VRMC_vrm_animation humanoid mapping.')
    if result.get('meshes') or result.get('skins'):
        raise ValueError('Export an animation-only VRMA; mesh/skin mirroring is not supported.')
    expression_nodes = set()
    expressions = extension.get('expressions', {})
    for category in ('preset', 'custom'):
        entries = expressions.get(category, {})
        expression_nodes.update(entry['node'] for entry in entries.values())
        if category == 'preset':
            expressions[category] = {swap_sides(key): value for key, value in entries.items()}
    bones = extension['humanoid']['humanBones']
    extension['humanoid']['humanBones'] = {swap_sides(key): value for key, value in bones.items()}
    for index, node in enumerate(result.get('nodes', [])):
        old_name = node.get('name', '')
        if old_name:
            node['name'] = swap_sides(old_name)
            if node.get('extras', {}).get('name') == old_name:
                node['extras']['name'] = node['name']
        if index in expression_nodes:
            continue  # Expression strengths are scalar controls, not spatial X.
        if 'translation' in node:
            node['translation'][0] *= -1
        if 'rotation' in node:
            node['rotation'][1] *= -1
            node['rotation'][2] *= -1
        if 'matrix' in node:
            node['matrix'] = [value * (-1 if (i % 4 == 0) != (i // 4 == 0) else 1) for i, value in enumerate(node['matrix'])]
    output = bytearray(binary[:result['buffers'][0]['byteLength']])
    transformed = {}
    for animation in result.get('animations', []):
        # Copy a sampler per spatial channel: a source accessor/sampler may also
        # drive a non-spatial expression, so never negate shared storage in place.
        for channel in animation['channels']:
            target = channel['target']
            path = target.get('path')
            if path not in ('translation', 'rotation') or target.get('node') in expression_nodes:
                continue
            if 'node' not in target:
                raise ValueError('Animation pointer channels are not supported.')
            sampler = copy.deepcopy(animation['samplers'][channel['sampler']])
            key = (sampler['output'], path)
            if key not in transformed:
                values = accessor_values(document, binary, sampler['output'])
                components = 3 if path == 'translation' else 4
                if len(values[0]) != components:
                    raise ValueError('Animation output does not match its target path.')
                changed = [tuple(-v if (j == 0 if components == 3 else j in (1, 2)) else v for j, v in enumerate(row)) for row in values]
                output.extend(bytes((-len(output)) % 4))
                view_index = len(result.setdefault('bufferViews', []))
                raw = b''.join(struct.pack('<' + 'f' * components, *row) for row in changed)
                result['bufferViews'].append(dict(buffer=0, byteOffset=len(output), byteLength=len(raw)))
                output.extend(raw)
                accessor = copy.deepcopy(document['accessors'][sampler['output']])
                accessor.update(bufferView=view_index, byteOffset=0,
                                min=[min(row[j] for row in changed) for j in range(components)],
                                max=[max(row[j] for row in changed) for j in range(components)])
                transformed[key] = len(result['accessors'])
                result['accessors'].append(accessor)
            sampler['output'] = transformed[key]
            channel['sampler'] = len(animation['samplers'])
            animation['samplers'].append(sampler)
    result['buffers'][0]['byteLength'] = len(output)
    return result, bytes(output)


def write_glb(path, document, binary):
    encoded = json.dumps(document, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode()
    encoded += b' ' * ((-len(encoded)) % 4)
    binary += bytes((-len(binary)) % 4)
    data = struct.pack('<III', 0x46546C67, 2, 28 + len(encoded) + len(binary))
    data += struct.pack('<II', len(encoded), JSON_CHUNK) + encoded
    data += struct.pack('<II', len(binary), BIN_CHUNK) + binary
    with Path(path).open('xb') as stream:
        stream.write(data)


def mirror_vrma(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve() or destination.exists() or destination.with_suffix('.cfg').exists():
        raise ValueError('Choose a new output filename; existing animations/configs are never overwritten.')
    document, binary = load_glb(source)
    mirrored, data = mirrored_document(document, binary)
    for animation in mirrored.get('animations', []):
        animation['name'] = destination.stem
    write_glb(destination, mirrored, data)
    # Keep tuning/notices; a new derivative needs its own roles and LLM permission.
    from .animation_config import save_properties
    config = source.with_suffix('.cfg')
    if config.exists():
        lines = config.read_text().splitlines(keepends=True)
        destination.with_suffix('.cfg').write_text(''.join(line for line in lines if not line.lstrip().startswith('bounds.')))
    save_properties(destination, dict(loopable=False, startup=False, is_idle=False, llm_enabled=False))
    return destination
