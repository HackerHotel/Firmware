#!./micropython/unix/micropython

# 'Emulator' to run badge micropython scripts on linux.
#
# Usage: './emulator/emulate.py micropython/esp32/modules/launcher.py'
#
# Needs this wrapper python script so the python module loader prefers the
# emulated module implementations in './emulator' over the 'real'
# implementations next to the to-be-emulated application.

import sys,os

print('Running',sys.argv[1],'in badge emulator')

dir = '/'.join(sys.argv[1].split('/')[:-1])
file = sys.argv[1].split('/')[-1]

sys.path.append(dir)

print('looking for',file,'in sys.path:',sys.path)

__import__(file)
print('yolo')
