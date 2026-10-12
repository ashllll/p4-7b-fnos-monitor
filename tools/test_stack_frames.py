#!/usr/bin/env python3
"""使用 objdump 输出夹具验证大小帧与无法判定的栈调整。"""
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import stack_check

class Frames(unittest.TestCase):
    def load(self, instructions):
        text = '1000 <task>:\n' + ''.join(f' {0x1000+i*4:x}: {op}\n' for i, op in enumerate(instructions))
        with patch.object(stack_check.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout=text)):
            return stack_check.load('fixture.elf', 'objdump')[0]['task']

    def test_immediate_and_split_frame(self):
        self.assertEqual(self.load(['addi sp,sp,-2032', 'addi sp,sp,-32', 'addi sp,sp,32', 'addi sp,sp,2032', 'ret']), 2064)

    def test_large_positive_register(self):
        self.assertEqual(self.load(['addi sp,sp,-32', 'lui t0,0x2', 'addi t0,t0,-16', 'sub sp,sp,t0', 'add sp,sp,t0', 'addi sp,sp,32', 'ret']), 8208)

    def test_large_negative_register(self):
        self.assertEqual(self.load(['lui a5,0xffffd', 'addi a5,a5,16', 'add sp,sp,a5']), 12272)
        self.assertEqual(self.load(['li t0,4096', 'mv a5,t0', 'sub sp,sp,a5']), 4096)

    def test_branch_merge_and_alignment(self):
        # 两条来源（1或2）汇合，向上16B对齐后确定为16，分配两次。
        self.assertEqual(self.load(['addi sp,sp,-80', 'addi s0,sp,80',
            'bnez a0,1014 <task+0x14>', 'li s9,1', 'j 1018 <task+0x18>',
            'li s9,2', 'addi a4,s9,15', 'andi a4,a4,-16', 'sub sp,sp,a4',
            'sub sp,sp,a4', 'addi sp,s0,-80', 'addi sp,sp,80', 'ret']), 112)

    def test_unknown_branch_and_frame_pointer_clobber(self):
        for code in (['bnez a0,1008 <task+0x8>', 'li a5,4096', 'sub sp,sp,a5'],
                     ['addi sp,sp,-80', 'addi s0,sp,80', 'lw s0,0(a0)', 'addi sp,s0,-80']):
            with self.subTest(code=code):
                self.assertEqual(self.load(code), -1)

    def test_loop_widens_to_unknown(self):
        code = ['li a5,1', 'addi a5,a5,1', 'bnez a0,1004 <task+0x4>', 'sub sp,sp,a5']
        self.assertEqual(self.load(code), -1)

    def test_unknown_or_clobbered_register_fails(self):
        for code in (['sub sp,sp,a5'], ['mv sp,a0'], ['andi sp,sp,-16'], ['li a5,4096', 'lw a5,0(a0)', 'sub sp,sp,a5'],
                     ['li a5,4096', 'call 2000 <other>', 'sub sp,sp,a5']):
            with self.subTest(code=code):
                frame = self.load(code)
                self.assertEqual(frame, -1)
                with self.assertRaises(ValueError):
                    stack_check.worst({'task': frame}, {}, 'task')

if __name__ == '__main__':
    unittest.main()
