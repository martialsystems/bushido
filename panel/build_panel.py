"""SQ-10 style panel generator. PROVISIONAL layout (built from documented features, not from a photo).
Writes vector art in the MS-50 style, so the panel scales cleanly at any editor size:
  assets/sq10_panel.svg     every part at its default (preview)
  assets/sq10_panel_bg.svg  no live parts (knobs, switches, keys, lamps), for UIs that draw those on top
  assets/sq10_layout.json   geometry for the plugin editor and the web version
Labels are converted to outlines, so the SVGs need no font. Needs Python 3 with fonttools and pillow, and Liberation Sans Bold."""
import math,json,random,os,re
from PIL import ImageFont
FP="/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
FT,FL,FS,LST,LSL=12,11,9,.8,.4
fm={s:ImageFont.truetype(FP,s*8) for s in (FT,FL,FS)}
tw=lambda s,z:fm[z].getlength(s)/8+(LST if z==FT else LSL)*(len(s)-1)
W,SC,M=1600,2,14; AVAIL=W-2*M; INK="#dcd6c2"; GOLD="#c29f4c"
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
def key_svg(cx,cy,style):   # square key like the MS-50 HOLD key: cream, or black for STEP and RESET
    k,d,e,st=(("bs","bd","#7e7764","#6f6a5a") if style=="cream" else ("bk","bkd","#000","#3a3a3e"))
    return (f'<rect x="{cx-13:.1f}" y="{cy-13}" width="26" height="26" rx="3" fill="url(#{k})" stroke="{st}" stroke-width=".9"/>'
            f'<rect x="{cx-13:.1f}" y="{cy+9}" width="26" height="4" rx="2" fill="{e}" opacity=".55"/><rect x="{cx-9.5:.1f}" y="{cy-10}" width="19" height="17" rx="2.5" fill="url(#{d})"/>')
def button(sec,lab,cx,cy,style="black",lamp=None):
    controls.append(dict(id=f"{sec}:{lab}",kind="button",style=style,cx=round(cx,1),cy=cy,r=13,default=0,hit=[round(cx-17,1),cy-17,34,34]))
    circ.append((cx,cy,18,"button "+lab))
    P.append(f'<rect x="{cx-17:.1f}" y="{cy-17}" width="34" height="34" rx="4" fill="#050505" stroke="#2a2a2c" stroke-width="1.2"/><g class="live">{key_svg(cx,cy,style)}</g>')
    ly=cy+17
    if lamp: led(lamp,cx,cy+26,r=3.5); ly=cy+30
    T(cx,ly+14,lab,FL)
def rocker_svg(x,y,w,h,right,dark=False):   # pressed half sits low and shaded, raised half catches the light
    hw=w/2; up,dn=("#3a3a3e","#101012") if dark else ("url(#ru)","url(#rd)"); a,b=(up,dn) if right else (dn,up)   # the active half is pressed in
    hl,sep=("#77777c","#000") if dark else ("#fffdf4","#6b675a")
    return (f'<rect x="{x:.1f}" y="{y}" width="{hw}" height="{h}" rx="2.5" fill="{a}"/><rect x="{x+hw:.1f}" y="{y}" width="{hw}" height="{h}" rx="2.5" fill="{b}"/>'
            f'<line x1="{x+(2 if right else hw+2):.1f}" y1="{y+1.2}" x2="{x+(hw-2 if right else w-2):.1f}" y2="{y+1.2}" stroke="{hl}" stroke-width="1"/>'
            f'<line x1="{x+hw:.1f}" y1="{y+1}" x2="{x+hw:.1f}" y2="{y+h-1}" stroke="{sep}" stroke-width="1"/>')
