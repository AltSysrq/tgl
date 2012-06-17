#! /bin/bash
cp INSTALL README
echo >>README
echo 'Manual page follows:' >>README
echo >>README
MANWIDTH=80 man doc/tgl.1 >>README
