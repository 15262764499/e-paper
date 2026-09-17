"""Compare the compiled MCU effect against the shipped desktop reference."""
import ctypes
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'desktop'))
from pulse.effect import duties

build = root/'tmp'
build.mkdir(exist_ok=True)
source = build/'pulse_effect_bridge.c'
source.write_text('''#include "pulse_effect.h"
__declspec(dllexport) void calculate(unsigned t, unsigned value, unsigned brightness,
                                   unsigned speed, unsigned char *out) {
  PulseEffect effect={0};
  PulseEffect_Set(&effect,0,(uint16_t)value,(uint8_t)brightness,(uint8_t)speed);
  PulseEffect_Duty(&effect,t,out);
}
''', encoding='utf-8')
library = build/'pulse_effect_test.dll'
subprocess.run(['gcc','-shared','-std=c11','-Wall','-Wextra','-Werror','-ICore/Inc',
                str(source),'-o',str(library)],cwd=root,check=True)
calculate=ctypes.CDLL(str(library)).calculate
calculate.argtypes=[ctypes.c_uint]*4+[ctypes.POINTER(ctypes.c_ubyte)]
calculate.restype=None
result=(ctypes.c_ubyte*8)()
count=0
for value in (0,1,1999,2000,2001,5000,7999,9999,10000):
    for brightness in (0,37,80,100):
        for speed in (50,100,137,200):
            for tick in range(0,3000,10):
                calculate(tick,value,brightness,speed,result)
                expected=duties(tick,value/100,brightness,speed)
                assert result[0]==0
                # Integer half-up vs Python ties-to-even can differ by one duty step.
                assert all(abs(a-b)<=1 for a,b in zip(result[1:],expected)), (tick,value,brightness,speed,list(result),expected)
                count+=1
print(f'PASS: {count} MCU effect samples match desktop within one duty step')
