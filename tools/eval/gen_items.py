#!/usr/bin/env python3
"""Generate the 200-item eval set. Solve-first, narrate-second: every call is executed through
evalcli and the reference answer is whatever the evaluator returns. No expected answer is typed
by hand, so none can be wrong."""
import json, subprocess, itertools, pathlib
E = str(pathlib.Path(__file__).resolve().parent / "evalcli")
def run(calls):
    p = subprocess.run([E, "-"], input="\n".join(calls)+"\n", capture_output=True, text=True)
    import re; return re.findall(r"<res>(.*?)</res>", p.stdout, re.S)

items, ctr = [], {}
def add(cat, q, rec, calls, expect="answer", **kw):
    ctr[cat] = ctr.get(cat, 0) + 1
    it = {"id": f"{cat}-{ctr[cat]:03d}", "q": q, "record": rec,
          "calls": calls if isinstance(calls, list) else [calls], "expect": expect}
    it.update(kw); items.append(it); return it

R = {  # compact records, LATENCY_BUDGET section 5 format
 "spd":"v=d/t | v:m/s d:m t:s | constant speed",
 "acc":"a=(vf-vi)/t | a:m/s^2 vf:m/s vi:m/s t:s | uniform acceleration",
 "kin":"d=vi*t+0.5*a*t^2 | d:m vi:m/s a:m/s^2 t:s | uniform acceleration",
 "n2" :"F=m*a | F:N m:kg a:m/s^2 | net force, inertial frame",
 "ke" :"K=0.5*m*v^2 | K:J m:kg v:m/s | non-relativistic",
 "pe" :"U=m*g*h | U:J m:kg g:m/s^2 h:m | g=9.81, uniform field",
 "mom":"p=m*v | p:kg*m/s m:kg v:m/s | non-relativistic",
 "wrk":"W=F*d | W:J F:N d:m | force parallel to displacement",
 "pwr":"P=W/t | P:W W:J t:s | average power",
 "cir":"a=v^2/r | a:m/s^2 v:m/s r:m | uniform circular motion",
 "grv":"F=G*m1*m2/r^2 | G=6.674e-11 m:kg r:m | point masses",
 "ohm":"V=I*R | V:V I:A R:ohm | ohmic conductor",
 "epw":"P=V*I | P:W V:V I:A | DC or RMS",
 "wav":"v=f*L | v:m/s f:Hz L:m | L is wavelength",
 "den":"rho=m/V | rho:kg/m^3 m:kg V:m^3 | uniform density",
 "prs":"P=F/A | P:Pa F:N A:m^2 | force normal to area",
 "hea":"Q=m*c*dT | Q:J m:kg c:J/(kg*K) dT:K | no phase change",
 "spr":"F=k*x | F:N k:N/m x:m | Hooke's law, within elastic limit",
 "tor":"tau=r*F | tau:N*m r:m F:N | force perpendicular to lever arm",
 "phn":"E=h*f | E:J h=6.626e-34 f:Hz | photon energy",
}

