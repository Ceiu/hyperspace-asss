#!/usr/bin/env python
import sys,socket
h, p = sys.argv[1].split(':')
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect((h, int(p)))
s.send('abcd')
s.settimeout(2.0)
try:
  o = s.recv(8)
  print ord(o[0])+256*ord(o[1])
except socket.timeout:
  print '-'
