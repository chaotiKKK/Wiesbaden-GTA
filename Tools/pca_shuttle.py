import bpy, math, os
import numpy as np

GLB = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle\shuttle.glb"
OUT = r"C:\freebuff\WiesbadenReal_Sicherung\WiesbadenReal\Data\Raw\Shuttle"
def P(m): print("###PCA### " + m)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)
ms=[o for o in bpy.context.scene.objects if o.type=='MESH']; obj=ms[0]
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
if len(ms)>1: bpy.ops.object.join()
me=obj.data; n=len(me.vertices)
co=np.empty(n*3); me.vertices.foreach_get('co',co); co=co.reshape(n,3).astype(np.float64)
c=co-co.mean(0)
cov=(c.T@c)/len(c)
w,V=np.linalg.eigh(cov)
P("eigenwerte %.4f %.4f %.4f" % (w[0],w[1],w[2]))
v=V[:,2]; v=v/np.linalg.norm(v)
zt=np.array([0.,0.,1.])
if np.dot(v,zt)<0: v=-v
dot=float(max(-1.0,min(1.0,np.dot(v,zt))))
ax=np.cross(v,zt); s=np.linalg.norm(ax)
if s<1e-8:
    R=np.eye(3)
else:
    ax=ax/s; ang=math.acos(dot)
    K=np.array([[0,-ax[2],ax[1]],[ax[2],0,-ax[0]],[-ax[1],ax[0],0]])
    R=np.eye(3)+math.sin(ang)*K+(1-math.cos(ang))*(K@K)
new=c@R.T
zmin=new[:,2].min(); H=new[:,2].max()-zmin; zr=new[:,2]-zmin
bot=zr<0.2*H; top=zr>0.8*H
sb=float(np.ptp(new[bot,0])*np.ptp(new[bot,1])) if bot.sum()>3 else 0.0
st=float(np.ptp(new[top,0])*np.ptp(new[top,1])) if top.sum()>3 else 0.0
P("breite unten=%.3f oben=%.3f" % (sb,st))
if st>sb:
    new[:,1]=-new[:,1]; new[:,2]=-new[:,2]; P("geflippt")
new[:,2]-=new[:,2].min()
me.vertices.foreach_set('co', new.reshape(-1).astype(np.float32).copy()); me.update()
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS'); obj.location=(0,0,0)
P("dims X=%.2f Y=%.2f Z=%.2f" % (obj.dimensions.x,obj.dimensions.y,obj.dimensions.z))
sc=bpy.context.scene; sc.render.engine='BLENDER_WORKBENCH'; sc.render.resolution_x=600; sc.render.resolution_y=600
d=max(obj.dimensions.x,obj.dimensions.y,obj.dimensions.z); zc=obj.dimensions.z*0.5
cd=bpy.data.cameras.new('C'); cam=bpy.data.objects.new('C',cd); sc.collection.objects.link(cam); sc.camera=cam
cd.type='ORTHO'; cd.ortho_scale=d*1.35; R2=d*4
def shot(nm,loc,rr):
    cam.location=loc; cam.rotation_euler=rr; sc.render.filepath=os.path.join(OUT,"pca_%s.png"%nm); bpy.ops.render.render(write_still=True)
shot("front",(0,-R2,zc),(math.radians(90),0,0))
shot("side",(R2,0,zc),(math.radians(90),0,math.radians(90)))
cd.type='PERSP'; cam.location=(R2*0.8,-R2*0.8,zc+d*0.4)
import mathutils
dirv=mathutils.Vector((0,0,zc))-cam.location; cam.rotation_euler=dirv.to_track_quat('-Z','Y').to_euler()
sc.render.filepath=os.path.join(OUT,"pca_3q.png"); bpy.ops.render.render(write_still=True)
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,"shuttle_pca.glb"), export_format='GLB', use_selection=True)
P("FERTIG")