def rocker(sec,lab,cx,cy,marks,default,text=None):   # two-position rocker like the MS-50 POWER switch, in white: press the left or right half
    w,h=34,18; x,y=cx-w/2,cy-h/2
    controls.append(dict(id=f"{sec}:{lab}",kind="switch",style="rocker",cx=round(cx,1),cy=cy,r=w/2,default=default,positions=2,angles=[-50,50],marks=marks,
                         rect=[round(x,1),y,w,h],hit=[round(cx-21,1),cy-15,42,30]))
    for dx in (-9,0,9): circ.append((cx+dx,cy,h/2+4,"rocker "+lab))   # three circles cover the wide, short housing
    P.append(f'<rect x="{x-3:.1f}" y="{y-3}" width="{w+6}" height="{h+6}" rx="4" fill="#050505" stroke="#2a2a2c" stroke-width="1.2"/><g class="live">{rocker_svg(x,y,w,h,default>.5)}</g>')
    for m,dx in zip(marks,(-1,1)): T(cx+dx*w/4,cy-h/2-9,m,FL)
    T(cx,cy+h/2+17,text or lab,FL)
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
                led("CH:"+ch,x+20,cy+22); knob("CH","PORTA "+ch,x+64,cy,0.0,text="PORTA"); rocker("CH","RANGE "+ch,x+124,cy,["1V","5V"],1.0,text="RANGE")
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
        button("MODE","START/STOP",cx,222,"cream",lamp="MODE:RUN"); button("MODE","STEP",cx,292); button("MODE","RESET",cx,346)
    elif title=="INPUTS":
        for i,l in enumerate(["START/STOP","STEP","RESET"]): jack("INPUTS",l,"in",cx,TI_Y+28+JR+1.5+i*60)
    elif title=="OUTPUTS":
        for i,l in enumerate(["CV A","GATE A","CV B","GATE B","CV C"]): jack("OUTPUTS",l,"out",cx,TI_Y+28+JR+1.5+i*60)
    elif title=="MIXER":
        knob("MIXER","LEVEL 1",cx,ROWY[0],.7); knob("MIXER","LEVEL 2",cx,ROWY[1],.7)
        jack("MIXER","IN 1","in",cx,272); jack("MIXER","IN 2","in",cx,322); jack("MIXER","OUT","out",cx,372)
    x+=w
FR_T=TI_Y-6; FR_B=max(t[3] for t in texts)+12; rules=[c["x"] for c in cols[1:]]
H=round(FR_B+26)                 # no empty lane under the frame: the bottom screws sit just below it
# ---------------- top bar: name, BYPASS rocker (top left), PATTERN screen (centre) ----------------
NAME="BUSHIDO"
NAME_W=ImageFont.truetype(FP,14*8).getlength(NAME)/8+3*(len(NAME)-1)
BP_RK=[round(M+4+NAME_W+28+tw("BYPASS",FL)+14+tw("OFF",FL)+6),CH_Y+5,40,20]         # dark rocker like the MS-50 POWER switch: left half OFF, right half ON
controls.append(dict(id="TOP:BYPASS",kind="switch",style="rocker",tone="dark",cx=BP_RK[0]+BP_RK[2]/2,cy=BP_RK[1]+BP_RK[3]/2,r=BP_RK[2]/2,default=0.0,positions=2,
                     angles=[-50,50],marks=["OFF","ON"],rect=BP_RK,hit=[BP_RK[0]-2,BP_RK[1]-4,BP_RK[2]+4,BP_RK[3]+8]))
SCR_BEZEL=[680,CH_Y+1,240,28]; SCR_LCD=[684,CH_Y+5,232,20]; SCR_KEY=[926,CH_Y+3,26,24]   # same geometry as the MS-50 PRESET screen
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
        if n!=n2 and math.hypot(cx-cx2,cy-cy2)<r+r2: bad.append(("ctl/ctl",n,n2))
hh=[c["hit"] for c in controls]+[j["hit"] for j in jacks]
for i,a in enumerate(hh):
    for b in hh[i+1:]:
        if a[0]<b[0]+b[2] and b[0]<a[0]+a[2] and a[1]<b[1]+b[3] and b[1]<a[1]+a[3]: bad.append(("hit/hit",a,b))
