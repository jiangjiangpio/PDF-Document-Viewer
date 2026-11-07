from pathlib import Path
import math
OUT=Path(__file__).resolve().parent
import struct
strokes=[]; marker=False; color=0x2563eb; width=2.5; page=0
colors=[0x000000,0x555759,0x808385,0xa4a7aa,0xd0d2d4,0xffffff,0xb31368,0xe81818,0xff5900,0xffac00,0xffd000,0xffeb00,0xa4e619,0x26d800,0x00865d,0x00a8c3,0x006bff,0x4200ba]
glyph={
 '0': [[(.48,1),(.2,.94),(.05,.6),(.07,.2),(.3,-.03),(.65,0),(.88,.28),(.9,.7),(.7,.98),(.48,1)]],
 '1': [[(.15,.74),(.5,1),(.48,0)],[(.18,0),(.83,0)]],
 '2': [[(.06,.75),(.2,.96),(.55,1),(.86,.84),(.83,.59),(.08,.02),(.92,0)]],
 '3': [[(.05,.89),(.4,1),(.8,.9),(.85,.68),(.5,.51),(.32,.5),(.78,.4),(.88,.17),(.68,0),(.3,-.04),(.04,.12)]],
 '4': [[(.68,1),(.03,.27),(.95,.27)],[(.72,.78),(.70,-.04)]],
 '5': [[(.87,1),(.13,.97),(.1,.54),(.59,.59),(.85,.4),(.88,.18),(.63,0),(.24,-.02),(.03,.12)]],
 '9': [[(.88,.62),(.63,.49),(.24,.5),(.04,.7),(.17,.94),(.55,1),(.88,.82),(.83,.38),(.65,.04),(.31,-.04)]],
 'D': [[(.02,0),(.5,1.02),(.98,-.01),(.02,0)]],
 '>': [[(.1,.88),(.9,.51),(.08,.08)]],
 '=': [[(.05,.68),(.92,.68)],[(.02,.35),(.91,.35)]],
 '-': [[(.02,.5),(.92,.5)]],
 ':': [[(.45,.72),(.49,.73)],[(.43,.2),(.48,.21)]],
 ',': [[(.52,.11),(.31,-.22)]],
 'F': [[(.07,0),(.11,1),(.95,1)],[(.11,.54),(.76,.53)]],
 'm': [[(.05,0),(.05,.71),(.13,.41),(.37,.73),(.5,.63),(.51,.03),(.54,.4),(.78,.73),(.93,.6),(.95,.01)]],
 'a': [[(.8,.57),(.61,.75),(.26,.7),(.07,.44),(.11,.15),(.36,.04),(.65,.2),(.8,.6),(.78,.02),(.97,.06)]],
 'x': [[(.05,.74),(.9,.03)],[(.86,.75),(.02,.02)]],
}
commands=[]
def cmd(s): commands.append(s)
def stroke(points):
    dense=[]
    for a,b in zip(points,points[1:]):
        n=max(1,round(math.dist(a,b)/2.2))
        dense.extend([(a[0]+(b[0]-a[0])*i/n,a[1]+(b[1]-a[1])*i/n) for i in range(n)])
    dense.append(points[-1]); cmd('stroke '+str(len(dense))+' '+' '.join(f'{x:.2f} {y:.2f}' for x,y in dense))
def text(s,x,y,h=25,w=20):
    for ch in s:
        if ch!=' ':
            for p in glyph[ch]: stroke([(x+a*w,y+b*h) for a,b in p])
        x+=w*1.32 if ch!=' ' else w*.65
def ellipse(x,y,rx,ry): stroke([(x+rx*math.cos(i*math.pi/40),y+ry*math.sin(i*math.pi/40)) for i in range(82)])
def arrow(a,b):
    stroke([a,((a[0]+b[0])*.5+2,(a[1]+b[1])*.5-3),b]);ang=math.atan2(b[1]-a[1],b[0]-a[0])
    stroke([(b[0]-11*math.cos(ang-.48),b[1]-11*math.sin(ang-.48)),b,(b[0]-11*math.cos(ang+.48),b[1]-11*math.sin(ang+.48))])
def save(n):
    global marker,color,width,page
    for s in commands:
        a=s.split()
        if a[0]=='page':page=int(a[1])+2
        elif a[0]=='button' and a[1]=='104':marker=not marker
        elif a[0]=='palette':color=colors[int(a[1])];width=int(a[2])/10
        elif a[0]=='stroke':
            pts=[(float(a[i]),560-float(a[i+1])) for i in range(2,len(a),2)]
            strokes.append((len(strokes)+1,page,int(marker),color,width,pts))
    commands.clear()

cmd('page 1');cmd('button 104');cmd('palette 10 205')
stroke([(50,228),(106,228),(157,228),(208,228)])
cmd('button 104');cmd('palette 16 21')
text('D > 0',299,224,25,20)
text('x = 2,3',568,157,26,20)
arrow((620,200),(616,236));arrow((695,200),(702,236))
stroke([(62,83),(68,87),(77,71),(88,109),(244,110)])
text('25-24',101,77,25,20);text('=1',264,77,25,20)
cmd('palette 7 20');ellipse(616,255,16,14);ellipse(702,255,16,14)
save(1)

cmd('page 2');cmd('button 104');cmd('palette 10 235')
stroke([(50,379),(106,379),(174,379)])
stroke([(50,168),(144,168),(237,168),(311,168)])
cmd('button 104');cmd('palette 16 21')
text('F = ma',88,85,28,23)
ellipse(157,322,67,26)
cmd('palette 7 23')
arrow((742,400),(672,400));text('F',764,392,25,19)
arrow((615,195),(651,166))
save(2)

cmd('page 3');cmd('button 104');cmd('palette 10 260')
stroke([(180,382),(226,382),(276,382)])
stroke([(472,382),(506,382),(540,382)])
cmd('button 104');cmd('palette 16 23')
text('1 : 2',519,207,31,26)
ellipse(237,218,87,84)
cmd('palette 7 22');arrow((391,312),(366,353))
stroke([(487,194),(604,192),(664,196)])
save(3)
data=bytearray(b'PAPER001'+struct.pack('<iiddII',0,0,1.,0.,1,len(strokes)))
for sid,page,kind,color,width,points in strokes:
    data.extend(struct.pack('<QiIIfI',sid,page,kind,color,width,len(points)))
    for x,y in points:data.extend(struct.pack('<dd',x,y))
target=OUT.parents[1]/'packaging'/'tutorial.ink'
target.write_bytes(data)
print('Embedded editable pen/highlighter examples:',len(strokes),target)
