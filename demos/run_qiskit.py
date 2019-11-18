from qiskit import QuantumCircuit
from qiskit.compiler import transpile
from qiskit.test.mock import FakeTokyo

import sys

basis = ['u3', 't', 'tdg', 's', 'sdg', 'cx', 'x', 'y', 'z', 'h']

circ = QuantumCircuit.from_qasm_file(sys.argv[1])
print("Original:\n  {}".format(circ.count_ops()))

# Optimize without mapping
circ1 = transpile(circ, basis_gates=basis, backend=None, seed_transpiler=11, optimization_level=3)
print("Optimized:\n  {}".format(circ1.count_ops()))

# Optimize and map
circ2 = transpile(circ, backend=FakeTokyo(), seed_transpiler=11, optimization_level=3)
print("Mapped:\n  {}".format(circ2.count_ops()))
