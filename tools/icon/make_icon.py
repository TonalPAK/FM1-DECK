# FM1 DECK launcher icon: a micro panel (background layer) behind a colour FM wave (foreground layer).
# Usage: python tools/icon/make_icon.py <preview_dir> app/src/main/res
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter
OUT = sys.argv[1]                     # preview dir
RES = sys.argv[2] if len(sys.argv) > 2 else None   # app/src/main/res or None
SS = 4                                # supersampling
N = 432 * SS                          # xxxhdpi adaptive layer (108 dp)

BODY=(29,29,31); TRAY=(20,20,21); PART=(42,42,45); EDGE=(58,58,62); LIT=(255,244,214); DIMB=(58,57,52)
KEY=(12,12,13); SLIT=(119,119,123)
GRAD=[(46,197,255),(79,107,255),(162,59,255),(255,46,126),(255,122,0),(255,212,0)]

def s(v): return int(v * N / 432)
K = 0.8   # the panel drawn at 80 % around the centre (216, 230): wholly inside the visible circle
def S(v, c): return s(c + (v - c) * K)
def X(v): return S(v, 216)
def Y(v): return S(v, 230)
def lerp(a,b,t): return tuple(int(a[i]+(b[i]-a[i])*t) for i in range(3))
def grad(t):
    t=max(0,min(1,t))*(len(GRAD)-1); i=min(int(t),len(GRAD)-2); return lerp(GRAD[i],GRAD[i+1],t-i)

def background():
    im = Image.new("RGB",(N,N),BODY); d=ImageDraw.Draw(im)
    def rr(x0,y0,x1,y1,r,**kw): d.rounded_rectangle((X(x0),Y(y0),X(x1),Y(y1)),int(s(r)*K),**kw)
    def el(cx,cy,r,**kw): d.ellipse((X(cx-r),Y(cy-r),X(cx+r),Y(cy+r)),**kw)
    def ln(x0,y0,x1,y1,w,fill): d.line((X(x0),Y(y0),X(x1),Y(y1)),fill=fill,width=int(s(w)*K))
    # system block: 2x2 knobs + OCT island
    for (cx,cy) in [(84,128),(122,128),(84,170),(122,170)]:
        el(cx,cy,15,fill=TRAY); el(cx,cy,11,fill=PART,outline=EDGE,width=s(1))
    ln(84,128,92,121,2.4,LIT)
    rr(66,198,140,222,6,fill=TRAY)
    for x0 in (70,105): rr(x0,202,x0+31,218,3,fill=DIMB)
    # buttons 2x6 + knobs row
    bx0,by0,bw,bh,g = 152,108,19,17,4
    for r in range(2):
        for c in range(6):
            x=bx0+c*(bw+g); y=by0+r*(bh+g)
            col = LIT if (r==1 and c==0) else ((95,224,138) if (r==1 and c==4) else DIMB)
            rr(x,y,x+bw,y+bh,3.5,fill=col)
    for c in range(4):
        cx=166+c*33; cy=196
        el(cx,cy,12,fill=TRAY); el(cx,cy,9,fill=PART,outline=EDGE,width=s(1))
    # screen with a little life on it
    rr(296,104,372,222,9,fill=(8,8,9)); rr(302,112,366,214,4,fill=(16,14,26))
    for i,(h,c) in enumerate([(30,(255,122,0)),(52,(162,59,255)),(22,(46,197,255)),(40,(255,46,126))]):
        rr(308+i*14,204-h,317+i*14,204,2,fill=c)
    # keyboard: two rows of capsules
    rr(46,244,386,366,12,fill=TRAY)
    whites=13; ww=(380-52)/whites
    for i in range(whites):
        x=52+i*ww+2; rr(x,306,x+ww-4,360,9,fill=KEY,outline=(40,40,43),width=s(1))
        ln(x+(ww-4)/2,318,x+(ww-4)/2,348,2.4,SLIT)
    for i in range(whites-1):
        if i % 7 in (2,6): continue
        x=52+(i+0.5)*ww+3; rr(x,250,x+ww-6,300,9,fill=KEY,outline=(40,40,43),width=s(1))
        ln(x+(ww-6)/2,262,x+(ww-6)/2,290,2.4,SLIT)
    veil = Image.new("RGB",(N,N),(0,0,0))
    return Image.blend(im,veil,0.30)