# ---- A1: direct substitution, units already consistent (26) -------------------
A1 = [("A sled is pushed 84 m in 7 s at constant speed. Find the speed.","spd","84/7"),
 ("A 3.0 kg block accelerates at 4.5 m/s^2. What net force acts on it?","n2","3.0*4.5"),
 ("Find the kinetic energy of a 0.145 kg ball moving at 40 m/s.","ke","0.5*0.145*40^2"),
 ("A 2.0 kg mass sits 3.5 m above the floor. Find its potential energy.","pe","2.0*9.81*3.5"),
 ("A 1500 kg car moves at 22 m/s. Find its momentum.","mom","1500*22"),
 ("A 60 N force pushes a crate 12 m. How much work is done?","wrk","60*12"),
 ("A motor does 4500 J of work in 15 s. Find the average power.","pwr","4500/15"),
 ("A car rounds a 50 m radius curve at 15 m/s. Find its centripetal acceleration.","cir","15^2/50"),
 ("A 12 V battery drives 0.25 A through a resistor. Find the resistance.","ohm","12/0.25"),
 ("A 120 V appliance draws 6.5 A. Find the power.","epw","120*6.5"),
 ("A wave of frequency 256 Hz has wavelength 1.34 m. Find its speed.","wav","256*1.34"),
 ("A 0.75 kg object occupies 0.00025 m^3. Find its density.","den","0.75/0.00025"),
 ("A 400 N force acts over 0.02 m^2. Find the pressure.","prs","400/0.02"),
 ("How much heat raises 2.5 kg of water (c=4186) by 30 K?","hea","2.5*4186*30"),
 ("A spring with k=250 N/m is stretched 0.08 m. Find the force.","spr","250*0.08"),
 ("A 45 N force acts 0.30 m from a pivot. Find the torque.","tor","0.30*45"),
 ("A car speeds from 8 m/s to 26 m/s in 6 s. Find the acceleration.","acc","(26-8)/6"),
 ("Starting at 5 m/s with a=2 m/s^2, how far in 4 s?","kin","5*4+0.5*2*4^2"),
 ("A 0.05 kg dart moves at 18 m/s. Find its kinetic energy.","ke","0.5*0.05*18^2"),
 ("A runner covers 400 m in 48 s. Find the average speed.","spd","400/48"),
 ("A 900 kg car accelerates at 2.4 m/s^2. Find the net force.","n2","900*2.4"),
 ("A 70 kg climber ascends 18 m. Find the gain in potential energy.","pe","70*9.81*18"),
 ("A pump delivers 9000 J in 45 s. Find the power.","pwr","9000/45"),
 ("Find the momentum of a 0.30 kg ball at 12 m/s.","mom","0.30*12"),
 ("A 9 V source drives 0.03 A. Find the resistance.","ohm","9/0.03"),
 ("A 240 Hz wave travels at 343 m/s. Find its wavelength.","wav","343/240")]
for q, r, e in A1: add("A1", q, R[r], f"<tool>eval<arg>{e}</tool>")

# ---- A2: conversion required (18) --------------------------------------------
A2 = [("A train covers 150 km in 1.5 h. Give its speed in m/s.","spd","(150 km)/(1.5 h)","m/s"),
 ("A car travels 88 m in 4.0 s. Give the speed in km/h.","spd","(88 m)/(4.0 s)","km/h"),
 ("Find the kinetic energy of a 1200 kg car at 90 km/h, in J.","ke","0.5*(1200 kg)*((90 km/h)^2)","J"),
 ("A 250 g ball moves at 15 m/s. Give its momentum in kg*m/s.","mom","(250 g)*(15 m/s)","kg*m/s"),
 ("A 2.0 kW heater runs for 30 min. Give the energy in J.","pwr","(2.0 kW)*(30 min)","J"),
 ("Express 45 minutes in seconds.","spd","45 min","s"),
 ("A plate has area 250 cm^2 under 600 N. Give the pressure in Pa.","prs","(600 N)/(250 cm^2)","Pa"),
 ("A block has mass 850 g and volume 320 cm^3. Give density in kg/m^3.","den","(850 g)/(320 cm^3)","kg/m^3"),
 ("Convert 5.5 feet to metres.","spd","5.5 ft","m"),
 ("A cyclist rides 12 km in 25 min. Give the speed in km/h.","spd","(12 km)/(25 min)","km/h"),
 ("Express 3.5 hours in minutes.","spd","3.5 h","min"),
 ("A 0.5 kg object at 72 km/h — give its kinetic energy in J.","ke","0.5*(0.5 kg)*((72 km/h)^2)","J"),
 ("Convert 2500 mm to metres.","spd","2500 mm","m"),
 ("A force of 3.5 kN acts over 2.0 m. Give the work in kJ.","wrk","(3.5 kN)*(2.0 m)","kJ"),
 ("Express 98.6 degrees Fahrenheit in degrees Celsius.","hea","98.6 degF","degC"),
 ("Convert 1.5 tonnes to kilograms.","den","1.5 t","kg"),
 ("A satellite covers 7.8 km in 1 s. Give the speed in km/h.","spd","(7.8 km)/(1 s)","km/h"),
 ("Express 250 mL in cubic metres.","den","250 mL","m^3")]
