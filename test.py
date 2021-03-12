import capnp
import Simulator_capnp
import matplotlib.pyplot as plt
from cmath import phase
import numpy as np

ckt = """* test
V1 1 0 AC 1 sin(0 5 1k)
R1 1 2 1k
C2 2 0 1u
.step dec R1 10 10000 3
*.tran 1u 2m 0
.ac dec 10 1 1e6
.end
"""
print(ckt)

sim = capnp.TwoPartyClient('localhost:5923').bootstrap().cast_as(Simulator_capnp.Simulator)
res = sim.loadFiles([{"name": "bar.sp", "contents": ckt}]).wait()

raw_vectors = res.commands[0].run.run(["V(*)", "I(*)", "FREQ"]).result.readAll().wait()
print(raw_vectors)
vectors = {}
for vec in raw_vectors.data:
    sim, name = vec.name.split('_', 1)
    vectors.setdefault(sim, {})[name] = vec.data

def map_complex(vec):
    return np.array([complex(v.real, v.imag) for v in vec.complex])

for step in vectors.values():
    if "TIME" in step:
        plt.figure(1)
        t = step['TIME'].real
        plt.plot(t, step['V(1)'].real)
        plt.plot(t, step['V(2)'].real)
    elif "FREQ" in step:
        plt.figure(2)
        f = np.real(map_complex(step['FREQ']))
        v2 = map_complex(step['V(2)'])

        plt.subplot(211)
        plt.loglog(f, np.abs(v2))
        plt.subplot(212)
        plt.semilogx(f, np.rad2deg(np.angle(v2)))
plt.show()