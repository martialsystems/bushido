"""Writes assets/bushido_patterns.json: the factory bank at the front of bank A on the PATTERN screen (plugin and web pages).
The plugin embeds the file as binary data (CMake juce_add_binary_data); web/build_web.py and web/build_rack.py embed it in the pages.
After editing, run this script, then rebuild the pages and the plugin.

Every pattern is format 1 and sets every panel and tab parameter (0..1, the values the plugin's parameters hold), so a pattern
saved back from the panel equals its factory entry. Cables are [jack, jack, colour] with BUSHIDO jack ids, output first.
Pitches are note names, stored as knob positions under the row's PITCH LAW (C3 = 130.81 Hz = MIDI 48 for both laws):
  V/OCT     0 V = C3, 1 V per octave:          knob = (note - 48) / 12 / RANGE
  HZ/V LIN  1 V = C3, double the volts = +1 oct: knob = 2^((note - 48) / 12) / RANGE;  "-" = 0 V, a rest (no MIDI note, the VCO stops)
Row C is 0..1: a CV of 0..5 V (velocity FROM C = round(1 + 126 C), accent CV for a filter), or in TIME mode the gate length 5..95 %.
Tempo is stored as the TEMPO knob for a BPM at the pattern's DIV: TEMPO = log2(BPM x steps per beat / 60 / 0.5) / 6.
Names: at most 12 characters (the screen shows "A001 " plus 12), in the screen's character set. All melodies are original."""
import json,math,os,struct
NOTES={"C":0,"C#":1,"Db":1,"D":2,"D#":3,"Eb":3,"E":4,"F":5,"F#":6,"Gb":6,"G":7,"G#":8,"Ab":8,"A":9,"A#":10,"Bb":10,"B":11}
CHARSET=set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 &-+/.")
def midi(n): return 12*(int(n[-1])+1)+NOTES[n[:-1]]
def f32(x): return struct.unpack("f",struct.pack("f",x))[0]                 # a float32 value, written exactly
def sw(k,positions): assert 0<=k<positions; return f32(f32(1.0/(positions-1))*k)   # switch position k of `positions`, as JUCE's snapped parameter holds it
def knob(n,law,rng):
    if n=="-": assert law==1,"a rest needs HZ/V LIN"; return 0.0
    m=midi(n)
    if law==0: v=(m-48)/12
    else: v=2**((m-48)/12); assert v>=1/32,n
    k=v/rng; assert 0<=k<=1+1e-12,(n,law,rng); return round(min(k,1.0),6)
SPB={"1/8":2,"1/16":4,"1/32":8}
def tempo(bpm,div): t=math.log2(bpm*SPB[div]/60/0.5)/6; assert 0<=t<=1,bpm; return round(t,6)

PARAM_IDS=[f"{r}:{i}" for r in "ABC" for i in range(1,13)]+["CH:PORTA A","CH:PORTA B","CH:RANGE A","CH:RANGE B","CH:C MODE","CLOCK:TEMPO","CLOCK:SOURCE","MODE:MODE",
    "MIXER:LEVEL 1","MIXER:LEVEL 2","CLOCK:DIV","CLOCK:EXT SOURCE","CLOCK:SETTLE","CLOCK:TRIG MODE","STEPS:LAW A","STEPS:LAW B","STEPS:QUANT A","STEPS:QUANT B",
    "MIDI:CH A","MIDI:CH B","MIDI:VEL A","MIDI:VEL B"]   # engine order (BushidoModule.cpp), buttons left out