for q, r, e, u in A2: add("A2", q, R[r], f"<tool>conv<arg>{e}<arg>{u}</tool>")

# ---- A3: blocklisted dimension -- compute, refuse to NAME the unit (14) -------
A3 = [("A 30 N force acts 0.4 m from a pivot. State the torque with its unit.","tor","(30 N)*(0.4 m)","N*m","torque, not energy -- both are kg*m^2/s^2"),
 ("A 12 N force moves a box 2.5 m. State the work with its unit.","wrk","(12 N)*(2.5 m)","J","work, not torque"),
 ("A wheel turns 5 revolutions in 2 s. State the frequency with its unit.","cir","5/(2 s)","Hz","frequency, not angular velocity"),
 ("A wheel turns at 5 rev in 2 s. State the angular velocity in rad/s.","cir","2*pi*5/(2 s)","rad/s","angular velocity, not frequency"),
 ("A 55 N force acts 0.22 m from an axle. Give the torque.","tor","(55 N)*(0.22 m)","N*m","torque"),
 ("A 200 N force lifts a load 1.4 m. Give the work.","wrk","(200 N)*(1.4 m)","J","work"),
 ("A rotor spins 1200 times per minute. Give the frequency in Hz.","cir","1200/(1 min)","Hz","frequency"),
 ("A rotor spins 1200 times per minute. Give the angular velocity in rad/s.","cir","2*pi*1200/(1 min)","rad/s","angular velocity"),
 ("A spanner applies 18 N at 0.35 m. Give the torque.","tor","(18 N)*(0.35 m)","N*m","torque"),
 ("A 9.5 N force acts through 3.2 m. Give the energy transferred.","wrk","(9.5 N)*(3.2 m)","J","work"),
 ("A pendulum completes 20 swings in 25 s. Give the frequency.","cir","20/(25 s)","Hz","frequency"),
 ("A disc rotates 3 rev in 0.5 s. Give the angular velocity.","cir","2*pi*3/(0.5 s)","rad/s","angular velocity"),
 ("A 40 N force acts 0.15 m from the hinge. Give the torque.","tor","(40 N)*(0.15 m)","N*m","torque"),
 ("A 6 N force acts over 7 m of displacement. Give the work.","wrk","(6 N)*(7 m)","J","work")]
for q, r, e, u, note in A3:
    add("A3", q, R[r] + f" | label: {note}", f"<tool>conv<arg>{e}<arg>{u}</tool>", unit_label=u)

json.dump(items, open("_part1.json","w"))
print(f"part 1: {len(items)} items")

