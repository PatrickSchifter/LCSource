# contact sheet of the saved frames: rows of Metal | Vulkan pairs, as a PNG
import sys, glob, os, zlib, struct
def readppm(f):
    b = open(f,'rb').read(); i = b.index(b'255\n')+4
    w, h = map(int, b[3:i-4].split()[:2]); return w, h, b[i:]
names = sys.argv[3:]
W, H = 256, 192; cols = 2; gap = 4
rows = len(names)
out = bytearray((W*cols+gap)*3*(H+gap)*rows)
stride = (W*cols+gap)*3
for r, n in enumerate(names):
    for c, api in enumerate(['metal','vulkan']):
        w, h, d = readppm(os.path.join(sys.argv[1], n + '.' + api + '.ppm'))
        for y in range(H):
            o = (r*(H+gap)+y)*stride + c*(W+gap)*3
            out[o:o+W*3] = d[y*W*3:(y+1)*W*3]
rowsz = stride
raw = b''.join(b'\0' + bytes(out[y*rowsz:(y+1)*rowsz]) for y in range((H+gap)*rows))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W*cols+gap, (H+gap)*rows, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
