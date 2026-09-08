"""Print a per-cell colour map of a MAME screenshot.

Usage: python3 tools/analyze.py <png> [first row] [last row]

The KC 85 screen is 40x32 character cells.  Each cell prints as a letter
standing for its dominant colour, upper case when the cell holds more than
one colour, i.e. when something is actually drawn there.  Reading that is
much easier than eyeballing a 320x256 image when checking a layout.
"""
import zlib, sys
from struct import unpack
def readpng(p):
    d=open(p,'rb').read(); pos=8; idat=b''; w=h=0
    while pos<len(d):
        ln=int.from_bytes(d[pos:pos+4],'big'); typ=d[pos+4:pos+8]; data=d[pos+8:pos+8+ln]
        if typ==b'IHDR': w,h,bd,ct=unpack('>IIBB',data[:10])
        elif typ==b'IDAT': idat+=data
        pos+=12+ln
    raw=zlib.decompress(idat); px=[]; stride=w*3; prev=bytearray(stride); i=0
    for y in range(h):
        f=raw[i]; i+=1; line=bytearray(raw[i:i+stride]); i+=stride
        for x in range(stride):
            a=line[x-3] if x>=3 else 0; b=prev[x]; c=prev[x-3] if x>=3 else 0
            if f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+(a+b)//2)&255
            elif f==4:
                p_=a+b-c; pa=abs(p_-a); pb=abs(p_-b); pc=abs(p_-c)
                pr=a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[x]=(line[x]+pr)&255
        prev=line; px.append(bytes(line))
    return w,h,px
w,h,px=readpng(sys.argv[1])
r0,r1=(int(sys.argv[2]),int(sys.argv[3])) if len(sys.argv)>3 else (0,31)
pal={}
def key(c):
    if c not in pal: pal[c]=chr(ord('a')+len(pal))
    return pal[c]
for row in range(r0,r1+1):
    out=''
    for col in range(40):
        counts={}
        for y in range(row*8,row*8+8):
            for x in range(col*8,col*8+8):
                c=px[y][x*3:x*3+3]; counts[c]=counts.get(c,0)+1
        dom=max(counts,key=counts.get)
        ch=key(dom)
        out += ch.upper() if len(counts)>1 else ch
    print('%2d %s'%(row,out))
print({v:'#%02x%02x%02x'%tuple(k) for k,v in pal.items()})
