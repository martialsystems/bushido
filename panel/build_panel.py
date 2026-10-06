"""SQ-10 style panel generator. PROVISIONAL layout (built from documented features, not from a photo).
Writes assets/sq10_bg.png (+@2x, no live parts), assets/sq10_preview.png (all parts at defaults) and assets/sq10_layout.json."""
import math,json,io,random,os,re
import numpy as np,cairosvg
from PIL import Image,ImageFont,ImageDraw
from scipy.ndimage import gaussian_filter
FP="/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
FT,FL,FS,LST,LSL=12,11,9,.8,.4
fm={s:ImageFont.truetype(FP,s*8) for s in (FT,FL,FS)}
tw=lambda s,z:fm[z].getlength(s)/8+(LST if z==FT else LSL)*(len(s)-1)
W,H,SC,M=1600,640,2,14; AVAIL=W-2*M; INK="#dcd6c2"; GOLD="#c29f4c"
CH_Y=12; TI_Y=50; ROWY=[130,204,278]; LAMPY=90; TRIGY=372; KR=14; RING=21; JR=9; SWR=11
# ---------------- module description (ids are SECTION:LABEL) ----------------
COLS=[("CH",164)]+[(str(i),None) for i in range(1,13)]+[("CLOCK",96),("MODE",96),("INPUTS",96),("OUTPUTS",96),("MIXER",96)]
fixed=sum(w for _,w in COLS if w); stepw=(AVAIL-fixed)/12
P=[];LIVE=[];texts=[];circ=[];controls=[];leds=[];jacks=[];cols=[];CUR=[None,None]
def T(cx,y,s,z,fill=INK):
    w=tw(s,z); x0=cx-w/2; texts.append((x0,y-z*.74,x0+w,y+z*.2,s,CUR[0],CUR[1]))
    P.append(f'<text x="{x0:.1f}" y="{y}" font-size="{z}" font-weight="700" letter-spacing="{LST if z==FT else LSL}" fill="{fill}">{s.replace("&","&amp;")}</text>')
