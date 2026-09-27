"""One-week, bounded thumbnail cache. Photo credit/link travel with every packet."""
import os
import re
import struct
import time
from pathlib import Path
import extras

TTL=7*86400
MAX_FILES=256
REG=re.compile(r'[A-Z0-9][A-Z0-9-]{0,14}\Z')

def root(): return extras.CACHE_DIR/'photos'
def valid(packet):
    if len(packet)<392: return False
    magic,w,h=struct.unpack_from('<4sHH',packet)
    return magic==b'ECP1' and 0<w<=200 and 0<h<=150 and len(packet)==392+w*h*2 and b'\0' in packet[8:136] and b'\0' in packet[136:392]
def read(reg):
    if not REG.fullmatch(reg): raise ValueError('Invalid registration')
    path=root()/(reg+'.ecp')
    try:
        if time.time()-path.stat().st_mtime>=TTL: path.unlink(); return None
        if path.stat().st_size>60392: path.unlink(); return None
        packet=path.read_bytes()
        if not valid(packet): path.unlink(); return None
        return packet,packet[8:136].split(b'\0')[0].decode(),packet[136:392].split(b'\0')[0].decode()
    except (OSError,UnicodeError): return None

def write(reg,packet):
    if not REG.fullmatch(reg) or not valid(packet): raise ValueError('Invalid photo packet')
    directory=root(); directory.mkdir(parents=True,exist_ok=True)
    tmp=directory/(reg+'.tmp'); tmp.write_bytes(packet); tmp.replace(directory/(reg+'.ecp'))
    files=sorted(directory.glob('*.ecp'),key=lambda p:p.stat().st_mtime,reverse=True)
    for n,path in enumerate(files):
        if n>=MAX_FILES or time.time()-path.stat().st_mtime>=TTL: path.unlink(missing_ok=True)

def clear():
    count=0
    for path in root().glob('*.ecp'):
        path.unlink(missing_ok=True); count+=1
    return count