def pat(name,a,c,b=None,mode="A",law=("VOCT","VOCT"),rng=(5,5),quant=(1,1),porta=(0,0),cmode="CV",bpm=120,div="1/16",clock="INT",
        settle="TIGHT",trig="STEP",vel=("100","100"),ch=(1,2),cables=(),desc="",ronin=""):
    assert len(name)<=12 and set(name)<=CHARSET,name
    assert len(a)==len(c)==12 and (b is None or len(b)==12),name
    if b is None: law,rng,quant=(law[0],law[0]),(rng[0],rng[0]),(quant[0],quant[0])   # row B unused: a copy of row A under the same law and range
    L=[{"VOCT":0,"LIN":1}[x] for x in law]
    p={}
    for i in range(12):
        p[f"A:{i+1}"]=knob(a[i],L[0],rng[0])
        if b is None: p[f"B:{i+1}"]=p[f"A:{i+1}"]                                                          # row B unused: a copy of row A's knobs
        else: p[f"B:{i+1}"]=knob(b[i],L[0] if mode=="A+B" else L[1],rng[0] if mode=="A+B" else rng[1])   # A+B plays row B on the A jacks
        assert 0<=c[i]<=1; p[f"C:{i+1}"]=round(c[i],6)
    p.update({"CH:PORTA A":porta[0],"CH:PORTA B":porta[1],"CH:RANGE A":sw(rng[0]==5,2),"CH:RANGE B":sw(rng[1]==5,2),"CH:C MODE":sw(cmode=="TIME",2),
              "CLOCK:TEMPO":tempo(bpm,div),"CLOCK:SOURCE":sw(clock=="HOST",2),"MODE:MODE":sw({"A":0,"A+B":1,"ALT":2}[mode],3),
              "MIXER:LEVEL 1":0.7,"MIXER:LEVEL 2":0.7,"CLOCK:DIV":sw({"1/8":0,"1/16":1,"1/32":2}[div],3),"CLOCK:EXT SOURCE":sw(clock=="HOST",2),
              "CLOCK:SETTLE":sw(settle=="VINTAGE",2),"CLOCK:TRIG MODE":sw(trig=="PULSE",2),"STEPS:LAW A":float(L[0]),"STEPS:LAW B":float(L[1]),
              "STEPS:QUANT A":float(quant[0]),"STEPS:QUANT B":float(quant[1]),"MIDI:CH A":sw(ch[0]-1,16),"MIDI:CH B":sw(ch[1]-1,16),
              "MIDI:VEL A":sw(vel[0]=="FROM C",2),"MIDI:VEL B":sw(vel[1]=="FROM C",2)})
    p={k:p[k] for k in PARAM_IDS}; assert len(p)==len(PARAM_IDS)
    return dict(name=name,format=1,params=p,cables=[list(x) for x in cables]),desc,ronin
S=lambda s:s.split()
C=lambda s:[float(x) for x in s.split()]
RESET=lambda n:(f"{n+1}:TRIG","INPUTS:RESET","yellow")      # TRIG N+1 -> RESET: an N-step loop
SKIP=lambda n:(f"{n}:TRIG","INPUTS:STEP","white")           # TRIG N -> STEP: step N is skipped (it never reaches CV or GATE)
TEMPOCV=("OUTPUTS:CV C","CLOCK:TEMPO CV","green")           # row C bends the internal clock: +1 V = twice as fast, per step
ACC="Accents: VEL A = FROM C (C 1.0 = velocity 127, 0.5 = 64)."

# INIT: the engine defaults (BushidoModule's ParamInfo defaults), no cables. A fresh instance opens on it.
INIT=dict(name="INIT",format=1,params={k:(0.5 if k[0] in "ABC" and k[1]==":" else 0.0) for k in PARAM_IDS},cables=[])
INIT["params"].update({"CH:RANGE A":1.0,"CH:RANGE B":1.0,"CLOCK:TEMPO":0.5,"MODE:MODE":0.5,"MIXER:LEVEL 1":0.7,"MIXER:LEVEL 2":0.7,"CLOCK:DIV":0.5,"MIDI:CH B":sw(1,16)})