print(f"canvas {W}x{H} | step col {stepw:.1f}px | frame y {FR_T}-{FR_B:.0f} | controls {len(controls)} leds {len(leds)} jacks {len(jacks)} | overlaps: {bad}")
assert not bad
# ---------------- render ----------------
tl=lambda x,s:f'<text x="{x:.1f}" y="{CH_Y+19}" font-size="{FL}" font-weight="700" letter-spacing="{LSL}" fill="{INK}">{s}</text>'
DEFS=('<defs><linearGradient id="pf" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#242426"/><stop offset="1" stop-color="#161618"/></linearGradient>'
 '<radialGradient id="js" cx=".4" cy=".35" r=".8"><stop offset="0" stop-color="#e6e6e1"/><stop offset="1" stop-color="#7d7d79"/></radialGradient><radialGradient id="jn" cx=".4" cy=".35" r=".8"><stop offset="0" stop-color="#9a9a96"/><stop offset="1" stop-color="#3c3c3c"/></radialGradient>'
 '<linearGradient id="kb" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4b4b4e"/><stop offset=".5" stop-color="#1a1a1b"/><stop offset="1" stop-color="#060607"/></linearGradient><linearGradient id="kt" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2a2a2c"/><stop offset="1" stop-color="#131314"/></linearGradient>'
 '<radialGradient id="ks"><stop offset="0" stop-color="#fff" stop-opacity=".16"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>'
 '<linearGradient id="ru" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fbf9f1"/><stop offset="1" stop-color="#dcd7c6"/></linearGradient><linearGradient id="rd" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8e897a"/><stop offset="1" stop-color="#bdb8a6"/></linearGradient><linearGradient id="bk" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4a4a4e"/><stop offset="1" stop-color="#0e0e10"/></linearGradient><radialGradient id="bkd" cx=".45" cy=".4" r=".8"><stop offset="0" stop-color="#36363a"/><stop offset="1" stop-color="#161618"/></radialGradient><linearGradient id="bs" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f4eedc"/><stop offset="1" stop-color="#a9a18a"/></linearGradient><radialGradient id="bd" cx=".45" cy=".4" r=".8"><stop offset="0" stop-color="#f8f3e4"/><stop offset="1" stop-color="#d0c8b2"/></radialGradient>'
 '<linearGradient id="lcd" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8f9a7c"/><stop offset=".5" stop-color="#a6b192"/><stop offset="1" stop-color="#94a083"/></linearGradient></defs>')