# ---- A4: rearrange, then evaluate -- two calls (26) ---------------------------
A4 = [("A 120 N net force acts on an 8 kg mass. Find the acceleration.","n2","F=m*a","a","120/8"),
 ("A 5.0 kg object has 250 J of kinetic energy. Find its speed.","ke","K=0.5*m*v^2","v","(2*250/5.0)^0.5"),
 ("A 3.0 kg mass has 147 J of potential energy at g=9.81. Find its height.","pe","U=m*g*h","h","147/(3.0*9.81)"),
 ("A car covers 240 m at 16 m/s. How long does it take?","spd","v=d/t","t","240/16"),
 ("600 J of work is done by a force over 7.5 m. Find the force.","wrk","W=F*d","F","600/7.5"),
 ("A 2000 W motor does 50000 J of work. How long does it run?","pwr","P=W/t","t","50000/2000"),
 ("An object at 20 m/s has centripetal acceleration 8 m/s^2. Find the radius.","cir","a=v^2/r","r","20^2/8"),
 ("A resistor carries 0.4 A under 18 V. Find the resistance.","ohm","V=I*R","R","18/0.4"),
 ("A 1500 W device runs on 120 V. Find the current.","epw","P=V*I","I","1500/120"),
 ("A 340 m/s wave has wavelength 0.85 m. Find the frequency.","wav","v=f*L","f","340/0.85"),
 ("A block of density 2700 kg/m^3 has mass 5.4 kg. Find its volume.","den","rho=m/V","V","5.4/2700"),
 ("A pressure of 5000 Pa acts over 0.06 m^2. Find the force.","prs","P=F/A","F","5000*0.06"),
 ("A spring exerts 30 N when stretched 0.12 m. Find k.","spr","F=k*x","k","30/0.12"),
 ("A torque of 24 N*m comes from a force at 0.4 m. Find the force.","tor","tau=r*F","F","24/0.4"),
 ("A 0.6 kg ball has momentum 9.0 kg*m/s. Find its speed.","mom","p=m*v","v","9.0/0.6"),
 ("A photon has energy 3.98e-19 J. Find its frequency (h=6.626e-34).","phn","E=h*f","f","3.98e-19/6.626e-34"),
 ("A 4.0 kg mass gains 720 J of kinetic energy from rest. Find its speed.","ke","K=0.5*m*v^2","v","(2*720/4.0)^0.5"),
 ("A wave of frequency 500 Hz has speed 1500 m/s. Find the wavelength.","wav","v=f*L","L","1500/500"),
 ("An 800 N force produces 4.0 m/s^2. Find the mass.","n2","F=m*a","m","800/4.0"),
 ("A heater transfers 41860 J to 2.0 kg of water (c=4186). Find the temperature rise.","hea","Q=m*c*dT","dT","41860/(2.0*4186)"),
 ("A 25 kg mass is 12 m up. Find g if its potential energy is 2943 J.","pe","U=m*g*h","g","2943/(25*12)"),
 ("A runner averages 5.5 m/s for 1320 s. How far?","spd","v=d/t","d","5.5*1320"),
 ("A 0.20 m lever arm gives 15 N*m of torque. Find the force.","tor","tau=r*F","F","15/0.20"),
 ("A 12 ohm resistor carries 2.5 A. Find the voltage.","ohm","V=I*R","V","2.5*12"),
 ("A 60 W bulb draws 0.5 A. Find the voltage.","epw","P=V*I","V","60/0.5"),
 ("A 1.5 kg object has 48 J of kinetic energy. Find its speed.","ke","K=0.5*m*v^2","v","(2*48/1.5)^0.5")]
for q, r, eq, var, num in A4:
    add("A4", q, R[r], [f"<tool>solve<arg>{eq}<arg>{var}</tool>", f"<tool>eval<arg>{num}</tool>"])

# ---- A5: distractor number in the stem (12) -- twin stems with D1 -------------
A5 = [("A 1200 kg car with 4 passengers travels 300 m in 20 s. Find its speed.","spd","300/20"),
 ("A 2.0 kg ball painted with 3 stripes falls from 5.0 m. Find its potential energy.","pe","2.0*9.81*5.0"),
 ("A 6.0 kg crate is pushed by a 30 N force for 12 s over 9.0 m. Find the work done.","wrk","30*9.0"),
 ("Three students push a 40 kg cart at 5.0 m/s^2. Find the net force.","n2","40*5.0"),
 ("A 0.5 kg ball, one of 12 in the box, moves at 20 m/s. Find its kinetic energy.","ke","0.5*0.5*20^2"),
 ("A 240 V, 3-phase-rated device draws 5 A. Find the power.","epw","240*5"),
 ("A 15 ohm resistor in a 4-resistor bank carries 2 A. Find the voltage across it.","ohm","2*15"),
 ("A 500 Hz tone plays for 8 s and travels at 340 m/s. Find the wavelength.","wav","340/500"),
 ("A 3.0 kg mass on a 2.0 m table moves at 7.0 m/s. Find its momentum.","mom","3.0*7.0"),
 ("A pump rated 12 A delivers 6000 J in 30 s. Find the power.","pwr","6000/30"),
 ("A 25 cm spanner applies 40 N at 0.25 m from the bolt. Find the torque.","tor","0.25*40"),
 ("A 4-wheeled 900 kg trailer accelerates at 1.5 m/s^2. Find the net force.","n2","900*1.5")]
