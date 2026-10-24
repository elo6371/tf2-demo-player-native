import csv,collections
rows=list(csv.DictReader(open('bsp_inventory.tsv',encoding='utf-8'),delimiter='\t'))
c=collections.Counter()
for r in rows:
    p=r['path'].replace('\\','/')
    c['download' if '/download/maps/' in p else 'maps' if '/tf/maps/' in p else 'other']+=1
print("stores:",dict(c))
print("with demos:",sum(1 for r in rows if int(r['demos_in_tf_demos'])>0),"without:",sum(1 for r in rows if int(r['demos_in_tf_demos'])==0))
mat=list(csv.DictReader(open('map_material_reachability.tsv',encoding='utf-8'),delimiter='\t'))
print("material rows",len(mat),dict(collections.Counter(m['map'] for m in mat)))
