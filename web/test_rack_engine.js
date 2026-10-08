// Rack engine with several devices: two identical BUSHIDO+RONIN pairs sound exactly twice one pair; removing a device drops its cables.
// Run: node web/test_rack_engine.js
const W=__dirname+"/";global.BUSHIDO_DSP=require(W+"bushido_dsp.js");global.RONIN_DSP=require(W+"ronin_dsp.js");const RACK=require(W+"rack_engine.js");
// The page's "Play RONIN's VCO" patch: no feedback, so pairs are independent (with feedback only the rack's newest loop cable is delayed).
const p={preset:RONIN_DSP.defaultPreset,power:true,params:{},knobs:{"EG 1:ATTACK":.05,"EG 1:DECAY":.4,"EG 1:SUSTAIN":.25,"EG 1:RELEASE":.3,"VCF:CUTOFF":.38,"VCF:PEAK":.55,"VCF:MOD":.6,"VCO:RANGE":.5,"OUTPUT:MIX":1,"OUTPUT:LEVEL":.7},
 cables:[["BUSHIDO/OUTPUTS:CV A","RONIN/VCO:HZ/V"],["BUSHIDO/OUTPUTS:GATE A","RONIN/EG 1:TRIG"],["RONIN/VCO:SAW","RONIN/VCF:IN"],["RONIN/VCF:OUT","RONIN/VCA 1:IN"],["RONIN/EG 1:OUT A","RONIN/VCA 1:ENV"],["RONIN/EG 1:OUT A","RONIN/VCF:CUTOFF"],["RONIN/VCA 1:OUT","RONIN/OUTPUT:WET"]]};
function pair(e,b,r){const mp=g=>g.replace("BUSHIDO/",b+"/").replace("RONIN/",r+"/");
  e.msg({t:"preset",d:r,i:p.preset});e.msg({t:"knobs",d:r,v:p.knobs});e.msg({t:"power",d:r,on:p.power});for(const id in p.params)e.msg({t:"param",d:b,id,v:p.params[id]});
  return p.cables.map(c=>c.map(mp))}
const N=48000,buf=()=>[new Float32Array(N),new Float32Array(N)];let ok=true;const t=(n,c)=>{console.log(n.padEnd(46),c?"PASS":"FAIL");ok=ok&&c};
const one=RACK.create(48000);one.msg({t:"devices",list:["BUSHIDO#1","RONIN#1"]});one.msg({t:"monitor",mode:"off"});one.msg({t:"cables",list:pair(one,"BUSHIDO#1","RONIN#1")});one.msg({t:"press",d:"BUSHIDO#1",id:"MODE:START/STOP"});
const two=RACK.create(48000);two.msg({t:"devices",list:["BUSHIDO#1","RONIN#1","BUSHIDO#2","RONIN#2"]});two.msg({t:"monitor",mode:"off"});
two.msg({t:"cables",list:pair(two,"BUSHIDO#1","RONIN#1").concat(pair(two,"BUSHIDO#2","RONIN#2"))});two.msg({t:"press",d:"BUSHIDO#1",id:"MODE:START/STOP"});two.msg({t:"press",d:"BUSHIDO#2",id:"MODE:START/STOP"});
const [a]=buf(),[b]=buf(),x=new Float32Array(N),y=new Float32Array(N);one.msg({t:"volume",v:.5});two.msg({t:"volume",v:.5});
one.render(a,x,N);two.render(b,y,N);let e=0,m=0;for(let i=0;i<N;i++){e=Math.max(e,Math.abs(2*a[i]-b[i]));m=Math.max(m,Math.abs(a[i]))}
t(`two pairs = 2 x one pair (peak ${m.toFixed(3)}, diff ${e.toExponential(2)})`,e<1e-6&&m>0.01);
const s=two.snapshot();t("state reports all four devices",Object.keys(s.devices).join()=="BUSHIDO#1,RONIN#1,BUSHIDO#2,RONIN#2"&&s.devices["BUSHIDO#2"].sq.running);
two.msg({t:"devices",list:["BUSHIDO#1","RONIN#1","BUSHIDO#2"]});two.render(b,y,N);t("removing RONIN 2 keeps pair 1 playing",Math.max(...b.map(Math.abs))>0.01);
const n0=two.setCables(pair(two,"BUSHIDO#1","RONIN#1").concat(pair(two,"BUSHIDO#2","RONIN#2")));t("cables to a missing device are skipped ("+n0+" of 14)",n0==7);
two.msg({t:"devices",list:[]});two.render(b,y,1000);t("empty rack is silent",b.slice(0,1000).every(v=>v===0));
const c=RACK.create(48000);c.msg({t:"devices",list:["RONIN#1","BUSHIDO#1"]});t("cross check: BUSHIDO gate into RONIN 1 EG TRIG legal",c.check("BUSHIDO#1/OUTPUTS:GATE A","RONIN#1/EG 1:TRIG").ok);
console.log(ok?"ALL PASS":"SOME FAIL");process.exit(ok?0:1);