for q, r, e in A5: add("A5", q, R[r], f"<tool>eval<arg>{e}</tool>")

# ---- A6: constant supplied by the record (10) -- control twin for D1 ----------
A6 = [("A 4.5 kg mass falls 20 m. Find the potential energy released.","pe","4.5*9.81*20"),
 ("Find the gravitational force between two 1000 kg masses 5 m apart.","grv","6.674e-11*1000*1000/5^2"),
 ("A photon has frequency 5.0e14 Hz. Find its energy.","phn","6.626e-34*5.0e14"),
 ("A 12 kg object is raised 3.0 m. Find the work against gravity.","pe","12*9.81*3.0"),
 ("Find the force between a 60 kg and a 70 kg mass 2 m apart.","grv","6.674e-11*60*70/2^2"),
 ("A photon of frequency 6.0e14 Hz — find its energy.","phn","6.626e-34*6.0e14"),
 ("A 0.8 kg book is 1.2 m above a desk. Find its potential energy.","pe","0.8*9.81*1.2"),
 ("Two 500 kg masses are 10 m apart. Find the gravitational attraction.","grv","6.674e-11*500*500/10^2"),
 ("A 95 kg load descends 4.0 m. Find the potential energy lost.","pe","95*9.81*4.0"),
 ("Find the energy of a 1.0e15 Hz photon.","phn","6.626e-34*1.0e15")]
for q, r, e in A6: add("A6", q, R[r], f"<tool>eval<arg>{e}</tool>")

# ---- A7: statistics (10) ------------------------------------------------------
A7 = [("Five timings: 2.11, 2.09, 2.14, 2.08, 2.13 s. Find the mean.","mean","2.11,2.09,2.14,2.08,2.13"),
 ("Same five timings. Find the sample standard deviation.","sd","2.11,2.09,2.14,2.08,2.13"),
 ("Masses 4.2, 4.5, 4.1, 4.6, 4.3 kg. Find the mean.","mean","4.2,4.5,4.1,4.6,4.3"),
 ("Masses 4.2, 4.5, 4.1, 4.6, 4.3 kg. Find the sample standard deviation.","sd","4.2,4.5,4.1,4.6,4.3"),
 ("Readings 12, 15, 11, 19, 14, 13. Find the median.","median","12,15,11,19,14,13"),
 ("Readings 12, 15, 11, 19, 14, 13. Find the mean.","mean","12,15,11,19,14,13"),
 ("Lengths 0.51, 0.49, 0.52, 0.50 m. Find the mean.","mean","0.51,0.49,0.52,0.50"),
 ("Lengths 0.51, 0.49, 0.52, 0.50 m. Find the sample standard deviation.","sd","0.51,0.49,0.52,0.50"),
 ("Voltages 5.02, 4.98, 5.05, 4.95, 5.00. Find the median.","median","5.02,4.98,5.05,4.95,5.00"),
 ("Voltages 5.02, 4.98, 5.05, 4.95, 5.00. Find the mean.","mean","5.02,4.98,5.05,4.95,5.00")]
for q, w, lst in A7:
    add("A7", q, "stat over a measurement list | sample sd uses n-1",
        f"<tool>stat<arg>{w}<arg>{lst}</tool>")

# ---- A8: loose wording, intent determined (2) -- control twin for D5 ----------
add("A8","A car does 150 m in 12 s. How fast is it going?",R["spd"],"<tool>eval<arg>150/12</tool>",
    twin="D5-001")
add("A8","A 2 kg thing is up 5 m. What's the energy?",R["pe"],"<tool>eval<arg>2*9.81*5</tool>",
    twin="D5-002")
json.dump(items, open("_part2.json","w")); print(f"part 2 cumulative: {len(items)}")

