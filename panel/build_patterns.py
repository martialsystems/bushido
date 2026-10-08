"""Writes assets/bushido_patterns.json: the factory bank shown on the PATTERN screen (plugin and web page).
The factory bank is cleared for now: it holds one INIT pattern, every parameter at the engine default
(engine/BushidoModule.cpp) and no cables. New factory patterns can be added to P with pat() below.
Each pattern sets every panel parameter (0..1, the same values the plugin's parameters hold) and its cables.
Pitches are written as note names and stored as knob positions for the Hz/V law at the 5 V range:
1 V = A1 (MIDI 33), double the volts = one octave up, so knob = 2^((note-33)/12) / 5. The 5 V range tops out near C4."""
import json,math,os
NOTES={"C":0,"C#":1,"D":2,"D#":3,"E":4,"F":5,"F#":6,"G":7,"G#":8,"A":9,"A#":10,"B":11}
def midi(n): return 12*(int(n[-1])+1)+NOTES[n[:-1]]
def knob(n):
    if n in ("-",None): return 0.0                                   # 0 V: a Hz/V VCO is silent
    v=2**((midi(n)-33)/12); assert v<=5.0,n; return round(v/5,4)
def tempo(steps_per_s):            # engine: rate = 0.5 * 2^(6 * TEMPO) steps/s; rounded to a whole BPM at 1/16 (4 steps per beat)
    bpm=round(steps_per_s*15); return round(math.log2(bpm*4/60/0.5)/6,6)
def pat(name,mode,a,b,c,bps,cmode=0,porta=(0,0),cables=(),source=0):
    assert len(name)<=13 and len(a)==len(b)==len(c)==12,name
    p={"MODE:MODE":{"A":0.0,"A+B":0.5,"ALT":1.0}[mode],"CH:C MODE":float(cmode),"CLOCK:TEMPO":tempo(bps),"CLOCK:SOURCE":float(source),"CLOCK:DIV":0.5,
       "CH:RANGE A":1.0,"CH:RANGE B":1.0,"CH:PORTA A":porta[0],"CH:PORTA B":porta[1],"MIXER:LEVEL 1":0.7,"MIXER:LEVEL 2":0.7}
    for i in range(12): p[f"A:{i+1}"]=knob(a[i]); p[f"B:{i+1}"]=knob(b[i]); p[f"C:{i+1}"]=round(c[i],3)
    return dict(name=name,params=p,cables=[list(x) for x in cables])
S=lambda s:s.split()
BPM=lambda bpm:bpm/15   # pat() takes steps per second; at 1/16 a beat is 4 steps

# INIT: the engine defaults (BushidoModule's ParamInfo defaults), no cables. A fresh instance opens on it.
INIT=dict(name="INIT",params={"MODE:MODE":0.5,"CH:C MODE":0.0,"CLOCK:TEMPO":0.5,"CLOCK:SOURCE":0.0,"CLOCK:DIV":0.5,
          "CH:RANGE A":1.0,"CH:RANGE B":1.0,"CH:PORTA A":0.0,"CH:PORTA B":0.0,"MIXER:LEVEL 1":0.7,"MIXER:LEVEL 2":0.7,
          **{f"{r}:{i}":0.5 for i in range(1,13) for r in "ABC"}},cables=[])
P=[INIT]
out=os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","assets","bushido_patterns.json")
json.dump(dict(note="Factory bank for the PATTERN screen (INIT only for now). Values are 0..1 parameter positions; cables are [jack, jack, colour] with BUSHIDO jack ids.",patterns=P),open(out,"w"),indent=1)
print(len(P),"patterns ->",out)
