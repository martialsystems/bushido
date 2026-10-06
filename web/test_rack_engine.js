// Rack engine with several devices: two identical BUSHIDO+RONIN pairs sound exactly twice one pair; removing a device drops its cables.
// Run: node web/test_rack_engine.js
const W=__dirname+"/";global.SQ10=require(W+"sq10_dsp.js");global.MS50=require(W+"ms50_dsp.js");const RACK=require(W+"rack_engine.js");
// The page's "Play RONIN's VCO" patch: no feedback, so pairs are independent (with feedback only the rack's newest loop cable is delayed).
const p={preset:MS50.defaultPreset,power:true,params:{},knobs:{"EG 1:ATTACK":.05,"EG 1:DECAY":.4,"EG 1:SUSTAIN":.25,"EG 1:RELEASE":.3,"VCF:CUTOFF":.38,"VCF:PEAK":.55,"VCF:MOD":.6,"VCO:RANGE":.5,"OUTPUT:MIX":1,"OUTPUT:LEVEL":.7},
 cables:[["SQ-10/OUTPUTS:CV A","MS-50/VCO:HZ/V"],["SQ-10/OUTPUTS:GATE A","MS-50/EG 1:TRIG"],["MS-50/VCO:SAW","MS-50/VCF:IN"],["MS-50/VCF:OUT","MS-50/VCA 1:IN"],["MS-50/EG 1:OUT A","MS-50/VCA 1:ENV"],["MS-50/EG 1:OUT A","MS-50/VCF:CUTOFF"],["MS-50/VCA 1:OUT","MS-50/OUTPUT:WET"]]};
function pair(e,b,r){const mp=g=>g.replace("SQ-10/",b+"/").replace("MS-50/",r+"/");
  e.msg({t:"preset",d:r,i:p.preset});e.msg({t:"knobs",d:r,v:p.knobs});e.msg({t:"power",d:r,on:p.power});for(const id in p.params)e.msg({t:"param",d:b,id,v:p.params[id]});
  return p.cables.map(c=>c.map(mp))}
const N=48000,buf=()=>[new Float32Array(N),new Float32Array(N)];let ok=true;const t=(n,c)=>{console.log(n.padEnd(46),c?"PASS":"FAIL");ok=ok&&c};
const one=RACK.create(48000);one.msg({t:"devices",list:["SQ-10#1","MS-50#1"]});one.msg({t:"monitor",mode:"off"});one.msg({t:"cables",list:pair(one,"SQ-10#1","MS-50#1")});one.msg({t:"press",d:"SQ-10#1",id:"MODE:START/STOP"});
const two=RACK.create(48000);two.msg({t:"devices",list:["SQ-10#1","MS-50#1","SQ-10#2","MS-50#2"]});two.msg({t:"monitor",mode:"off"});
two.msg({t:"cables",list:pair(two,"SQ-10#1","MS-50#1").concat(pair(two,"SQ-10#2","MS-50#2"))});two.msg({t:"press",d:"SQ-10#1",id:"MODE:START/STOP"});two.msg({t:"press",d:"SQ-10#2",id:"MODE:START/STOP"});
const [a]=buf(),[b]=buf(),x=new Float32Array(N),y=new Float32Array(N);one.msg({t:"volume",v:.5});two.msg({t:"volume",v:.5});
one.render(a,x,N);two.render(b,y,N);let e=0,m=0;for(let i=0;i<N;i++){e=Math.max(e,Math.abs(2*a[i]-b[i]));m=Math.max(m,Math.abs(a[i]))}
t(`two pairs = 2 x one pair (peak ${m.toFixed(3)}, diff ${e.toExponential(2)})`,e<1e-6&&m>0.01);
const s=two.snapshot();t("state reports all four devices",Object.keys(s.devices).join()=="SQ-10#1,MS-50#1,SQ-10#2,MS-50#2"&&s.devices["SQ-10#2"].sq.running);
two.msg({t:"devices",list:["SQ-10#1","MS-50#1","SQ-10#2"]});two.render(b,y,N);t("removing RONIN 2 keeps pair 1 playing",Math.max(...b.map(Math.abs))>0.01);
const n0=two.setCables(pair(two,"SQ-10#1","MS-50#1").concat(pair(two,"SQ-10#2","MS-50#2")));t("cables to a missing device are skipped ("+n0+" of 14)",n0==7);
two.msg({t:"devices",list:[]});two.render(b,y,1000);t("empty rack is silent",b.slice(0,1000).every(v=>v===0));
const c=RACK.create(48000);c.msg({t:"devices",list:["MS-50#1","SQ-10#1"]});t("cross check: BUSHIDO gate into RONIN 1 EG TRIG legal",c.check("SQ-10#1/OUTPUTS:GATE A","MS-50#1/EG 1:TRIG").ok);
console.log(ok?"ALL PASS":"SOME FAIL");process.exit(ok?0:1);
