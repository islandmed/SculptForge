# Texture orientation check for OBJ, glTF, USD and FBX exports. (tools/export_check/run.sh)
import sys, os, json, struct, subprocess, numpy as np
from PIL import Image
D=sys.argv[1]; FBXDUMP=sys.argv[2]
def ply(path):
    b=open(path,'rb').read(); h=b.index(b'end_header\n')+11; hdr=b[:h].decode().split('\n')
    n=int([l for l in hdr if l.startswith('element vertex')][0].split()[2])
    props=[l.split()[1:] for l in hdr if l.startswith('property') and 'list' not in l]
    props=props[:len(props)]  # vertex props first
    vp=[]; 
    for l in hdr:
        if l.startswith('element face'): break
        if l.startswith('property'): vp.append(l.split()[1:])
    fmt={'float':'f4','uchar':'u1','double':'f8','int':'i4','uint':'u4'}
    dt=np.dtype([(p[1],'<'+fmt[p[0]]) for p in vp])
    a=np.frombuffer(b,dtype=dt,count=n,offset=h)
    P=np.stack([a['x'],a['y'],a['z']],1).astype(np.float64); C=np.stack([a['red'],a['green'],a['blue']],1).astype(np.float64)
    return P,C
def norm(P,ref):
    lo,hi=ref.min(0),ref.max(0); plo,phi=P.min(0),P.max(0)
    return (P-plo)/(phi-plo).max()*(hi-lo).max()+lo
def compare(name,P,UV,img,flipv):
    TP,TC=truth
    P=norm(P,TP)
    im=np.asarray(img.convert('RGB')).astype(np.float64); H,W=im.shape[:2]
    u=UV[:,0]; v=UV[:,1]
    y=((1-v) if flipv else v)*H; x=u*W
    xi=np.clip(np.floor(x).astype(int),0,W-1); yi=np.clip(np.floor(y).astype(int),0,H-1)
    S=im[yi,xi]
    good=0; dmax=0
    for i in range(0,len(P),2000):
        d=((P[i:i+2000,None,:]-TP[None,:,:])**2).sum(-1); j=d.argmin(1); dmax=max(dmax,np.sqrt(d.min(1)).max())
        good+=(np.abs(S[i:i+2000]-TC[j]).max(1)<48).sum()
    print(f'{name:5s} corners={len(P)} match={good/len(P)*100:.1f}%  (pos match err {dmax:.2e}, image {W}x{H}, convention {"v up (flip)" if flipv else "v down"})')
truth=ply(os.path.join(D,'truth.ply'))
# OBJ
vs=[];vts=[];P=[];UV=[]
for l in open(os.path.join(D,'s.obj')):
    t=l.split()
    if not t: continue
    if t[0]=='v': vs.append(list(map(float,t[1:4])))
    elif t[0]=='vt': vts.append(list(map(float,t[1:3])))
    elif t[0]=='f':
        for c in t[1:]:
            a=c.split('/'); P.append(vs[int(a[0])-1]); UV.append(vts[int(a[1])-1])
mtl=[l.split()[1] for l in open(os.path.join(D,'s.mtl')) if l.startswith('map_Kd')][0]
compare('OBJ',np.array(P),np.array(UV),Image.open(os.path.join(D,mtl)),True)
# glTF
b=open(os.path.join(D,'s.glb'),'rb').read()
jl=struct.unpack('<I',b[12:16])[0]; J=json.loads(b[20:20+jl]); bin_=b[20+jl+8:]
def acc(i):
    a=J['accessors'][i]; bv=J['bufferViews'][a['bufferView']]; n={'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']]
    dt={5126:'<f4',5125:'<u4',5123:'<u2'}[a['componentType']]
    return np.frombuffer(bin_,dtype=dt,count=a['count']*n,offset=bv.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,n) if n>1 else np.frombuffer(bin_,dtype=dt,count=a['count'],offset=bv.get('byteOffset',0)+a.get('byteOffset',0))
print('glTF nodes', [ {k:v for k,v in n.items() if k in ('matrix','translation','rotation','scale')} for n in J['nodes']])
pr=J['meshes'][0]['primitives'][0]; idx=acc(pr['indices']); pos=acc(pr['attributes']['POSITION']); tc=acc(pr['attributes']['TEXCOORD_0'])
mat=J['materials'][pr['material']]; ti=mat['pbrMetallicRoughness']['baseColorTexture']['index']; img=J['images'][J['textures'][ti]['source']]
bv=J['bufferViews'][img['bufferView']]; import io
compare('glTF',pos[idx].astype(np.float64),tc[idx].astype(np.float64),Image.open(io.BytesIO(bin_[bv.get('byteOffset',0):bv.get('byteOffset',0)+bv['byteLength']])),False)
# USD
from pxr import Usd, UsdGeom, UsdShade
st=Usd.Stage.Open(os.path.join(D,'s.usda'))
for p in st.Traverse():
    if p.IsA(UsdGeom.Mesh):
        m=UsdGeom.Mesh(p); pts=np.array(m.GetPointsAttr().Get()); fvi=np.array(m.GetFaceVertexIndicesAttr().Get())
        xf=np.array(UsdGeom.Xformable(p).ComputeLocalToWorldTransform(0)); pts=np.c_[pts,np.ones(len(pts))]@xf; pts=pts[:,:3]
        pv=UsdGeom.PrimvarsAPI(p).GetPrimvar('st'); stv=np.array(pv.ComputeFlattened())
        print('USD up axis',UsdGeom.GetStageUpAxis(st),'metersPerUnit',UsdGeom.GetStageMetersPerUnit(st),'st interp',pv.GetInterpolation(), 'orientation', m.GetOrientationAttr().Get(), 'subdiv', m.GetSubdivisionSchemeAttr().Get())
        f=[s for s in st.Traverse() if s.IsA(UsdShade.Shader) and UsdShade.Shader(s).GetIdAttr().Get()=='UsdUVTexture'][0]
        path=UsdShade.Shader(f).GetInput('file').Get().resolvedPath
        compare('USD',pts[fvi],stv,Image.open(path),True)
# FBX
out=subprocess.run([FBXDUMP,os.path.join(D,'s.fbx')],capture_output=True,text=True); print(out.stderr.strip())
a=np.array([list(map(float,l.split()[1:])) for l in out.stdout.strip().split('\n')])
tex=[l.split('filename=')[1].split()[0] for l in out.stderr.split('\n') if 'tex filename=' in l]
compare('FBX',a[:,:3],a[:,3:5],Image.open(tex[0] if os.path.isabs(tex[0]) else os.path.join(D,tex[0])),True)
print('Expect ~90% or more per format: the rest are corners on UV seams and checker borders, where a vertex\'s one '
      'vertexpaint colour cannot match every corner. A flipped convention gives under 1%.')
