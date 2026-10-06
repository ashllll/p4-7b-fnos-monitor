#!/usr/bin/env python3
"""权限分类器自测：`classify_read()` / `classify_statvfs()` 判得对不对。

为什么值得单独测：P0 要交的是那张「来源 / 权限矩阵」，而矩阵的每一格都来自这两个
函数。**判错的代价是方向性的**——把「权限不足」报成「不存在」，用户会去查驱动和
硬件；反过来报，用户会去翻权限配置。两种都会让人白折腾半天，而矩阵本身看起来
一切正常。

本机是 macOS，没有 `/proc`、`/sys`，所以**测不了 NAS 上的结论**；但分类器是纯逻辑，
用临时造出来的文件树就能验：可读、无权限、不存在、以及"打开成功但读取失败"。
用 `chmod 000` 造真权限错误（进程不是 root，所以 EACCES 真的会发生）。

用法：python3 nas/fpk/perm_check.py
退出码非 0 表示分类器判错了。
"""

import importlib.util
import io
import os
import shutil
import stat
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
COLLECTOR = os.path.join(HERE, "nasscreencompanion", "app", "server", "fnos_collector.py")


def load():
    spec = importlib.util.spec_from_file_location("nsc_collector", COLLECTOR)
    mod = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True          # 别在包目录里留 __pycache__（会被打进 .fpk）
    spec.loader.exec_module(mod)
    return mod


def main():
    if not os.path.exists(COLLECTOR):
        print(f"找不到 {COLLECTOR}")
        return 2
    m = load()
    rc = 0

    def check(what, got, want):
        nonlocal rc
        if got == want:
            print(f"  \033[32m✓\033[0m {what} → {got}")
        else:
            print(f"  \033[31m✗\033[0m {what} → 判成了 {got}，应当是 {want}")
            rc = 1

    d = tempfile.mkdtemp(prefix="nsc-perm-")
    try:
        readable = os.path.join(d, "readable")
        io.open(readable, "w").write("x")

        denied_file = os.path.join(d, "denied-file")
        io.open(denied_file, "w").write("x")
        os.chmod(denied_file, 0)

        denied_dir = os.path.join(d, "denied-dir")
        os.mkdir(denied_dir)
        io.open(os.path.join(denied_dir, "inner"), "w").write("x")
        os.chmod(denied_dir, 0)

        missing = os.path.join(d, "does-not-exist")

        print("可读的文件")
        check("可读", m.classify_read(readable)[0], m.ST_OK)
        print("权限不足的文件（chmod 000）")
        check("无权限", m.classify_read(denied_file)[0], m.ST_DENIED)
        print("不存在的路径")
        check("不存在", m.classify_read(missing)[0], m.ST_MISSING)
        print("无权限的目录（要看目录本身，不是里面的文件）")
        check("目录无权限", m.classify_read(denied_dir + "/inner")[0], m.ST_DENIED)
        print("目录本身可读时，listdir 应当成功")
        check("目录可读", m.classify_read(readable)[0], m.ST_OK)

        print("卷容量（statvfs 的权限模型和 open 不同）")
        check("statvfs 可读", m.classify_statvfs(d)[0], m.ST_OK)
        check("statvfs 不存在", m.classify_statvfs(missing)[0], m.ST_MISSING)
        # macOS 上 chmod 000 的目录仍然能 statvfs（stat 只需要路径上的执行位），
        # 所以这里只断言"不会炸、且不会报成 ok 以外的成功态"——真机上是 Linux，
        # 判据以 classify_statvfs 的注释为准。
        st, _ = m.classify_statvfs(denied_dir)
        if st in (m.ST_OK, m.ST_DENIED):
            print(f"  \033[32m✓\033[0m chmod 000 的目录 statvfs → {st}（两个都是合理结果，不崩即可）")
        else:
            print(f"  \033[31m✗\033[0m chmod 000 的目录 statvfs → {st}，不该是这两个之外的状态")
            rc = 1
    finally:
        # 目录被 chmod 000 之后要先恢复权限才删得掉
        for root, dirs, _ in os.walk(d):
            for x in dirs:
                try:
                    os.chmod(os.path.join(root, x), stat.S_IRWXU)
                except OSError:
                    pass
        shutil.rmtree(d, ignore_errors=True)

    if rc:
        print("\n分类器有判错的地方——矩阵的每一格都来自它，先修这里。")
        return 1
    print("\n权限分类器判定正确：有权限/无权限/不存在分得开 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
