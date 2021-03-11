import capnp
import Simulator_capnp
import matplotlib.pyplot as plt

ckt = """* test
V1 1 0 sin(0 5 1k)
R1 1 2 50
R2 2 0 50
.tran 1u 1m 0
.end
"""

sim = capnp.TwoPartyClient('localhost:5923').bootstrap().cast_as(Simulator_capnp.Simulator)
res = sim.loadFiles([{"name": "bar.sp", "contents": ckt}]).wait()

raw_vectors = res.commands[0].run.run().result.readAll().wait()

vectors = {vec.name: vec.data for vec in raw_vectors.data}
t = vectors['TIME'].real
plt.plot(t, vectors['V(1)'].real, t, vectors['V(2)'].real)
plt.show()