P=[
# ---------------------------------------------------------------- ACID: 16ths at 124-135 BPM, slides (PORTA) and accents (row C)
pat("ACID CIRCUIT",S("C3 C3 C4 C3 Eb3 C3 G3 Bb3 C3 F3 C4 Eb4"),C("1 .5 .5 .5 1 .5 .5 1 .5 .5 1 .5"),porta=(.09,0),bpm=126,vel=("FROM C","100"),
    desc="C minor 12-step acid line with octave jumps over a 16-step bar. Slides: PORTA A 0.09. "+ACC,
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF for the accents; PEAK high, EG 1 short DECAY, low SUSTAIN."),
pat("ACID GHOSTS",S("A1 - A2 A1 - C2 A1 E2 A1 - G2 C3"),C("1 0 .5 .45 0 1 .45 .5 1 0 .45 1"),law=("LIN","VOCT"),rng=(1,5),porta=(.05,0),bpm=128,vel=("FROM C","100"),
    desc="A minor bass for the linear input, with rests: a step at 0 V sends no MIDI note and stops a HZ/V VCO. RANGE 1 V spans the low octaves. Short PORTA 0.05 for small slides. "+ACC,
    ronin="CV A -> VCO:HZ/V (8'), GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; PEAK near self-oscillation, EG 1 fast ATTACK, short DECAY."),
pat("ACID SLIDE",S("D3 D3 D4 F3 D3 A3 C4 D3 G3 F3 D4 A3"),C(".4 0 1 .4 0 1 .4 .4 1 .4 0 .4"),porta=(.11,0),cmode="TIME",bpm=124,
    desc="D minor line phrased with gate length (C MODE = TIME): C 1.0 holds the gate 95 % so the PORTA 0.11 glide sings into the next note, 0.4 is a normal note, 0 a 5 % blip.",
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG with SUSTAIN up so long gates hold; PEAK high, CUTOFF low, EG 1 -> filter MOD."),
pat("ACID SEVENS",S("E3 E4 F3 E3 G3 E3 B3 E3 E3 E3 E3 E3"),C("1 .5 .5 1 .5 .45 1 .5 .5 .5 .5 .5"),quant=(0,1),porta=(.08,0),bpm=130,trig="PULSE",vel=("FROM C","100"),cables=[RESET(7)],
    desc="E phrygian 7-step loop (TRIG 8 -> RESET) that turns against the bar; TRIG jacks give 5 ms pulses. QUANT off (knobs sit exactly on the notes). PORTA 0.08. "+ACC,
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; resonant filter, snappy EG 1 (short DECAY, no SUSTAIN)."),
pat("ACID VOYAGE",S("G3 G3 G4 Bb3 G3 F3 G3 D4 G3 Bb3 C4 G3"),C("1 .5 .5 .5 1 .5 .5 1 .5 1 .5 .5"),b=S("Eb3 Eb3 Eb4 G3 Eb3 F3 G3 Bb3 D3 D4 F3 A3"),mode="A+B",
    porta=(.07,0),bpm=128,clock="HOST",vel=("FROM C","100"),
    desc="G minor 24-step acid line (A+B: row A, then row B, on the A jacks), locked to the DAW grid in 1/16 (HOST). PORTA 0.07. "+ACC,
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; resonant filter, short EG 1 DECAY into the filter."),
pat("ACID SERPENT",S("C3 Db3 C4 C3 Ab3 G3 C3 Db4 C3 Eb3 Bb3 C4"),C("1 .5 .5 .45 1 .5 .5 1 .5 .5 1 .5"),quant=(0,1),porta=(.1,0),bpm=135,settle="VINTAGE",vel=("FROM C","100"),
    desc="C phrygian line at 135 BPM, the flat second against octave jumps. SETTLE VINTAGE (0.6 ms), QUANT off, PORTA 0.10. "+ACC,
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; PEAK high, EG 1 short DECAY, a little drive from the mixer."),
pat("ACID TUMBLE",S("F3 F4 Ab3 F3 F3 C4 F3 Eb4 F3 F3 Ab3 Bb3"),C("1 .5 .5 .5 1 .5 1 .5 .5 .5 1 .5"),porta=(.08,0),bpm=132,vel=("FROM C","100"),cables=[SKIP(4),SKIP(9)],
    desc="F minor line with steps 4 and 9 skipped (TRIG 4 and TRIG 9 -> STEP): a 10-step loop, so the accents tumble across the bar. PORTA 0.08. "+ACC,
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; resonant filter, snappy EG 1."),
# ---------------------------------------------------------------- EDM: arps, basslines, stabs, plucks, leads
pat("GATED TRANCE",S("A3 C4 E4 A4 E4 C4 A3 C4 E4 A4 C5 E4"),C("1 .25 .6 .25 1 .25 .6 1 .25 .6 .25 .6"),cmode="TIME",bpm=138,
    desc="A minor arpeggio at 138 BPM, gated by row C (TIME): long and short gates make the trance stutter. 12 steps over a 16-step bar.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG; medium PEAK, EG 1 fast DECAY with some SUSTAIN."),
pat("SWING HOUSE",S("F3 F4 F3 Ab3 F3 C4 Eb4 F3 Bb3 F3 Ab3 C4"),C("0 .141268 0 .141268 0 .141268 0 .141268 0 .141268 0 .141268"),bpm=98.4,cables=[TEMPOCV],
    desc="F minor house bass with swing from row C: CV C -> TEMPO CV plays every second step 0.71 V faster, a 62/38 swing. The BPM readout shows the base tempo (98.4); the groove averages 122 BPM.",
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG; low PEAK, short DECAY, round filter."),
pat("TECHNO STABS",S("D3 D3 F3 D3 A3 D3 C4 D3 D3 G3 F3 A3"),C(".12 .12 .3 .12 .12 .5 .12 .12 .3 .12 .12 .6"),cmode="TIME",bpm=130,div="1/8",
    desc="D minor stabs in eighth notes at 130 BPM (1/8): short TIME gates with a few longer ones.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG; PEAK medium, EG 1 very short DECAY, EG 1 -> filter MOD."),
pat("PROG PLUCKS",S("B3 D4 F#4 B3 E4 D4 B3 F#4 A4 F#4 D4 E4"),C(".5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5"),b=S("B4 F#4 D5 B4 A4 F#4 E4 D4 F#4 A4 B4 C#5"),mode="ALT",
    bpm=124,div="1/8",clock="HOST",trig="PULSE",
    desc="B minor plucks in eighths, synced to the DAW (HOST, 1/8). ALT: row A on the A jacks (MIDI ch 1), then the answer an octave up on the B jacks (ch 2). TRIG PULSE.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG; short pluck: EG 1 fast DECAY, no SUSTAIN, filter MOD up."),
pat("CALL ANSWER",S("C3 C3 G3 C3 Eb3 F3 C3 C3 Bb3 C4 G3 F3"),C(".5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5"),b=S("G4 F4 Eb4 D4 C4 D4 F4 G4 A4 G4 Bb4 C5"),mode="ALT",
    rng=(1,5),quant=(1,0),porta=(0,.25),bpm=124,
    desc="C dorian call and response (ALT): a one-octave bass call on row A (RANGE 1 V, QUANT on, MIDI ch 1), a gliding lead answer on row B (RANGE 5 V, PORTA B 0.25, QUANT off, ch 2).",
    ronin="CV A -> VCO:V/OCT for the bass; CV B and GATE B to a second voice."),
pat("HORIZON 24",S("D4 F4 A4 G4 F4 E4 D4 C4 D4 E4 F4 A4"),C(".2 .3 .4 .5 .6 .7 .8 .7 .6 .5 .4 .3"),b=S("C5 B4 A4 G4 A4 F4 E4 G4 F4 E4 C4 D4"),mode="A+B",
    quant=(0,0),porta=(.18,0),bpm=120,
    desc="D dorian 24-step melody (A+B) with PORTA 0.18 glide, QUANT off. Row C is a slow rise-and-fall CV for a filter.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; long RELEASE, medium PEAK."),
pat("FIVEFOLD ARP",S("C4 Eb4 G4 Bb4 F4 C4 C4 C4 C4 C4 C4 C4"),C(".5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5"),bpm=126,div="1/32",trig="PULSE",cables=[RESET(5)],
    desc="C minor pentatonic 5-step arp in 1/32 notes at 126 BPM (TRIG 6 -> RESET), a fast figure that cycles against the beat. TRIG PULSE.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG; very short DECAY, bright filter."),
pat("ROLLING TEN",S("E2 E3 E2 B2 G2 E2 D3 E2 B2 G2 E2 E2"),C(".5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5"),law=("LIN","VOCT"),bpm=124,clock="HOST",cables=[RESET(10)],
    desc="E minor rolling bass for RONIN's linear input (HZ/V LIN, RANGE 5 V): a 10-step loop (TRIG 11 -> RESET) on the DAW grid (HOST, 1/16), so it rolls across the bar. Low MIDI notes (E2 = MIDI 40).",
    ronin="CV A -> VCO:HZ/V, GATE A -> EG 1:TRIG; low PEAK, short DECAY."),
pat("LINEAR LEAD",S("A3 C4 E4 A4 G#4 B4 A4 E4 F4 D5 C5 B4"),C(".55 .6 .7 1 .6 .7 .8 .55 .6 1 .8 .7"),law=("LIN","VOCT"),porta=(.2,0),bpm=128,div="1/8",settle="VINTAGE",vel=("FROM C","100"),
    desc="A harmonic minor lead in eighths for the linear input (HZ/V LIN): PORTA 0.2 glides exponentially in pitch. SETTLE VINTAGE. Velocity FROM C for dynamics.",
    ronin="CV A -> VCO:HZ/V, GATE A -> EG 1:TRIG, CV C -> VCA 2:CV for dynamics; slow ATTACK, long RELEASE."),
pat("RATCHET ROLL",S("G3 G4 Bb3 D4 D4 G3 F4 G3 D4 F4 G4 Bb4"),C("0 0 0 .2 .2 0 0 0 .4 .4 .4 .4"),bpm=128,cables=[TEMPOCV],
    desc="G minor line with ratchets from row C: CV C -> TEMPO CV doubles the clock on steps 4-5 (+1 V, a repeated note) and quadruples it on 9-12 (+2 V, a rising roll). The 12 steps fill 8 sixteenths.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG; very short DECAY so the rolls stay crisp."),
pat("GLASS PLUCKS",S("E3 G3 C4 G3 A3 E3 D3 G3 C3 A3 G3 D3"),C(".3 .5 .7 .5 .4 .6 .8 .5 .3 .6 .7 .4"),rng=(1,5),quant=(0,1),bpm=122,trig="PULSE",
    desc="C major pentatonic plucks in one octave (RANGE 1 V, QUANT off, knobs on the notes). Row C is a CV for brightness. TRIG PULSE.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG, CV C -> VCF:CUTOFF; short DECAY, bright, a little PEAK."),
pat("ANTHEM LEAD",S("C5 C5 A4 F4 G4 A4 Bb4 A4 G4 F4 G4 C4"),C(".8 .6 .7 .6 .75 .65 1 .7 .7 .6 .75 .9"),porta=(.15,0),bpm=128,vel=("FROM C","100"),
    desc="F major festival lead with PORTA 0.15 glide and velocity FROM C for phrasing.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG, CV C -> VCA 2:CV; open filter, long RELEASE."),
pat("DEEP PULSE",S("E3 E3 B3 E3 D4 E3 G3 E3 E3 B3 A3 G3"),C(".9 .1 .3 .1 .6 .1 .3 .1 .9 .1 .45 .2"),cmode="TIME",bpm=120,settle="VINTAGE",
    desc="E minor deep-house pulse at 120 BPM: long and short TIME gates, SETTLE VINTAGE.",
    ronin="CV A -> VCO:V/OCT (16'), GATE A -> EG 1:TRIG; low CUTOFF, medium PEAK, EG 1 -> filter MOD."),
pat("BROKEN ARP",S("G3 B3 G3 D4 F4 G4 G3 B4 D5 B4 G3 F4"),C(".5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5 .5"),bpm=133,trig="PULSE",cables=[SKIP(3),SKIP(7),SKIP(11)],
    desc="G mixolydian arp with steps 3, 7 and 11 skipped (TRIG -> STEP): a 9-step loop at 133 BPM that breaks across the bar. TRIG PULSE.",
    ronin="CV A -> VCO:V/OCT, GATE A -> EG 1:TRIG; short DECAY, resonant filter."),
]
BANK=[INIT]+[x[0] for x in P]
names=[p["name"] for p in BANK]; assert len(set(names))==len(names)
out=os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","assets","bushido_patterns.json")
json.dump(dict(note="Factory bank for the PATTERN screen, written by panel/build_patterns.py. Every pattern is format 1 and sets every parameter. "
               "Values are 0..1 parameter positions; cables are [jack, jack, colour] with BUSHIDO jack ids.",patterns=BANK),open(out,"w"),indent=1)
if __name__=="__main__":
    import sys
    if "--list" in sys.argv:
        for (q,d,r) in P: print(f"{q['name']}\n  {d}\n  RONIN: {r}")
    print(len(BANK),"patterns ->",os.path.normpath(out))