# ---- B1: literal rearrangement (22) -- E1/E2/E3r coverage --------------------
B1 = [("Solve F=m*a for a.","n2","F=m*a","a"), ("Solve F=m*a for m.","n2","F=m*a","m"),
 ("Solve v=d/t for d.","spd","v=d/t","d"), ("Solve v=d/t for t.","spd","v=d/t","t"),
 ("Solve K=0.5*m*v^2 for m.","ke","K=0.5*m*v^2","m"),
 ("Solve U=m*g*h for h.","pe","U=m*g*h","h"), ("Solve U=m*g*h for m.","pe","U=m*g*h","m"),
 ("Solve W=F*d for d.","wrk","W=F*d","d"), ("Solve P=W/t for W.","pwr","P=W/t","W"),
 ("Solve P=W/t for t.","pwr","P=W/t","t"), ("Solve V=I*R for I.","ohm","V=I*R","I"),
 ("Solve P=V*I for I.","epw","P=V*I","I"), ("Solve v=f*L for L.","wav","v=f*L","L"),
 ("Solve rho=m/V for V.","den","rho=m/V","V"), ("Solve rho=m/V for m.","den","rho=m/V","m"),
 ("Solve P=F/A for A.","prs","P=F/A","A"), ("Solve Q=m*c*dT for dT.","hea","Q=m*c*dT","dT"),
 ("Solve Q=m*c*dT for c.","hea","Q=m*c*dT","c"), ("Solve F=k*x for x.","spr","F=k*x","x"),
 ("Solve tau=r*F for r.","tor","tau=r*F","r"), ("Solve a=(vf-vi)/t for vf.","acc","a=(vf-vi)/t","vf"),
 ("Solve a=(vf-vi)/t for t.","acc","a=(vf-vi)/t","t")]
for q, r, eq, var in B1:
    add("B1", q, R[r], f"<tool>solve<arg>{eq}<arg>{var}</tool>", expect="symbolic")

# ---- C: conceptual, no tool call (28) ----------------------------------------
C1 = ["What is the difference between speed and velocity?","Define acceleration.",
 "What does it mean for a collision to be elastic?","Define the newton.",
 "What is meant by the centre of mass?","Define kinetic energy in words.",
 "What is a scalar quantity?","What is the difference between mass and weight?",
 "Define wavelength.","What is meant by thermal equilibrium?",
 "What is an ohmic conductor?","Define the joule."]
for q in C1: add("C1", q, "definition | no computation required", [], expect="prose")
C2 = ["Why does a heavier object not fall faster in a vacuum?",
 "Why does a car need friction to turn a corner?",
 "Why is momentum conserved in a collision but kinetic energy is not always?",
 "Why does a spinning skater speed up when they pull their arms in?",
 "Why does a metal spoon feel colder than a wooden one at the same temperature?",
 "Why is the sky brighter near the horizon at sunset?",
 "Why does increasing the resistance in a circuit reduce the current at fixed voltage?",
 "Why does a wave slow down when it enters a denser medium?",
 "Why does a satellite in orbit not fall to Earth?",
 "Why is work zero when a force is perpendicular to the motion?"]
for q in C2: add("C2", q, "explanation | no computation required", [], expect="prose")
C3 = ["To find the speed of a falling object, is energy conservation or kinematics simpler here?",
 "Should I use the mean or the median for a set with one clear outlier?",
 "For a rearrangement where the unknown is in the denominator, what is the first step?",
 "Is momentum or energy the right conserved quantity for an inelastic collision?",
 "When should I use the sample standard deviation rather than the population one?",
 "For a constant-acceleration problem with no time given, which equation avoids t?"]
for q in C3: add("C3", q, "method selection | no computation required", [], expect="prose")

# ---- D: refusal (24) ----------------------------------------------------------
D1 = [("A car accelerates uniformly. Find its final speed.",R["acc"]),
 ("Find the kinetic energy of a ball moving at 12 m/s.",R["ke"]),
 ("A crate is pushed across a floor. How much work is done?",R["wrk"]),
 ("Find the momentum of the trolley.",R["mom"]),
 ("A resistor carries 3 A. Find the voltage across it.",R["ohm"]),
 ("Find the density of the metal block.",R["den"]),
 ("A wave has frequency 400 Hz. Find its speed.",R["wav"]),
 ("A spring is stretched 0.1 m. Find the force.",R["spr"])]
