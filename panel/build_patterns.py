"""Writes assets/sq10_patterns.json: the 10 factory patterns shown on the PATTERN screen (plugin and web page).
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
P=[
 pat("LOOP 8","A",S("A2 C3 D3 E3 C3 G3 E3 D3 A2 B2 C3 E3"),S("E3 G3 A3 C4 B3 A3 G3 E3 D3 E3 G3 A3"),
     [.8,.3,.55,.2,.9,.35,.6,.25,.7,.4,.5,.95],5,cables=[("9:TRIG","INPUTS:RESET","yellow")]),
 pat("LONG 24","A+B",S("A2 E3 A3 C4 B3 A3 E3 G3 F3 E3 D3 E3"),S("F2 C3 F3 A3 G3 F3 C3 E3 D3 C3 B2 C3"),
     [.6,.4,.5,.3,.7,.4,.5,.3,.6,.4,.5,.8],6),
 pat("CALL ANSWER","ALT",S("D3 F3 A3 - D3 F3 G3 - A3 G3 F3 -"),S("C3 E3 G3 - C3 E3 F3 - E3 D3 D3 -"),
     [.5]*12,5),
 pat("ACID LINE","A",S("A1 A1 A2 A1 C2 A1 D#2 A1 G2 A1 A2 C2"),S("A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 A2"),
     [.15,.6,.2,.85,.25,.15,.7,.2,.95,.15,.3,.6],8,cmode=1,porta=(0.25,0)),
 pat("ARP 4 CHORDS","A",S("A2 C3 E3 F2 A2 C3 C3 E3 G3 G2 B2 D3"),S("A1 A1 A1 F1 F1 F1 C2 C2 C2 G1 G1 G1"),
     [.9,.4,.5,.9,.4,.5,.9,.4,.5,.9,.4,.5],7),
 pat("BASS 6","A",S("E1 E2 G1 E2 A1 B1 E1 E1 E1 E1 E1 E1"),S("B2 B2 B2 B2 B2 B2 B2 B2 B2 B2 B2 B2"),
     [.7,.25,.6,.25,.8,.4,.5,.5,.5,.5,.5,.5],4,cmode=1,cables=[("7:TRIG","INPUTS:RESET","red")]),
 pat("FILTER WALK","A",S("C2 C2 G2 C2 C2 A#2 C2 C2 G2 C2 D#2 F2"),S("C3 C3 C3 C3 C3 C3 C3 C3 C3 C3 C3 C3"),
     [.05,.12,.2,.3,.4,.52,.64,.76,.86,.95,.7,.35],6),
 pat("SLOW GLIDE","A+B",S("D2 A2 F3 E3 D3 A2 C3 G2 A2 F2 E2 A2"),S("D3 C3 A2 G2 F2 A2 D3 E3 F3 A3 G3 E3"),
     [.3,.5,.7,.9,.7,.5,.3,.5,.7,.9,.7,.5],1.5,porta=(0.55,0.55)),
 pat("OCTAVES","ALT",S("C2 C3 C2 C3 D#2 D#3 F2 F3 G2 G3 A#2 A#3"),S("G#1 G#2 G#1 G#2 A#1 A#2 C2 C3 D2 D3 G2 G3"),
     [.5,.2,.5,.2,.5,.2,.5,.2,.5,.2,.5,.2],9,cmode=1),
 pat("TRIPLET 3","A",S("E3 B2 G2 A3 E3 C3 E3 B2 G2 D3 A2 F#2"),S("E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2"),
     [.8,.5,.3,.5,.5,.5,.5,.5,.5,.5,.5,.5],5,cables=[("4:TRIG","INPUTS:RESET","green")]),

 # ---- acid set: fast 16ths, octave jumps, slides from PORTA, and either TIME gates (short stabs, long tied notes)
 # ---- or CV C sweeping the filter. Loops shorter than 12 come from a TRIG cable into RESET.
 pat("ACID SQUELCH","A",S("A1 A1 A2 A1 A#1 A1 G2 A1 A1 C2 A2 E2"),S("A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 A2"),
     [.1,.35,.15,.9,.25,.1,.95,.2,.15,.6,.12,.85],8.5,cmode=1,porta=(0.2,0)),
 pat("ACID FILTER","A",S("C2 C2 C3 C2 D#2 C2 G2 F2 C2 C3 A#1 C2"),S("C3 C3 C3 C3 C3 C3 C3 C3 C3 C3 C3 C3"),
     [.1,.2,.95,.3,.55,.15,.85,.4,.2,1.0,.6,.25],8,porta=(0.15,0)),
 pat("ACID 7","A",S("E1 E2 E1 G1 E2 A#1 D2 E1 E1 E1 E1 E1"),S("E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2"),
     [.15,.9,.2,.6,.12,.95,.3,.5,.5,.5,.5,.5],8.5,cmode=1,porta=(0.22,0),cables=[("8:TRIG","INPUTS:RESET","red")]),
 pat("ACID 5 STEP","A",S("A1 A2 C2 A1 D#2 A1 A1 A1 A1 A1 A1 A1"),S("A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 A2 A2"),
     [.2,.8,.15,.95,.3,.5,.5,.5,.5,.5,.5,.5],9,cmode=1,porta=(0.18,0),cables=[("6:TRIG","INPUTS:RESET","yellow")]),
 pat("ACID 24","A+B",S("F1 F1 F2 F1 G#1 F1 C2 D#2 F1 F2 C#2 F1"),S("F1 F2 F1 G#1 G#2 F1 A#1 C2 F2 D#2 C#2 C2"),
     [.15,.4,.9,.2,.6,.1,.95,.3,.2,.85,.5,.12],8,cmode=1,porta=(0.2,0.2)),
 pat("ACID DUET","ALT",S("A1 A2 A1 C2 A1 E2 A1 G2 A1 A2 G1 A1"),S("E3 - E3 G3 - A3 - C4 B3 - G3 E3"),
     [.15,.8,.2,.9,.15,.6,.2,.95,.15,.7,.3,.1],8,cmode=1,porta=(0.2,0.08)),
]
out=os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","assets","sq10_patterns.json")
json.dump(dict(note="Factory patterns for the PATTERN screen. Values are 0..1 parameter positions; cables are [jack, jack, colour] with SQ-10 jack ids.",patterns=P),open(out,"w"),indent=1)
print(len(P),"patterns ->",out)