svg=(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="Liberation Sans">'+DEFS+
 f'<rect width="{W}" height="{H}" rx="8" fill="url(#pf)"/><rect x="3" y="3" width="{W-6}" height="{H-6}" rx="6" fill="none" stroke="#050506" stroke-width="2"/>'
 f'<text x="{M+4}" y="{CH_Y+21}" font-size="14" font-weight="700" letter-spacing="3" fill="#9a9684">{NAME}</text>'
 +tl(BP_RK[0]-20-tw("OFF",FL)-tw("BYPASS",FL),"BYPASS")+tl(BP_RK[0]-6-tw("OFF",FL),"OFF")+tl(BP_RK[0]+BP_RK[2]+6,"ON")
 +f'<rect x="{BP_RK[0]-2}" y="{BP_RK[1]-2}" width="{BP_RK[2]+4}" height="{BP_RK[3]+4}" rx="4" fill="#050505" stroke="#2a2a2c" stroke-width="1.2"/><g class="live">{rocker_svg(*BP_RK,False,dark=True)}</g>'
 +tl(SCR_BEZEL[0]-8-tw("PATTERN",FL),"PATTERN")
 +f'<rect x="{SCR_BEZEL[0]}" y="{SCR_BEZEL[1]}" width="{SCR_BEZEL[2]}" height="{SCR_BEZEL[3]}" rx="3" fill="#0a0a0b" stroke="#2a2a2c" stroke-width="1.2"/>'
 f'<rect x="{SCR_LCD[0]}" y="{SCR_LCD[1]}" width="{SCR_LCD[2]}" height="{SCR_LCD[3]}" rx="1.5" fill="url(#lcd)"/>'
 f'<rect x="{SCR_KEY[0]}" y="{SCR_KEY[1]}" width="{SCR_KEY[2]}" height="{SCR_KEY[3]}" rx="3" fill="url(#bs)" stroke="#6f6a5a" stroke-width=".9"/>'
 f'<rect x="{SCR_KEY[0]}" y="{SCR_KEY[1]+SCR_KEY[3]-4}" width="{SCR_KEY[2]}" height="4" rx="2" fill="#7e7764" opacity=".55"/>'
 f'<polygon points="{SCR_KEY[0]+8},{SCR_KEY[1]+9} {SCR_KEY[0]+18},{SCR_KEY[1]+9} {SCR_KEY[0]+13},{SCR_KEY[1]+15}" fill="#2a2620"/>'
 f'<rect x="{M}" y="{FR_T}" width="{AVAIL}" height="{FR_B-FR_T:.1f}" fill="none" stroke="{GOLD}" stroke-width="1.6"/>'
 +"".join(f'<line x1="{r}" y1="{FR_T}" x2="{r}" y2="{FR_B:.1f}" stroke="{GOLD}" stroke-width="1.6"/>' for r in rules)+"".join(P)
 +"".join(f'<g transform="translate({sx} {sy})"><circle r="5" fill="url(#js)" stroke="#000" stroke-width=".9"/><line x1="-3.5" x2="3.5" stroke="#1a1a1a" stroke-width="1.5"/><line y1="-3.5" y2="3.5" stroke="#1a1a1a" stroke-width="1.5"/></g>' for sx,sy in((9,9),(W-9,9),(9,H-9),(W-9,H-9)))+'</svg>')
# ---------------- SVG output (same treatment as the MS-50 panel) ----------------
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
_F=TTFont(FP); _GS=_F.getGlyphSet(); _CMAP=_F.getBestCmap(); _UPM=_F["head"].unitsPerEm; _HM=_F["hmtx"]
def _attr(a,k,d=None):
    m=re.search(r'\b'+k+r'="([^"]*)"',a); return m.group(1) if m else d
def text_to_paths(svg):
    def one(m):
        a,txt=m.group(1),m.group(2).replace("&amp;","&")
        x,y,sz=float(_attr(a,"x")),float(_attr(a,"y")),float(_attr(a,"font-size")); ls=float(_attr(a,"letter-spacing","0"))
        sc=sz/_UPM; gl=[_CMAP.get(ord(c),".notdef") for c in txt]; cx=x; d=[]
        for g in gl:
            pen=SVGPathPen(_GS); _GS[g].draw(TransformPen(pen,(sc,0,0,-sc,cx,y))); d.append(pen.getCommands()); cx+=_HM[g][0]*sc+ls
        return f'<path d="{" ".join(d)}" fill="{_attr(a,"fill","#000")}"/>'
    return re.sub(r'<text ([^>]*)>(.*?)</text>',one,svg)
