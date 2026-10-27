"""Validated local starter avatars and their matching animation libraries."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STARTER=ROOT/'assets/starter'


def starter_avatars():
    try:catalog=json.loads((STARTER/'catalog.json').read_text())
    except (OSError,ValueError):return []
    if not isinstance(catalog,dict) or not isinstance(catalog.get('avatars'),list):return []
    rows=[]
    for entry in catalog.get('avatars',[]):
        if not isinstance(entry,dict) or not all(isinstance(entry.get(k),str) for k in ('name','model','animations','rig')):continue
        model=(STARTER/entry['model']).resolve()
        animations=(STARTER/entry['animations']).resolve()
        if model.is_relative_to(STARTER) and animations.is_relative_to(STARTER) and model.is_file() and animations.is_dir():
            rows.append(dict(name=entry['name'],path=str(model),animations=str(animations),rig=entry['rig'],camera=entry.get('camera',{})))
    return rows


def featured_avatars():
    """Creator pages and original preview URLs, cached privately by the gallery."""
    rows = []
    for entry in starter_avatars():
        if entry['rig'] != 'humanoid':
            continue
        thumbnail = ROOT / 'vrm_overlay/thumbnails/starter.png'
        rows.append(dict(entry, id='starter-anime', kind='bundled', badge='Included',
                         author='VRoid / pixiv', thumbnail=thumbnail.as_uri(),
                         description='Sendagaya Shibu · A ready-to-use anime companion with matching motions.',
                         source_url='https://vroid.pixiv.help/hc/en-us/articles/4402614652569',
                         license='CC0 · legacy AvatarSample_1 release', license_url='', instructions=''))
    common = dict(kind='featured', path='', rig='humanoid', license_url='')
    rows.extend([
        dict(common, id='rian', name='Rian', author='RomanticSpica', badge='Free · VRM',
             thumbnail='https://booth.pximg.net/b7d6891c-b86d-40b8-bdd3-f18b7cc6dc44/i/8165924/da3731bc-5354-4264-9ca1-430b61b0ae16_base_resized.jpg',
             source_url='https://booth.pm/ja/items/8165924',
             description='An original VRoid character, with VRM 0 and VRM 1 downloads.',
             license='Creator terms · personal streaming allowed; no redistribution.',
             instructions='Download Rian0.0.vrm (the dressed model), then import it here. Review the creator’s current terms on BOOTH.'),
        dict(common, id='amane', name='Amane', author='yousoron-shop', badge='Free · VRM',
             thumbnail='https://booth.pximg.net/2f2f3fb5-3ccf-4d8f-b9fb-95d5075ae0ec/i/4911831/2e59da3b-e05a-4cdd-8962-a4bdbbfa0125_base_resized.jpg',
             source_url='https://booth.pm/ja/items/4911831',
             description='Four outfit variants, supplied as individual VRMs or a ZIP.',
             license='Creator terms · modification and commercial use allowed; no resale.',
             instructions='Download Type01.vrm or allType.zip. Import it here and choose an outfit if using the ZIP. Review the current BOOTH terms.'),
        dict(common, id='keiro', name='Keiro', author='Neomemo', badge='Paid · VRM',
             thumbnail='https://booth.pximg.net/cdd88825-6dd2-4b0c-a162-5f25d01ebb32/i/5992845/8792166f-ba82-44c9-9778-311c295dc9f6_base_resized.jpg',
             source_url='https://booth.pm/ja/items/5992845',
             description='A small biped animal character, supplied with a VRM 0 model.',
             license='Creator terms · personal purchase; no redistribution or resale.',
             instructions='Purchase from the creator, download the supplied VRM or ZIP, then import it here. Use the VRM rather than the Unity package.'),
    ])
    return rows
