"""Original DOCX formula sample for optional checks on a machine with desktop Word."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import sys
path=Path(sys.argv[1] if len(sys.argv)>1 else 'build-fixtures/Word-Formula-Sample.docx')
path.parent.mkdir(parents=True,exist_ok=True)
paragraphs='''
<w:p><w:r><w:t>Equation native DOCX: 数学、物理、化学公式测试</w:t></w:r></w:p>
<w:p><m:oMath><m:f><m:num><m:r><m:t>−b ± √(b² − 4ac)</m:t></m:r></m:num><m:den><m:r><m:t>2a</m:t></m:r></m:den></m:f></m:oMath></w:p>
<w:p><m:oMath><m:nary><m:naryPr><m:chr m:val="∫"/><m:limLoc m:val="subSup"/></m:naryPr><m:sub><m:r><m:t>0</m:t></m:r></m:sub><m:sup><m:r><m:t>1</m:t></m:r></m:sup><m:e><m:r><m:t>x² dx = ⅓</m:t></m:r></m:e></m:nary></m:oMath></w:p>
<w:p><w:r><w:t>α + β + γ = π</w:t></w:r></w:p>
<w:p><w:r><w:br w:type="page"/></w:r></w:p>
<w:p><w:r><w:t>Equation physics: E = mc², F = ma, ΔE = hν</w:t></w:r></w:p>
<w:p><m:oMath><m:sSup><m:e><m:r><m:t>E = mc</m:t></m:r></m:e><m:sup><m:r><m:t>2</m:t></m:r></m:sup></m:sSup></m:oMath></w:p>
<w:p><w:r><w:br w:type="page"/></w:r></w:p>
<w:p><w:r><w:t>Equation chemistry: H₂SO₄ + 2NaOH → Na₂SO₄ + 2H₂O</w:t></w:r></w:p>
<w:p><m:oMath><m:sSub><m:e><m:r><m:t>H</m:t></m:r></m:e><m:sub><m:r><m:t>2</m:t></m:r></m:sub></m:sSub><m:r><m:t>O</m:t></m:r></m:oMath></w:p>
'''
with ZipFile(path,'w',ZIP_DEFLATED) as archive:
    archive.writestr('[Content_Types].xml','''<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>''')
    archive.writestr('_rels/.rels','''<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>''')
    archive.writestr('word/document.xml','''<?xml version="1.0" encoding="UTF-8"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"><w:body>'''+paragraphs+'''<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr></w:body></w:document>''')
print(path)