def wear_svg():                     # vector wear: mottling, edge wear, scratches, dust, plus an optional grain filter
    R=random.Random(5); o=['<defs><radialGradient id="mdk"><stop offset="0" stop-color="#000" stop-opacity="1"/><stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>'
       '<radialGradient id="mlt"><stop offset="0" stop-color="#fff" stop-opacity="1"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>'
       '<radialGradient id="vig" cx=".5" cy=".5" r=".75"><stop offset=".55" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".38"/></radialGradient>'
       '<linearGradient id="edgeT" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#bdbdb8" stop-opacity=".22"/><stop offset="1" stop-color="#bdbdb8" stop-opacity="0"/></linearGradient>'
       '<linearGradient id="edgeL" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#bdbdb8" stop-opacity=".18"/><stop offset="1" stop-color="#bdbdb8" stop-opacity="0"/></linearGradient>'
       '<filter id="grain" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency=".85" numOctaves="2" seed="7"/>'
       '<feColorMatrix type="matrix" values="0 0 0 0 .5  0 0 0 0 .5  0 0 0 0 .5  0 0 0 .14 0"/></filter></defs><g id="wear">']
    for _ in range(34):
        o.append(f'<ellipse cx="{R.uniform(0,W):.0f}" cy="{R.uniform(0,H):.0f}" rx="{R.uniform(60,320):.0f}" ry="{R.uniform(30,160):.0f}" fill="url(#{R.choice(["mdk","mdk","mlt"])})" opacity="{R.uniform(.03,.09):.3f}"/>')
    o.append(f'<rect x="0" y="0" width="{W}" height="14" fill="url(#edgeT)"/><rect x="0" y="0" width="12" height="{H}" fill="url(#edgeL)"/>'
             f'<rect x="{W-12}" y="0" width="12" height="{H}" fill="url(#edgeL)" transform="rotate(180 {W-6} {H/2})"/>')
    for _ in range(40):
        side=R.choice("tblr"); x=R.uniform(0,W) if side in "tb" else (R.uniform(2,10) if side=="l" else R.uniform(W-10,W-2)); y=R.uniform(0,H) if side in "lr" else (R.uniform(2,10) if side=="t" else R.uniform(H-10,H-2))
        o.append(f'<ellipse cx="{x:.1f}" cy="{y:.1f}" rx="{R.uniform(1.5,7):.1f}" ry="{R.uniform(1,4):.1f}" fill="#6a6a66" opacity="{R.uniform(.15,.35):.2f}"/>')
    for _ in range(70):
        x0,y0=R.uniform(0,W),R.uniform(0,H); L=R.uniform(8,110); a=R.uniform(0,math.tau)
        o.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x0+L*math.cos(a):.1f}" y2="{y0+L*math.sin(a):.1f}" stroke="#dcdcd4" stroke-width="{R.uniform(.35,.7):.2f}" opacity="{R.uniform(.05,.16):.2f}"/>')
    for _ in range(320):
        o.append(f'<circle cx="{R.uniform(0,W):.1f}" cy="{R.uniform(0,H):.1f}" r="{R.uniform(.3,.9):.2f}" fill="#ece8dc" opacity="{R.uniform(.2,.55):.2f}"/>')
    o.append(f'<rect x="0" y="0" width="{W}" height="{H}" fill="none" filter="url(#grain)"/><rect x="0" y="0" width="{W}" height="{H}" rx="8" fill="url(#vig)"/></g>')
    return "".join(o)
o=os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","assets")+"/"; os.makedirs(o,exist_ok=True)
full=text_to_paths(svg.replace("</svg>",wear_svg()+"</svg>"))
open(o+"sq10_panel.svg","w").write(full)                                              # every part at its default
open(o+"sq10_panel_bg.svg","w").write(re.sub(r'<g class="live">.*?</g>',"",full))     # no live parts: the editor and web page draw those on top
json.dump(dict(rack="SQ-10",canvas=[W,H],provisional=True,frame=[M,FR_T,AVAIL,round(FR_B-FR_T,1)],lane=[M,round(FR_B,1),AVAIL,round(H-FR_B,1)],screen=dict(bezel=SCR_BEZEL,lcd=SCR_LCD,button=SCR_KEY,chars=16),name=NAME,
  columns=cols,controls=controls,leds=leds,jacks=jacks,labels=[dict(text=t[4],rect=[round(t[0],1),round(t[1],1),round(t[2]-t[0],1),round(t[3]-t[1],1)]) for t in texts]),open(o+"sq10_layout.json","w"),indent=1)
