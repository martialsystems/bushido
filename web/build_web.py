"""Builds web/bushido.html: a playable, self-contained web version of the BUSHIDO panel (same SVG art and layout as the plugin).
Usage: python web/build_web.py [--fragment OUT]   (--fragment writes the page body only, for hosts that add their own <html> skeleton)"""
import base64,json,os,sys
d=os.path.dirname(os.path.abspath(__file__)); a=os.path.join(d,"..","assets")
t=open(os.path.join(d,"bushido_template.html")).read()
svg=open(os.path.join(a,"bushido_panel_bg.svg"),"rb").read()
lay=json.load(open(os.path.join(a,"bushido_layout.json")))
pats=json.load(open(os.path.join(a,"bushido_patterns.json")))
body=t.replace("__PANEL__","data:image/svg+xml;base64,"+base64.b64encode(svg).decode()).replace("__LAYOUT__",json.dumps(lay,separators=(",",":"))).replace("__PATTERNS__",json.dumps(pats,separators=(",",":"))).replace("__W__",str(lay["canvas"][0])).replace("__H__",str(lay["canvas"][1]))
assert "__" not in body.replace("__proto__",""), "unfilled placeholder"
if "--fragment" in sys.argv: open(sys.argv[sys.argv.index("--fragment")+1],"w").write(body)
else: open(os.path.join(d,"bushido.html"),"w").write('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"></head><body>'+body+"</body></html>")
print("ok", len(body)//1024, "KB")