def ticks(cx,cy,r,a0=-135,a1=135,n=11):
    for i in range(n):
        a=math.radians(a0+(a1-a0)*i/(n-1)-90); l=4 if i in (0,n-1,(n-1)//2) else 2.5
        P.append(f'<line x1="{cx+(r+4)*math.cos(a):.1f}" y1="{cy+(r+4)*math.sin(a):.1f}" x2="{cx+(r+4+l)*math.cos(a):.1f}" y2="{cy+(r+4+l)*math.sin(a):.1f}" stroke="{INK}" stroke-width="1.1"/>')
def knob_body(cx,cy,r,ang):
    a=math.radians(ang-90); ex,ey=math.cos(a),math.sin(a)
    return (f'<g class="live"><circle cx="{cx+1}" cy="{cy+2}" r="{r+4}" fill="#000" opacity=".45"/><circle cx="{cx}" cy="{cy}" r="{r+3}" fill="#08080a" stroke="#000" stroke-width=".8"/>'
      f'<circle cx="{cx}" cy="{cy}" r="{r+1.2}" fill="none" stroke="#3c3c3f" stroke-width="2.4" stroke-dasharray=".8 1.6" opacity=".75"/>'
      f'<circle cx="{cx}" cy="{cy}" r="{r-.3}" fill="url(#kb)" stroke="#000" stroke-width=".8"/><circle cx="{cx}" cy="{cy}" r="{r*.8:.1f}" fill="url(#kt)" stroke="#050505" stroke-width=".8"/>'
      f'<ellipse cx="{cx-r*.28:.1f}" cy="{cy-r*.32:.1f}" rx="{r*.45:.1f}" ry="{r*.28:.1f}" fill="url(#ks)" transform="rotate(-35 {cx} {cy})"/>'
      f'<line x1="{cx+ex*r*.1:.1f}" y1="{cy+ey*r*.1:.1f}" x2="{cx+ex*(r-1.5):.1f}" y2="{cy+ey*(r-1.5):.1f}" stroke="#f1ede0" stroke-width="2.4"/></g>')
def knob(sec,lab,cx,cy,default,label=True,text=None):
    controls.append(dict(id=f"{sec}:{lab}",kind="knob",cx=round(cx,1),cy=cy,r=KR,default=default,angles=[-135,135],hit=[round(cx-RING,1),cy-RING,2*RING,2*RING]))
    circ.append((cx,cy,RING,"knob "+lab)); ticks(cx,cy,KR)
    if label: T(cx,cy+KR+14,text or lab,FL)
    P.append(knob_body(cx,cy,KR,-135+270*default))
def switch(sec,lab,cx,cy,marks,angles,default,rad=28,text=None):
    controls.append(dict(id=f"{sec}:{lab}",kind="switch",cx=round(cx,1),cy=cy,r=SWR,default=default,positions=len(marks),angles=angles,marks=marks,hit=[round(cx-22,1),cy-22,44,44]))
    circ.append((cx,cy,SWR+3,"switch "+lab))
    for m,a in zip(marks,angles):
        P.append(f'<line x1="{cx+15*math.sin(math.radians(a)):.1f}" y1="{cy-15*math.cos(math.radians(a)):.1f}" x2="{cx+18*math.sin(math.radians(a)):.1f}" y2="{cy-18*math.cos(math.radians(a)):.1f}" stroke="{INK}" stroke-width="1.4"/>')
        T(cx+rad*math.sin(math.radians(a)),cy-rad*math.cos(math.radians(a))+4,m,FL)
    T(cx,cy+SWR+17,text or lab,FL)
    k=round(default*(len(marks)-1)); P.append(knob_body(cx,cy,SWR,angles[k]))
def button(sec,lab,cx,cy):
    controls.append(dict(id=f"{sec}:{lab}",kind="button",cx=round(cx,1),cy=cy,r=12,default=0,hit=[round(cx-14,1),cy-14,28,28]))
    circ.append((cx,cy,15,"button "+lab)); T(cx,cy+12+14,lab,FL)
    P.append(f'<rect x="{cx-15:.1f}" y="{cy-15}" width="30" height="30" rx="3" fill="#050505" stroke="#2a2a2c"/>'
             f'<g class="live"><rect x="{cx-12:.1f}" y="{cy-12}" width="24" height="24" rx="3" fill="url(#bs)" stroke="#6f6a5a" stroke-width=".8"/><rect x="{cx-9:.1f}" y="{cy-10}" width="18" height="17" rx="2" fill="url(#bd)"/></g>')
def led(id_,cx,cy,r=4.5):
    leds.append(dict(id=id_,cx=round(cx,1),cy=cy,r=r)); circ.append((cx,cy,r+2.5,"led "+id_))
    P.append(f'<circle cx="{cx:.1f}" cy="{cy}" r="{r+2}" fill="#050505" stroke="#2a2a2c"/><g class="live"><circle cx="{cx:.1f}" cy="{cy}" r="{r}" fill="#4a0c08"/></g>')
def lamp(id_,cx,cy):   # step lamp: one per step, shared by rows A, B and C; a chrome bezel so it reads as the playhead, not a dot
    r=5.5; leds.append(dict(id=id_,cx=round(cx,1),cy=cy,r=r)); circ.append((cx,cy,r+3.5,"lamp "+id_))
    P.append(f'<circle cx="{cx:.1f}" cy="{cy}" r="{r+3.5}" fill="url(#js)" stroke="#2a2a2c" stroke-width=".9"/><circle cx="{cx:.1f}" cy="{cy}" r="{r+1}" fill="#050505"/>'
             f'<g class="live"><circle cx="{cx:.1f}" cy="{cy}" r="{r}" fill="#4a0c08"/></g>')
def toggle(sec,lab,cx,cy,marks,default,text=None):   # two-position bat toggle (drawn as a switch, not a pot); lever points at the active mark
    controls.append(dict(id=f"{sec}:{lab}",kind="switch",style="toggle",cx=round(cx,1),cy=cy,r=SWR,default=default,positions=2,angles=[-50,50],marks=marks,hit=[round(cx-22,1),cy-22,44,44]))
    circ.append((cx,cy,SWR+3,"toggle "+lab))
    P.append(f'<rect x="{cx-15:.1f}" y="{cy-8}" width="30" height="16" rx="3" fill="#050505" stroke="#2a2a2c"/><rect x="{cx-10:.1f}" y="{cy-2}" width="20" height="4" rx="2" fill="#000"/>'
             f'<circle cx="{cx:.1f}" cy="{cy}" r="5" fill="url(#jn)" stroke="#111" stroke-width=".8"/>')
    for m,dx in zip(marks,(-1,1)): T(cx+dx*18,cy-17,m,FL)
    T(cx,cy+SWR+17,text or lab,FL)
    lx=cx+(12 if default>.5 else -12); P.append(f'<g class="live"><line x1="{cx:.1f}" y1="{cy}" x2="{lx:.1f}" y2="{cy-2}" stroke="#d8d8d2" stroke-width="3.2" stroke-linecap="round"/><circle cx="{lx:.1f}" cy="{cy-2}" r="3.6" fill="url(#js)"/></g>')
def jack(sec,lab,d,cx,cy):
    jacks.append(dict(id=f"{sec}:{lab}",section=sec,label=lab,dir=d,x=round(cx,1),y=cy,radius=JR,hit=[round(cx-11,1),cy-11,22,22])); circ.append((cx,cy,JR+1.5,"jack "+lab))
    P.append(f'<circle cx="{cx:.1f}" cy="{cy}" r="{JR+1.5}" fill="#000" opacity=".55"/><circle cx="{cx:.1f}" cy="{cy}" r="{JR}" fill="url(#js)" stroke="#2a2a2c" stroke-width=".9"/>'
             f'<circle cx="{cx:.1f}" cy="{cy}" r="{JR-2.6}" fill="url(#jn)"/><circle cx="{cx:.1f}" cy="{cy}" r="{JR-5}" fill="#030303"/>')
    T(cx,round(cy+JR+13,1),lab,FL)
x=M
for title,w in COLS:
    w=w or stepw; cx=x+w/2; CUR[:]=[x+3,x+w-3]; cols.append(dict(title=title,x=round(x,1),w=round(w,1))); T(cx,TI_Y+15,title,FT)
    if title=="CH":
        for r,ch in enumerate("ABC"):
            cy=ROWY[r]; T(x+20,cy+5,ch,FT)
            if ch!="C":
                led("CH:"+ch,x+20,cy+22); knob("CH","PORTA "+ch,x+64,cy,0.0,text="PORTA"); switch("CH","RANGE "+ch,x+120,cy,["1V","5V"],[-50,50],1.0,text="RANGE")
            else: toggle("CH","C MODE",x+120,cy,["CV","TIME"],0.0)
    elif title.isdigit():
        for r,ch in enumerate("ABC"): knob(ch,title,cx,ROWY[r],[.5,.42,.58,.35,.66,.5,.3,.72,.45,.55,.38,.62][(int(title)+r*5)%12],label=False)
        lamp("STEP:"+title,cx,LAMPY); jack(title,"TRIG","out",cx,TRIGY)
    elif title=="CLOCK":
        knob("CLOCK","TEMPO",cx,ROWY[0],.5); switch("CLOCK","SOURCE",cx,ROWY[1],["INT","EXT"],[-50,50],0.0)
        jack("CLOCK","CLOCK","in",cx,282); jack("CLOCK","TEMPO CV","in",cx,342)
    elif title=="MODE":
        switch("MODE","MODE",cx,ROWY[0],["A","A+B","ALT"],[-60,0,60],.5,rad=30)
        for i,l in enumerate(["A · LOOP 12","A+B · LOOP 24","ALT · SWAP A/B"]): T(cx,172+i*11,l,FS)   # every mode loops until STOP
        for i,b in enumerate(["START/STOP","STEP","RESET"]): button("MODE",b,cx,224+i*58)
    elif title=="INPUTS":
        for i,l in enumerate(["START/STOP","STEP","RESET"]): jack("INPUTS",l,"in",cx,TI_Y+28+JR+1.5+i*60)
    elif title=="OUTPUTS":
        for i,l in enumerate(["CV A","GATE A","CV B","GATE B","CV C"]): jack("OUTPUTS",l,"out",cx,TI_Y+28+JR+1.5+i*60)
    elif title=="MIXER":
        knob("MIXER","LEVEL 1",cx,ROWY[0],.7); knob("MIXER","LEVEL 2",cx,ROWY[1],.7)
        jack("MIXER","IN 1","in",cx,272); jack("MIXER","IN 2","in",cx,322); jack("MIXER","OUT","out",cx,372)
    x+=w
FR_T=TI_Y-6; FR_B=max(t[3] for t in texts)+12; rules=[c["x"] for c in cols[1:]]
# ---------------- checks ----------------
bad=[]
for i,a in enumerate(texts):
    if a[5] is not None and not (a[5]<=a[0] and a[2]<=a[6]): bad.append(("text outside column",a[4]))
    for b in texts[i+1:]:
        if a[0]<b[2]+2 and b[0]<a[2]+2 and a[1]<b[3]+1 and b[1]<a[3]+1: bad.append(("text/text",a[4],b[4]))
    for cx,cy,r,n in circ:
        px=min(max(cx,a[0]),a[2]); py=min(max(cy,a[1]),a[3])
        if (px-cx)**2+(py-cy)**2<(r-(5 if n.startswith("knob") else 0))**2: bad.append(("text/ctl",a[4],n))
    for r in rules+[M,W-M]:
        if a[0]-2<r<a[2]+2 and a[3]>FR_T and a[1]<FR_B: bad.append(("text/rule",a[4],r))
for i,(cx,cy,r,n) in enumerate(circ):
    for rr in rules+[M,W-M]:
        if abs(cx-rr)<r+1: bad.append(("ctl/rule",n,rr))
    for (cx2,cy2,r2,n2) in circ[i+1:]:
        if math.hypot(cx-cx2,cy-cy2)<r+r2: bad.append(("ctl/ctl",n,n2))
hh=[c["hit"] for c in controls]+[j["hit"] for j in jacks]
for i,a in enumerate(hh):
    for b in hh[i+1:]:
        if a[0]<b[0]+b[2] and b[0]<a[0]+a[2] and a[1]<b[1]+b[3] and b[1]<a[1]+a[3]: bad.append(("hit/hit",a,b))
print(f"canvas {W}x{H} | step col {stepw:.1f}px | frame y {FR_T}-{FR_B:.0f} | controls {len(controls)} leds {len(leds)} jacks {len(jacks)} | overlaps: {bad}")
assert not bad
# ---------------- render ----------------
DEFS=('<defs><linearGradient id="pf" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#242426"/><stop offset="1" stop-color="#161618"/></linearGradient>'
 '<radialGradient id="js" cx=".4" cy=".35" r=".8"><stop offset="0" stop-color="#e6e6e1"/><stop offset="1" stop-color="#7d7d79"/></radialGradient><radialGradient id="jn" cx=".4" cy=".35" r=".8"><stop offset="0" stop-color="#9a9a96"/><stop offset="1" stop-color="#3c3c3c"/></radialGradient>'
 '<linearGradient id="kb" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4b4b4e"/><stop offset=".5" stop-color="#1a1a1b"/><stop offset="1" stop-color="#060607"/></linearGradient><linearGradient id="kt" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2a2a2c"/><stop offset="1" stop-color="#131314"/></linearGradient>'
 '<radialGradient id="ks"><stop offset="0" stop-color="#fff" stop-opacity=".16"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>'
 '<linearGradient id="bs" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f4eedc"/><stop offset="1" stop-color="#a9a18a"/></linearGradient><radialGradient id="bd" cx=".45" cy=".4" r=".8"><stop offset="0" stop-color="#f8f3e4"/><stop offset="1" stop-color="#d0c8b2"/></radialGradient></defs>')
svg=(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="Liberation Sans">'+DEFS+
 f'<rect width="{W}" height="{H}" rx="8" fill="url(#pf)"/><rect x="3" y="3" width="{W-6}" height="{H-6}" rx="6" fill="none" stroke="#050506" stroke-width="2"/>'
 f'<text x="{M+4}" y="{CH_Y+21}" font-size="14" font-weight="700" letter-spacing="3" fill="#9a9684">SEQUENCER</text>'
 f'<rect x="{M}" y="{FR_T}" width="{AVAIL}" height="{FR_B-FR_T:.1f}" fill="none" stroke="{GOLD}" stroke-width="1.6"/>'
 +"".join(f'<line x1="{r}" y1="{FR_T}" x2="{r}" y2="{FR_B:.1f}" stroke="{GOLD}" stroke-width="1.6"/>' for r in rules)+"".join(P)
 +"".join(f'<g transform="translate({sx} {sy})"><circle r="5" fill="url(#js)" stroke="#000" stroke-width=".9"/><line x1="-3.5" x2="3.5" stroke="#1a1a1a" stroke-width="1.5"/><line y1="-3.5" y2="3.5" stroke="#1a1a1a" stroke-width="1.5"/></g>' for sx,sy in((9,9),(W-9,9),(9,H-9),(W-9,H-9)))+'</svg>')
blur=lambda a,r:gaussian_filter(a.astype(np.float32),r)
ss=lambda a,b,x:(lambda t:t*t*(3-2*t))(np.clip((x-a)/(b-a),0,1))
def fbm(h,w,oc=6,base=4,seed=0):
    r=np.random.default_rng(seed); acc=np.zeros((h,w),np.float32); amp=1.;tot=0
    for o in range(oc):
        n=base*2**o; a=np.ascontiguousarray(r.random((max(2,n*h//w),n)).astype(np.float32)); acc+=amp*np.asarray(Image.fromarray(a).resize((w,h),Image.BICUBIC)); tot+=amp; amp*=.5
    acc/=tot; return (acc-acc.min())/(acc.max()-acc.min())
def finish(svg):
    random.seed(5); rng=np.random.default_rng(5)
    a=np.asarray(Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode(),output_width=W*SC,output_height=H*SC))).convert("RGB")).astype(np.float32)/255; h,w=a.shape[:2]
    paint=ss(.38,.5,a.mean(-1)); a=a*(1-paint[...,None])+a*np.array([1,.96,.88],np.float32)*(.96+.04*fbm(h,w,5,6,1))[...,None]*paint[...,None]
    a+=(1-paint)[...,None]*((rng.random((h,w,1))-.5).astype(np.float32)*.05+(blur(rng.random((h,w)),.9)[...,None]-.5)*.1)
    a*=(.9+.2*fbm(h,w,6,3,2))[...,None]
    yy,xx=np.mgrid[0:h,0:w].astype(np.float32); d=np.minimum(np.minimum(xx,w-xx),np.minimum(yy,h-yy))
    wm=np.exp(-d/(14*SC))*np.clip((fbm(h,w,5,20,4)-.5)*3.5,0,1)*.4; a=a*(1-.5*wm[...,None])+np.array([.36,.36,.34],np.float32)*.5*wm[...,None]
    sl=Image.new("L",(w,h),0); dr=ImageDraw.Draw(sl)
    for _ in range(40):
        x0,y0=random.uniform(0,w),random.uniform(0,h); L=random.uniform(10,120)*SC; t=random.uniform(0,6.28); dr.line([(x0,y0),(x0+L*math.cos(t),y0+L*math.sin(t))],fill=random.randint(40,110),width=1)
    a+=blur(np.asarray(sl).astype(np.float32)/255,.6)[...,None]*.14
    a+=blur((rng.random((h,w))>.9996).astype(np.float32),1.0)[...,None]*1.5
    r=np.sqrt(((xx/w-.5)/.5)**2+((yy/h-.5)/.5)**2)/1.414; a*=(1-.28*r**2.5)[...,None]; a*=np.array([1.03,1,.95],np.float32); a+=.012
    return Image.fromarray((np.clip(a,0,1)**.97*255).astype(np.uint8))
o=os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","assets")+"/"; os.makedirs(o,exist_ok=True)
bg=finish(re.sub(r'<g class="live">.*?</g>',"",svg)); bg.save(o+"sq10_bg@2x.png"); bg.resize((W,H),Image.LANCZOS).save(o+"sq10_bg.png")
finish(svg).resize((W,H),Image.LANCZOS).save(o+"sq10_preview.png")
json.dump(dict(rack="SQ-10",canvas=[W,H],provisional=True,frame=[M,FR_T,AVAIL,round(FR_B-FR_T,1)],lane=[M,round(FR_B,1),AVAIL,round(H-M-FR_B,1)],
  columns=cols,controls=controls,leds=leds,jacks=jacks,labels=[dict(text=t[4],rect=[round(t[0],1),round(t[1],1),round(t[2]-t[0],1),round(t[3]-t[1],1)]) for t in texts]),open(o+"sq10_layout.json","w"),indent=1)