def foreground():
    im = Image.new("RGBA",(N,N),(0,0,0,0))
    # an FM wave: carrier with a modulator, under a swelling envelope (a voice playing), in the safe zone 66 dp
    x0,x1,cy,amp = 104,328,216,58
    pts=[]
    steps=2600
    for k in range(steps+1):
        t=k/steps
        env = math.sin(math.pi*t)**0.8
        y = math.sin(2*math.pi*(5.0*t) + 2.2*math.sin(2*math.pi*(2.5*t)))
        pts.append((x0+(x1-x0)*t, cy - amp*env*y, t))
    # glow
    glow = Image.new("RGBA",(N,N),(0,0,0,0)); g=ImageDraw.Draw(glow)
    for p in pts:
        c=grad(p[2]); r=9; g.ellipse((s(p[0]-r),s(p[1]-r),s(p[0]+r),s(p[1]+r)),fill=c+(150,))
    glow = glow.filter(ImageFilter.GaussianBlur(s(9)))
    im.alpha_composite(glow)
    d=ImageDraw.Draw(im)
    for p in pts:
        c=grad(p[2]); r=4.3; d.ellipse((s(p[0]-r),s(p[1]-r),s(p[0]+r),s(p[1]+r)),fill=c+(255,))
    for p in (pts[0],pts[-1]):
        c=grad(p[2]); r=4.2; d.ellipse((s(p[0]-r),s(p[1]-r),s(p[0]+r),s(p[1]+r)),fill=c+(255,))
    return im

bg = background(); fg = foreground()
def down(im, px): return im.resize((px,px),Image.LANCZOS)

full = bg.convert("RGBA"); full.alpha_composite(fg)
def legacy(px, shape):
    # what a legacy launcher shows: the middle 72/108 of the layers, masked
    crop = int(N*18/108)
    im = full.crop((crop,crop,N-crop,N-crop)).resize((px*SS,px*SS),Image.LANCZOS)
    m = Image.new("L",im.size,0); md=ImageDraw.Draw(m)
    if shape=="round": md.ellipse((0,0,im.width-1,im.height-1),fill=255)
    else: md.rounded_rectangle((0,0,im.width-1,im.height-1),int(im.width*0.22),fill=255)
    out=Image.new("RGBA",im.size,(0,0,0,0)); out.paste(im,(0,0),m)
    return out.resize((px,px),Image.LANCZOS)

os.makedirs(OUT,exist_ok=True)
legacy(512,"square").save(os.path.join(OUT,"preview-square.png"))
legacy(512,"round").save(os.path.join(OUT,"preview-round.png"))
if RES:
    for dens,f in [("mdpi",1),("hdpi",1.5),("xhdpi",2),("xxhdpi",3),("xxxhdpi",4)]:
        dd=os.path.join(RES,f"mipmap-{dens}"); os.makedirs(dd,exist_ok=True)
        down(bg,int(108*f)).save(os.path.join(dd,"ic_launcher_background.png"),optimize=True)
        down(fg,int(108*f)).save(os.path.join(dd,"ic_launcher_foreground.png"),optimize=True)
        legacy(int(48*f),"square").save(os.path.join(dd,"ic_launcher.png"),optimize=True)
        legacy(int(48*f),"round").save(os.path.join(dd,"ic_launcher_round.png"),optimize=True)
    legacy(512,"square").save(os.path.join(OUT,"icon-512.png"))
print("ok")
