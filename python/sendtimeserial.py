import serial
import subprocess
ardu = serial.Serial('/dev/ttyUSB0',9600)
proc = subprocess.check_output("date +T%s", shell=True)
print "program output:", proc
# ardu.write('T1428413847\n')
ardu.write(proc)
