OPENQASM 2.0;
include "qelib1.inc";

qreg a[1];
qreg b[2];
creg c[1];
creg d[1];

// Prepare bell state
h b[0];
cx b[0],b[1];

// Prepare teleported state
h a[0];

// Perform measurement
cx a[0],b[0];
h a[0];
measure a[0] -> c[0];
measure b[0] -> d[0];

// Classically controlled correction
if (d==1) x b[1];
if (c==1) z b[1];