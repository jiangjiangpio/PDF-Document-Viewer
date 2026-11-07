"""Minimal original PDF to smoke-test the packaged Windows executable (stdlib only)."""
import sys
from pathlib import Path
objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [4 0 R 6 0 R 8 0 R] /Count 3 >>', b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
for index in range(3):
    width,height = ((792,612) if index==2 else (612,792))
    stream = (f'BT /F1 24 Tf 40 {height-70} Td (Equation page {index+1}: x + y = z) Tj ET\n0.7 w 40 100 400 200 re S\n40 100 m 440 300 l S\n').encode()
    content_index = len(objects)+2
    objects.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {width} {height}] /Resources << /Font << /F1 3 0 R >> >> /Contents {content_index} 0 R >>'.encode())
    objects.append(b'<< /Length '+str(len(stream)).encode()+b' >>\nstream\n'+stream+b'endstream')
data = bytearray(b'%PDF-1.4\n');offsets=[0]
for number,obj in enumerate(objects,1):
    offsets.append(len(data));data.extend(f'{number} 0 obj\n'.encode()+obj+b'\nendobj\n')
xref=len(data);data.extend(f'xref\n0 {len(objects)+1}\n0000000000 65535 f \n'.encode())
for offset in offsets[1:]:data.extend(f'{offset:010d} 00000 n \n'.encode())
data.extend(f'trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode())
Path(sys.argv[1]).write_bytes(data)
