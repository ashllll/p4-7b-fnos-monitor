#!/usr/bin/env python3
"""Anonymous PNG fixtures; no camera or device access."""
import importlib.util
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('photo_check',Path(__file__).with_name('photo_check.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
MAGIC=b'\x89PNG\r\n\x1a\n'
def chunk(kind,body=b''):
    return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
def header(w=1,h=1,depth=8,color=2,compression=0,filtering=0,interlace=0):
    return chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,depth,color,compression,filtering,interlace))
def png(raw=b'\0\x10\x20\x30',**kwargs):
    return MAGIC+header(**kwargs)+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND')
class PNG(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'fixture.png'
    def decode(self,data):
        self.path.write_bytes(data);return m.decode_png(str(self.path))
    def rejected(self,data):
        with self.assertRaises(SystemExit) as exc:self.decode(data)
        self.assertEqual(exc.exception.code,2)
    def test_supported_channels(self):
        for color,ch in ((0,1),(2,3),(4,2),(6,4)):
            with self.subTest(color=color):
                row=bytes(range(1,ch+1));self.assertEqual(self.decode(png(b'\0'+row,color=color)),(1,1,ch,[row]))
    def test_filters_reconstruct_exact_pixels(self):
        # Encoded second row [20,40,60], previous row [10,20,30], grayscale.
        encoded=(bytes([20,40,60]),bytes([20,20,20]),bytes([10,20,30]),bytes([15,20,25]),bytes([10,20,20]))
        for ft,row in enumerate(encoded):
            with self.subTest(filter=ft):
                result=self.decode(png(b'\0\x0a\x14\x1e'+bytes([ft])+row,w=3,h=2,color=0))
                self.assertEqual(result[3],[bytes([10,20,30]),bytes([20,40,60])])
    def test_bad_structure(self):
        good=png()
        cases=[b'',good[:-1],good[:-12],good+b'x',good[:20]+bytes([good[20]^1])+good[21:],
               MAGIC+header()+header()+good[33:],MAGIC+chunk(b'IDAT',b'x')+header()+chunk(b'IEND'),
               MAGIC+header()+chunk(b'ABCD')+good[33:],MAGIC+header()+chunk(b'IEND',b'x'),
               MAGIC+header()+chunk(b'IDAT',b'x')+chunk(b'tEXt',b'x')+chunk(b'IDAT',b'y')+chunk(b'IEND')]
        for data in cases:
            with self.subTest(size=len(data)):self.rejected(data)
    def test_unsupported_headers(self):
        for args in ({'depth':16},{'color':3},{'interlace':1},{'compression':1},{'filtering':1},{'w':0}):
            with self.subTest(args=args):self.rejected(png(**args))
    def test_bad_stream_and_exact_length(self):
        compressed=zlib.compress(b'\0\x10\x20\x30')
        for payload in (compressed[:-1],compressed+b'x',compressed+compressed,b'broken'):
            self.rejected(MAGIC+header()+chunk(b'IDAT',payload)+chunk(b'IEND'))
        for raw in (b'\0\x10',b'\0\x10\x20\x30X',b'\x05\x10\x20\x30'):
            self.rejected(png(raw))
    def test_bomb_uses_bounded_decompress(self):
        payload=zlib.compress(b'\0'*(1024*1024))
        real=zlib.decompressobj;limits=[]
        class Watch:
            def __init__(self):self.decoder=real()
            def decompress(self,data,limit=0):
                limits.append(limit);return self.decoder.decompress(data,limit)
            def __getattr__(self,name):return getattr(self.decoder,name)
        with patch.object(m.zlib,'decompressobj',Watch):
            self.rejected(MAGIC+header()+chunk(b'IDAT',payload)+chunk(b'IEND'))
        self.assertEqual(limits,[5]) # 1 RGB pixel + row filter + one excess byte.
        with patch.object(m,'MAX_DECODED_BYTES',3),patch.object(m.zlib,'decompressobj') as decode:
            self.rejected(png());decode.assert_not_called()
    def test_cli_requires_board_like_result_for_every_input(self):
        for raw,color,expected in ((b'\0\x10\x20\x30',2,0),(b'\0\xc8',0,1),(b'\0\xff\xc8\xc8',2,1)):
            self.path.write_bytes(png(raw,color=color))
            with patch.object(m.sys,'argv',['photo_check',str(self.path)]):self.assertEqual(m.main(),expected)
        with patch.object(m.sys,'argv',['photo_check']):self.assertEqual(m.main(),2)
    def test_transparency_and_step(self):
        self.path.write_bytes(png(b'\0\xff\0\0\0',color=6))
        with self.assertRaises(SystemExit):m.stats(str(self.path))
        self.path.write_bytes(png(b'\0\x10\x20\x30\xff',color=6))
        self.assertEqual(m.stats(str(self.path))[3],100)
        for step in (0,-1,1.5):
            with self.assertRaises(SystemExit):m.stats(str(self.path),step)
if __name__=='__main__':unittest.main()
