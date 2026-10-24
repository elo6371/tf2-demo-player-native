#!/usr/bin/env python3
"""Merge raw/player_models.json + raw/vtx_index_counts.json + raw/materials_resolved.json
+ raw/anim_models.json into raw/nine_class_table.tsv.
Every number comes from a real MDL/VVD/VTX/VMT/VTF header read out of the TF2 VPKs;
nothing is derived from filenames alone. Counts come from the studiohdr_t fields
(mdl.boneCount / mdl.sequenceCount / ...), not from len() of the decoded arrays."""
import json, os
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(BASE, 'raw')

pm = json.load(open(os.path.join(RAW, 'player_models.json'), encoding='utf-8'))
classes = pm['classes']
vtx = {r['class']: r for r in json.load(open(os.path.join(RAW, 'vtx_index_counts.json'), encoding='utf-8'))}
mat = {r['className']: r for r in json.load(open(os.path.join(RAW, 'materials_resolved.json'), encoding='utf-8'))}
anim = {r['path']: r for r in json.load(open(os.path.join(RAW, 'anim_models.json'), encoding='utf-8'))['animationModels']}

rows = []
for c in classes:
    cls = c['className']
    v = vtx[cls]
    mm = mat[cls]
    mdl = c['mdl']
    am = anim.get(c['mdlPath'].replace('.mdl', '_animations.mdl'), {})
    rows.append({
        'class': cls,
        'tfClass': c['tfClass'],
        'mdl': c['mdlPath'],
        'mdlMagic': mdl.get('magic'),
        'mdlVersion': mdl.get('version'),
        'checksum': mdl.get('checksum'),
        'fileBytes': mdl.get('length'),
        'bones': mdl.get('boneCount'),
        'sequences': mdl.get('sequenceCount'),
        'textures': mdl.get('textureCount'),
        'cdTextures': mdl.get('cdTextureCount'),
        'bodyParts': mdl.get('bodyPartCount'),
        'attachments': mdl.get('attachmentCount'),
        'vvdLods': c['vvd']['numLODs'],
        'vvdLod0Verts': c['vvd']['lod0Vertices'],
        'vtxVersion': c['vtx']['.dx90.vtx']['version'],
        'vtxLod0RenderIndices': v['vtxLod0RenderIndices'],
        'vtxTotalRenderIndices': v['vtxTotalRenderIndices'],
        'vtxDescriptors': v['descriptorCount'],
        'vtxLod0LostTo12000Cap': v['lostIndicesToCap'],
        'companionVvd': c['companions']['.vvd'],
        'companionDx90': c['companions']['.dx90.vtx'],
        'companionDx80': c['companions']['.dx80.vtx'],
        'companionSw': c['companions']['.sw.vtx'],
        'companionPhy': c['companions']['.phy'],
        'companionBareVtx': c['companions']['.vtx'],
        'materialSlots': mm['slots'],
        'materialResolved': mm['resolved'],
        'materialIncomplete': '|'.join(mm['incomplete']),
        'slot0Texture': mm['slot0'],
        'animModel': am.get('path'),
        'animBones': am.get('boneCount'),
        'animSequences': am.get('sequenceCount'),
    })

cols = list(rows[0].keys())
out = os.path.join(RAW, 'nine_class_table.tsv')
with open(out, 'w', encoding='utf-8', newline='') as f:
    f.write('\t'.join(cols) + '\n')
    for r in rows:
        f.write('\t'.join('' if r[c] is None else str(r[c]) for c in cols) + '\n')
print('wrote', len(rows), 'rows,', len(cols), 'columns')
