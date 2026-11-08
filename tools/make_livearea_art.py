import random, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter
F='/mnt/skills/examples/canvas-design/canvas-fonts/'
RED=(122,30,34); GOLD=(240,190,20); INK=(92,64,40)
def parchment(w,h,seed=1):
    random.seed(seed)
    im=Image.new('RGB',(w,h))
    px=im.load()
    for y in range(h):
        for x in range(w):
            d=math.hypot((x-w/2)/w,(y-h/2)/h)
            b=1-0.35*d*d
            n=random.uniform(-6,6)
            px[x,y]=(int(236*b+n),int(220*b+n),int(170*b+n*0.8))
    # Fasern
    dr=ImageDraw.Draw(im,'RGBA')
    for _ in range(w*h//900):
        x=random.uniform(0,w); y=random.uniform(0,h); a=random.uniform(0,math.pi); l=random.uniform(4,18)
        dr.line([(x,y),(x+l*math.cos(a),y+l*math.sin(a))],fill=(150,120,70,22),width=1)
    return im.filter(ImageFilter.GaussianBlur(0.6))
def title(word,size,color,outline=None,squeeze=0.72,font='Gloock-Regular.ttf'):
    f=ImageFont.truetype(F+font,size)
    bb=f.getbbox(word); tw,th=bb[2]-bb[0]+8,bb[3]-bb[1]+8
    im=Image.new('RGBA',(tw,th),(0,0,0,0)); d=ImageDraw.Draw(im)
    if outline: d.text((4-bb[0],4-bb[1]),word,font=f,fill=color,stroke_width=max(1,size//40),stroke_fill=outline)
    else: d.text((4-bb[0],4-bb[1]),word,font=f,fill=color)
    return im.resize((int(tw*squeeze),th),Image.LANCZOS)
def guides(dr,box,seed):
    # Konstruktionslinien wie auf einer Bauzeichnung
    random.seed(seed); x0,y0,x1,y1=box
    for _ in range(5):
        y=random.uniform(y0,y1); dr.line([(x0-10,y),(x1+10,y)],fill=(122,30,34,70),width=1)
    for _ in range(3):
        cx=random.uniform(x0,x1); cy=random.uniform(y0,y1); r=random.uniform(10,(y1-y0)*0.6)
        dr.ellipse([cx-r,cy-r,cx+r,cy+r],outline=(122,30,34,55),width=1)
def sketch(dr,ox,oy,s,seed=3):
    random.seed(seed)
    def L(pts,c=INK,a=200,w=1):
        q=[(ox+x*s+random.uniform(-.6,.6),oy+y*s+random.uniform(-.6,.6)) for x,y in pts]
        dr.line(q,fill=c+(a,),width=w)
    def P(pts,c,a=110):
        dr.polygon([(ox+x*s,oy+y*s) for x,y in pts],fill=c+(a,))
    # Pyramide
    P([(0,100),(55,40),(110,100)],(214,140,90)); L([(0,100),(55,40),(110,100),(0,100)])
    for i in range(1,6): L([(55-11*i,40+12*i),(55+11*i,40+12*i)],a=90)
    # Tempel
    P([(120,48),(190,30),(260,48)],(225,215,195),150); L([(120,48),(190,30),(260,48),(120,48)],w=2)
    P([(125,52),(255,52),(255,100),(125,100)],(232,224,205),130)
    L([(122,52),(258,52)],w=2); L([(118,100),(262,100)],w=2)
    for i in range(8):
        x=132+i*17; L([(x,54),(x,98)]); L([(x+6,54),(x+6,98)],a=120)
    # Hochhaeuser im Hintergrund
    for i,(x,h) in enumerate([(275,70),(300,95),(330,60),(355,85)]):
        P([(x,100),(x,100-h),(x+22,100-h),(x+22,100)],(200,175,140),70)
        L([(x,100),(x,100-h),(x+22,100-h),(x+22,100)],a=110)
        for y in range(int(100-h)+6,100,8): L([(x+4,y),(x+18,y)],a=60)
    # Segelschiff
    P([(150,118),(230,118),(220,132),(160,132)],(150,95,55),170); L([(150,118),(230,118),(220,132),(160,132),(150,118)],w=2)
    L([(190,118),(190,58)],w=2)
    P([(170,64),(210,62),(214,108),(166,110)],(250,246,236),220); L([(170,64),(210,62),(214,108),(166,110),(170,64)])
    L([(172,86),(212,85)],a=90); L([(176,74),(208,73)],a=70)
    for i in range(6): L([(140+i*18,136),(150+i*18,134),(160+i*18,136)],c=(60,90,120),a=120)


def logo(img,x,y,sc,dr=None):
    ii=title('I I',int(190*sc),GOLD,outline=(150,110,0),squeeze=0.95)
    c=title('C',int(150*sc),RED,outline=(70,12,16),squeeze=0.9)
    img.paste(ii,(x,y-int(30*sc)),ii)                                   # II hinten
    img.paste(c,(x+(ii.width-c.width)//2,y+(ii.height-c.height)//2-int(30*sc)),c)   # C mittig davor
    v=title('Vita',int(130*sc),RED,squeeze=0.85); vx=x+ii.width+int(20*sc); img.paste(v,(vx,y),v)
    return vx+v.width-x
# bg 840x500
bg=parchment(840,500,1); d=ImageDraw.Draw(bg,'RGBA')
guides(d,(120,40,720,160),5)
w=logo(bg,0,-1000,1.0); logo(bg,(840-w)//2,46,1.0)
sketch(d,116,272,1.6)
bg.save('/home/claude/art/bg.png')
st=parchment(280,158,2); d=ImageDraw.Draw(st,'RGBA')
w=logo(st,0,-1000,0.42); logo(st,(280-w)//2,14,0.42)
sketch(d,40,62,0.62,4)
st.save('/home/claude/art/startup.png')
ic=parchment(128,128,3); d=ImageDraw.Draw(ic,'RGBA')
guides(d,(10,10,118,118),7)
ii=title('I I',92,GOLD,outline=(140,100,0),squeeze=0.95); c=title('C',72,RED,outline=(70,12,16),squeeze=0.9)
x0=(128-ii.width)//2; ic.paste(ii,(x0,0),ii); ic.paste(c,(x0+(ii.width-c.width)//2,(ii.height-c.height)//2),c)
v=title('Vita',34,RED,squeeze=0.9); ic.paste(v,((128-v.width)//2,86),v)
ic.save('/home/claude/art/icon0.png')
