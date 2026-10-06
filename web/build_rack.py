"""Builds web/rack.html: the Jidai rack on one page. A device browser, and a rack of any number of BUSHIDOs and RONINs in one
patch graph with one cable layer. Each device keeps its own layout (y from 0); the page stacks them.
RONIN's panel art and layout come from the RONIN repo, martialsystems/Ronin (default: a clone named ronin next to this repo, or --ms50 DIR).
Usage: python web/build_rack.py [--ms50 DIR] [--fragment OUT]"""
import base64,json,os,re,sys
d=os.path.dirname(os.path.abspath(__file__)); a=os.path.join(d,"..","assets")
arg=lambda k,dv:sys.argv[sys.argv.index(k)+1] if k in sys.argv else dv
ms=arg("--ms50",os.path.join(d,"..","..","ronin"))
t=open(os.path.join(d,"rack_template.html")).read()
b64=lambda p:"data:image/svg+xml;base64,"+base64.b64encode(open(p,"rb").read()).decode()
lay=json.load(open(os.path.join(a,"sq10_layout.json")))
pats=json.load(open(os.path.join(a,"sq10_patterns.json")))
rack=json.load(open(os.path.join(a,"rack_patches.json")))["patches"]   # bank B on both screens
# RONIN's screen row has no bank lamps or SAVE key. Copy BUSHIDO's (sockets, A and B, the + key, SAVE) from its art:
# both screens sit at the same x and y on their own panel. Elements are found by their spot on the panel.
bg=open(os.path.join(a,"sq10_panel_bg.svg")).read()
def bbox(el):
    m=re.search(r'\bcx="([\d.]+)" cy="([\d.]+)" r="([\d.]+)"',el)
    if m: x,y,r=map(float,m.groups()); return x-r,y-r,x+r,y+r
    m=re.search(r'\bx="([\d.]+)" y="([\d.]+)" width="([\d.]+)" height="([\d.]+)"',el)
    if m: x,y,w,h=map(float,m.groups()); return x,y,x+w,y+h
    m=re.search(r'\bd="([^"]+)"',el)
    if m:
        xs=[];ys=[];cmd=None;cx=cy=0;t=re.findall(r'[A-Za-z]|-?\d*\.?\d+(?:e-?\d+)?',m.group(1));i=0
        while i<len(t):   # absolute M L Q C H V Z, as the glyph paths use
            if t[i].isalpha(): cmd=t[i];i+=1;continue
            if cmd=="H": cx=float(t[i]);i+=1
            elif cmd=="V": cy=float(t[i]);i+=1
            else: cx,cy=float(t[i]),float(t[i+1]);i+=2
            xs.append(cx);ys.append(cy)
        return min(xs),min(ys),max(xs),max(ys)
    return None
art=[e for e in re.findall(r'<(?:circle|rect|path)\b[^>]*/>',bg) if (b:=bbox(e)) and b[0]>=955 and b[2]<=1100 and b[1]>=8 and b[3]<=46 and b[2]-b[0]>1.5]   # skips the faceplate's speckle dots
assert len(art)==9, len(art)
rl=json.load(open(os.path.join(ms,"panel","assets","layout.json")))
assert lay["canvas"][0]==rl["canvas"][0]==1600, "both panels are 1600 wide"
for k in ("columns","bands"): rl.pop(k,None)
dsp="\n".join(open(os.path.join(d,f)).read() for f in ("sq10_dsp.js","ms50_dsp.js","rack_engine.js"))
assert "</script" not in dsp
rep={"__PANEL__":b64(os.path.join(a,"sq10_panel_bg.svg")),"__RPANEL__":b64(os.path.join(ms,"panel","assets","panel_bg.svg")),
     "__LAYOUT__":json.dumps(lay,separators=(",",":")),"__RLAYOUT__":json.dumps(rl,separators=(",",":")),
     "__PATTERNS__":json.dumps(pats,separators=(",",":")),"__RACKP__":json.dumps(rack,separators=(",",":")),"__BANKART__":json.dumps("".join(art)),"__DSP__":dsp}
body=t
for k,v in rep.items(): body=body.replace(k,v)
chk=body.replace(dsp,"")
assert "__" not in chk.replace("__proto__",""), "unfilled placeholder"
if "--fragment" in sys.argv: open(arg("--fragment",None),"w").write(body)
else: open(os.path.join(d,"rack.html"),"w").write('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"></head><body>'+body+"</body></html>")
print("ok", len(body)//1024, "KB")