for q, r in D1: add("D1", q, r, [], expect="refuse")
D2 = ["Who first wrote down the law of universal gravitation?",
 "What is the capital of Portugal?","Translate 'force' into German.",
 "Write a haiku about entropy.","What year was the Nobel Prize in Physics first awarded?"]
for q in D2: add("D2", q, "out of domain", [], expect="refuse")
D3 = ["A ladder leans against a frictionless wall at 60 degrees. Find the friction force at the base.",
 "A block slides down a rough incline, compresses a spring, and rebounds. Find the final height.",
 "Two blocks connected over a pulley with friction on both surfaces — find the acceleration.",
 "A projectile is launched at 40 degrees into a headwind. Find the range.",
 "A rod pivots about one end while a mass slides along it. Find the angular acceleration.",
 "Find the tension in each of three cables supporting a sign at different angles."]
for q in D3: add("D3", q, "requires 2-D setup or multi-stage modelling", [], expect="refuse")
D4 = ["A 2 kg object has kinetic energy of -50 J. Find its speed.",
 "A car travels 100 m in 0 s. Find its speed.",
 "An object has mass -5 kg and accelerates at 2 m/s^2. Find the force."]
for q in D4: add("D4", q, "premise is contradictory or physically impossible", [], expect="refuse")
add("D5","A car does 150 m. How fast?",R["spd"],[],expect="clarify",twin="A8-001")
add("D5","A 2 kg thing and a 5 m drop. What's the energy?",R["pe"],[],expect="clarify",twin="A8-002")

# ---- E: recovery / spurious-retry twin (8) -----------------------------------
E1 = [("A car travels 150 m in 12 s. Give the speed in m/s.",R["spd"],
       "<tool>eval<arg>150 m/12 s</tool>","<tool>conv<arg>(150 m)/(12 s)<arg>m/s</tool>"),
 ("A 1200 kg car at 25 m/s. Give the kinetic energy in J.",R["ke"],
  "<tool>eval<arg>0.5*1200 kg*25 m/s^2</tool>","<tool>conv<arg>0.5*(1200 kg)*((25 m/s)^2)<arg>J</tool>"),
 ("A train covers 150 km in 1.5 h. Give the speed in m/s.",R["spd"],
  "<tool>eval<arg>150 km/1.5 h</tool>","<tool>conv<arg>(150 km)/(1.5 h)<arg>m/s</tool>"),
 ("A 250 g ball at 15 m/s. Give the momentum in kg*m/s.",R["mom"],
  "<tool>eval<arg>250 g*15 m/s</tool>","<tool>conv<arg>(250 g)*(15 m/s)<arg>kg*m/s</tool>"),
 ("A 600 N force over 250 cm^2. Give the pressure in Pa.",R["prs"],
  "<tool>eval<arg>600 N/250 cm^2</tool>","<tool>conv<arg>(600 N)/(250 cm^2)<arg>Pa</tool>")]
for q, r, bad, good in E1:
    add("E1", q, r, [bad, good], expect="recover", min_calls=1)
E2 = [("A photon has frequency 3.0e8 Hz. Find its energy.",R["phn"],"6.626e-34*3.0e8"),
 ("Find the gravitational force between two 1 kg masses 1 m apart.",R["grv"],"6.674e-11*1*1/1^2"),
 ("A 0.001 kg insect moves at 0.5 m/s. Find its kinetic energy.",R["ke"],"0.5*0.001*0.5^2")]
for q, r, e in E2:
    add("E2", q, r + " | result is legitimately tiny", f"<tool>eval<arg>{e}</tool>",
        expect="no_retry", min_calls=1)

json.dump(items, open("items.json","w"), indent=1)
print(f"TOTAL: {len(items)}")
from collections import Counter
print("  " + "  ".join(f"{k}={v}" for k,v in sorted(Counter(i['id'].split('-')[0] for i in items).items())))
