from pathlib import Path
import math
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor

import argparse
parser = argparse.ArgumentParser(description='Regenerate the original embedded tutorial; requires ReportLab.')
parser.add_argument('--cjk-font', type=Path, required=True)
parser.add_argument('--math-font', type=Path, default=Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'))
args = parser.parse_args()
OUT = Path(__file__).resolve().parents[2] / 'packaging'
OUT.mkdir(parents=True, exist_ok=True)
pdfmetrics.registerFont(TTFont('UI', str(args.cjk_font), subfontIndex=0))
pdfmetrics.registerFont(TTFont('Math', str(args.math_font)))
c = canvas.Canvas(str(OUT / 'tutorial.pdf'), pagesize=(900, 560), pageCompression=1, invariant=1)
c.setTitle('PDF view 功能教程 — 阅读、触控与数理化批注练习')
c.setAuthor('PDF view project')
INK='#18243a'; MUTED='#66758a'; BLUE='#315bc9'; GRID='#e8edf4'
def txt(x,y,s,size=15,font='UI',color=INK):
    c.setFont(font,size); c.setFillColor(HexColor(color)); c.drawString(x,y,s)
def line(x1,y1,x2,y2,color=GRID,width=1):
    c.setStrokeColor(HexColor(color)); c.setLineWidth(width); c.line(x1,y1,x2,y2)
def path(points,color=BLUE,width=1.7):
    c.setStrokeColor(HexColor(color)); c.setLineWidth(width)
    p=c.beginPath(); p.moveTo(*points[0])
    for a in points[1:]: p.lineTo(*a)
    c.drawPath(p)
def arrow(a,b,color=BLUE,width=1.5):
    line(*a,*b,color,width)
    ang=math.atan2(b[1]-a[1],b[0]-a[0])
    for d in [-.48,.48]: line(*b,b[0]-8*math.cos(ang+d),b[1]-8*math.sin(ang+d),color,width)
def header(n,title,sub):
    c.setFillColor(HexColor('#f3f6fc')); c.rect(0,460,900,100,stroke=0,fill=1)
    txt(46,516,f'{n:02d}',24,'Math',BLUE)
    txt(100,515,title,25)
    txt(100,483,sub,12,color=MUTED)
    line(46,444,854,444)
    txt(46,26,'PDF view   /   功能教程 · 所有示例均可直接练习批注',10,color=MUTED)
def sqrt(x,y,rad,size=22):
    txt(x+19,y,rad,size,'Math')
    w=pdfmetrics.stringWidth(rad,'Math',size)
    path([(x,y+8),(x+5,y+11),(x+10,y-2),(x+17,y+size+3),(x+23+w,y+size+3)],INK,1.45)
    return x+23+w

def lesson(key,n,title,sub,sections,footer):
    c.bookmarkPage(key); c.addOutlineEntry(title,key,0)
    header(n,title,sub)
    for index,(heading,body) in enumerate(sections):
        col=index%2; row=index//2; x=48+col*425; y=403-row*150
        txt(x,y,heading,18,color=BLUE)
        for j,s in enumerate(body):txt(x,y-31-j*25,s,13)
    txt(48,62,footer,12,color=MUTED)
    c.showPage()

lesson('reading',1,'从这里开始：阅读与触控','离线教程 · 不需要 Word · 从左侧目录跳转到想练习的章节',[
 ('打开与多标签',['点文件夹图标，或把 PDF / DOC / DOCX 拖进窗口。','点击顶部标签切换；默认 Ctrl+Tab 切换标签。','Ctrl+O 打开文档，Ctrl+W 关闭当前标签。','关闭标签不会删除原文档或已保存的批注。']),
 ('页码、目录与缩略图',['左侧目录按章节跳转；方格图标切换缩略图。','在顶部页码框输入页数，按 Enter 跳转。','加减号调节缩放，宽度图标让页面适应窗口。']),
 ('单指阅读，双指缩放',['“绘制”关闭时，单指拖动页面进行阅读。','双指移动和捏合可平移、缩放文档。','“绘制”打开后，单指可以直接书写。']),
 ('查找、旋转与全屏',['放大镜查找文字，F3 / Shift+F3 前后跳转结果。','弯箭头旋转页面；右上角斜箭头进入全屏。','默认 Ctrl+F 查找，Ctrl+0 适应宽度，F11 全屏。','Ctrl+Shift+C 复制本页文字；扫描件需要文字层。'])
],'下一步：阅读“画笔、荧光笔与批注”，再到后面的数学、物理、化学示例动手练习。')
lesson('annotation',2,'画笔、荧光笔与批注','继续使用顶部现有工具 · 本教程中的圈画和高亮是可编辑的真实批注',[
 ('画笔开关与颜色粗细',['点击“绘制”开启，再点击一次回到阅读。','画笔旁的小箭头打开颜色和粗细面板。','用手指、鼠标或触控笔书写；根号横线要写完整。']),
 ('荧光笔',['点击荧光笔图标，沿文字拖动即可透明高亮。','再点击荧光笔图标可切回普通画笔。','颜色与粗细分别保留，可以随时切换。']),
 ('橡皮、撤销与重做',['橡皮擦除碰到的整条笔迹，文字不会被删除。','撤销和重做按钮可以反悔刚才的批注操作。','默认 Ctrl+Z 撤销，Ctrl+Y 重做。']),
 ('在数理化示例上练习',['数学页：圈出两个实根，补写完整的根号。','物理页：画出回复力方向，强调能量方程。','化学页：标记配平系数，写出物质的量之比。'])
],'试一试：切换到“数学练习”，擦掉一个示例圆圈，再撤销恢复，然后添加你自己的笔记。')
lesson('documents',3,'保存、Word 与个性化设置','示例中的快捷键为默认值；当前绑定请以“设置 → 快捷键”为准',[
 ('自动保存批注',['笔迹和阅读位置自动保存在这台电脑。','磁盘图标或 Ctrl+S 可手动保存一次。','原始文档不变；重新打开教程可继续上次练习。','分享原始文件不会携带这里的批注。']),
 ('DOC / DOCX 在软件内阅读',['需要已安装桌面 Microsoft Word 或兼容 WPS。','原生模式直接显示 Office 排版的页面。','与 PDF 使用同一套画笔、荧光笔和橡皮。']),
 ('可选 PDF 兼容模式',['在“设置 → Word / WPS”选择 PDF 兼容阅读。','这是显式的本机转换选项，仍在软件内阅读。','公式效果取决于原文件、字体与本机 Office。']),
 ('齿轮设置',['可调整自动侧边栏、会话恢复、交互虚化。','滑动灵敏度控制阅读时的滚动和拖动速度。','快捷键可重新绑定；“功能教程”随时回来练习。'])
],'下面三页沿用数理化演示：公式和图形是矢量内容，预置圈画与荧光笔仍能擦除和修改。')

c.bookmarkPage('math'); c.addOutlineEntry('数学练习 · 方程与函数图','math',0)
header(1,'数学：二次方程与函数图','嵌入字体 · 完整根号 · 上下标 · 矢量曲线')
txt(48,405,'一般式',12,color=MUTED)
txt(48,371,'ax² + bx + c = 0',24,'Math')
txt(48,325,'x =',24,'Math')
txt(113,341,'−b ±',22,'Math')
end=sqrt(182,341,'b² − 4ac',22)
line(110,330,end+3,330,INK,1.2)
txt(211,302,'2a',22,'Math')
txt(48,253,'判别式',12,color=MUTED)
txt(48,219,'Δ = b² − 4ac',25,'Math')
txt(48,161,'例题',12,color=MUTED)
txt(48,128,'x² − 5x + 6 = 0',23,'Math')
txt(491,412,'函数图像  y = x² − 5x + 6',15)
gx=lambda x: 530+(x-1)*86
gy=lambda y: 255+y*46
for x in range(1,5): line(gx(x),171,gx(x),389)
for y in [-1,0,1,2]: line(515,gy(y),828,gy(y))
arrow((515,255),(837,255),MUTED,.9); arrow((530,171),(530,395),MUTED,.9)
txt(835,238,'x',12,'Math',MUTED); txt(513,392,'y',12,'Math',MUTED)
for x in [1,2,3,4]: txt(gx(x)-4,236,str(x),10,'Math',MUTED)
path([(gx(x),gy((x-2)*(x-3))) for x in [1.13+i*.014 for i in range(196)]],BLUE,2)
for x in [2,3]:
    c.setFillColor(HexColor(BLUE)); c.circle(gx(x),255,3,stroke=0,fill=1)
txt(532,111,'与 x 轴的两个交点，对应方程的两个实根。',12,color=MUTED)
c.showPage()

c.bookmarkPage('physics'); c.addOutlineEntry('物理练习 · 弹簧振子','physics',0)
header(2,'物理：弹簧振子与简谐运动','希腊字母 · 能量方程 · 受力示意图 · 波形')
txt(48,405,'回复力',12,color=MUTED)
txt(48,370,'F = −kx',28,'Math')
txt(48,312,'ω =',27,'Math'); sqrt(117,312,'k / m',24)
txt(48,256,'T = 2π / ω',26,'Math')
txt(48,194,'机械能守恒',12,color=MUTED)
txt(48,160,'E = ½mv² + ½kx²',25,'Math')
txt(495,411,'理想弹簧振子',15)
line(517,337,821,337,MUTED,1.3)
line(521,340,521,389,MUTED,3)
for y in range(344,390,8): line(510,y-7,521,y,MUTED,.8)
spring=[(522,365),(546,365)]
for i in range(19): spring.append((550+i*6,365+(-11 if i%2==0 else 11)))
spring.extend([(666,365),(687,365)]); path(spring,BLUE,1.8)
c.setStrokeColor(HexColor(BLUE)); c.setFillColor(HexColor('#eff3ff')); c.roundRect(687,339,64,52,5,stroke=1,fill=1)
txt(707,357,'m',20,'Math',BLUE)
line(656,330,656,399,'#9cabc1',.8)
txt(634,313,'O',11,'Math',MUTED)
arrow((657,300),(719,300),MUTED,1)
txt(683,282,'x',13,'Math',MUTED)
txt(495,248,'位移随时间变化',13)
for xx in range(527,829,30): line(xx,105,xx,228)
for yy in range(110,229,28): line(517,yy,830,yy)
arrow((517,166),(837,166),MUTED,.9); arrow((527,104),(527,235),MUTED,.9)
txt(835,149,'t',12,'Math',MUTED); txt(510,228,'x',12,'Math',MUTED)
path([(527+i,166+48*math.cos(i/41)) for i in range(299)],BLUE,1.8)
c.showPage()

c.bookmarkPage('chemistry'); c.addOutlineEntry('化学练习 · 反应与结构','chemistry',0)
header(3,'化学：反应方程式与分子结构','化学下标 · 配平系数 · 结构图 · 矢量细线')
txt(48,413,'酸碱中和反应',13,color=MUTED)
txt(48,371,'H₂SO₄ + 2NaOH → Na₂SO₄ + 2H₂O',29,'Math')
txt(48,316,'分子结构',13,color=MUTED)
cx,cy,r=237,218,66
pts=[(cx+r*math.cos(math.pi/6+i*math.pi/3),cy+r*math.sin(math.pi/6+i*math.pi/3)) for i in range(6)]
path(pts+[pts[0]],INK,1.8)
for i in [0,2,4]:
    a,b=pts[i],pts[(i+1)%6]
    # Double bonds shortened and inset toward the ring centre.
    aa=(cx+(a[0]-cx)*.80,cy+(a[1]-cy)*.80); bb=(cx+(b[0]-cx)*.80,cy+(b[1]-cy)*.80)
    line(*aa,*bb,INK,1.45)
txt(204,115,'苯',16)
txt(238,114,'C₆H₆',20,'Math')
txt(485,306,'物质的量之比',15)
txt(485,267,'n(H₂SO₄) : n(NaOH)',22,'Math')
txt(485,148,'观察配平系数，确定反应物的比例。',12,color=MUTED)
txt(485,114,'结构式中的双键以平行细线显示。',12,color=MUTED)
c.save()
print(OUT / 'tutorial.pdf')
