#!/usr/bin/env python3
"""mapper 审核驱动（README 已验证清单之外的 mapper 逐个取证）。

背景：仓库自带的语料 `rom/` 只覆盖了一部分 mapper，而用户收藏
（默认 `E:\\Game\\NES`）里有几千张 ROM。本脚本按 **Mesen 库**
（`E:\\Mesen\\MesenNesDB.txt`，按 PRG+CHR 的 CRC32 索引）把收藏里的 ROM
归到「板子真正是哪个 mapper」，再对每个 mapper 抽样跑 `nes-tests --rom`，
产出 `test/out/mapper_audit.{csv,md}`：

  ① `db_mapper != effective_mapper` —— 头部谎报的候选（romdb 条目来源）；
  ② 有 ROM 却 `blank`/`load_fail`/`suspect` 的 mapper（按收藏 ROM 数降序，
     这就是审核/修复的优先级表）；
  ③ 收藏里没有 ROM 的已注册 mapper（只能静态审核 + 合成单测）。

用法（在仓库根目录）：
    python test/audit_mappers.py                      # 全量抽样（每个 mapper 3 张）
    python test/audit_mappers.py --mappers 176,241 -v # 只审某几个 mapper
    python test/audit_mappers.py --per-mapper 1 --frames 180

注意：
  * `nes-tests --rom` 的结论行是 CSV，按字段解析（不要按关键字 grep）。
  * `ok` 只代表「有非均匀渲染」，不代表画面正确 —— 画面正确性仍要抓图与
    Mesen 比对（见 test/out/mesencap.ps1 / livecap.ps1）。
  * 结果按 (path, frames) 缓存，重跑只补测缺的，迭代很快。
"""

import argparse
import binascii
import collections
import concurrent.futures
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DB = r"E:\Mesen\MesenNesDB.txt"
DEFAULT_ROM_DIRS = [r"E:\Game\NES"]
DEFAULT_EXE = os.path.join(REPO, "test", "out", "bin", "nes-tests.exe")
DEFAULT_OUT = os.path.join(REPO, "test", "out")

VERDICT_BAD = ("blank", "load_fail", "suspect")
STATUSES = ("NEW", "OK", "SAME", "OLD", "REGRESSED", "IMPROVED", "KNOWN_BAD",
            "MISSING", "STALE_FRAMES", "HASH_DIFF", "LOAD_FAIL")
FIELD_NAMES = ("status", "verdict", "sha256", "label", "dir_mapper",
               "header_mapper", "effective_mapper", "frames", "hash_chain",
               "first_render")


# ----------------------------------------------------------------------------
# Mesen 数据库 / ROM 头
# ----------------------------------------------------------------------------

def load_db(path):
    """CRC32(PRG+CHR) -> (system, board, mapper_name, mapper, prg_kb, chr_kb,
    chr_ram_kb, save_ram_kb, work_ram_kb)"""
    db = {}
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split(",")
            if len(p) < 11:
                continue
            def num(i):
                try:
                    return int(p[i])
                except ValueError:
                    return -1
            db[p[0].upper()] = (p[1], p[2], p[4], num(5), num(6), num(7), num(8), num(9), num(10))
    return db


def parse_header(raw):
    """iNES/NES 2.0 头 -> (prg_bytes, chr_bytes, mapper, submapper, nes2) 或 None。

    NES 2.0 的尺寸是「高半字节扩展」形式：低半字节 = 0xF 时按 2^n 字节解释，
    否则 PRG 以 16KB、CHR 以 8KB 为单位，byte9 的高低半字节分别是它们的 MSB。"""
    if len(raw) < 16 or raw[:4] != b"NES\x1a":
        return None
    h = raw[:16]
    nes2 = ((h[7] >> 2) & 0x03) == 0x02
    if nes2 and (h[4] & 0x0F) == 0x0F:
        prg = 1 << (h[4] >> 4)
    elif nes2:
        prg = ((h[4] & 0x0F) | ((h[9] & 0x0F) << 4)) * 16384
    else:
        prg = h[4] * 16384
    if nes2 and (h[5] & 0x0F) == 0x0F:
        chr_ = 1 << (h[5] >> 4)
    elif nes2:
        chr_ = ((h[5] & 0x0F) | (((h[9] >> 4) & 0x0F) << 4)) * 8192
    else:
        chr_ = h[5] * 8192
    mapper = (h[6] >> 4) | (h[7] & 0xF0)
    if nes2:
        mapper |= (h[8] & 0x0F) << 8
    sub = ((h[8] >> 4) & 0x0F) if nes2 else -1
    return prg, chr_, mapper, sub, nes2


def scan_rom(path, db):
    """读一个镜像，返回 (db_mapper, db_system, header_mapper, crc) 或 None。

    CRC 只算 PRG+CHR（Mesen 库就是这么索引的）：trainer 位有效时跳过 512 字节；
    两种算法都试一遍，命中即停（有些 dump 的 CHR 长度字段夸大）。"""
    try:
        raw = open(path, "rb").read()
    except OSError:
        return None
    head = parse_header(raw)
    if head is None:
        return None
    prg, chr_, header_mapper, _sub, _nes2 = head
    for base in (16 + (512 if (raw[6] & 0x04) else 0), 16):
        for chr_len in (chr_, 0):
            if base + prg + chr_len > len(raw):
                continue
            blob = raw[base:base + prg + chr_len]
            crc_full = "%08X" % (binascii.crc32(blob) & 0xFFFFFFFF)
            info = db.get(crc_full)
            if info is not None:
                return info[3], info[0], header_mapper, crc_full
            crc_prg = "%08X" % (binascii.crc32(raw[base:base + prg]) & 0xFFFFFFFF)
            info = db.get(crc_prg)
            if info is not None:
                return info[3], info[0], header_mapper, crc_prg
    return None


# ----------------------------------------------------------------------------
# 已注册 mapper（从 src/nes_mapper.c 的 case 表里读）
# ----------------------------------------------------------------------------

def csv_path_of(path):
    """本脚本自己的 CSV 是逗号分隔 —— Windows 路径里可能有逗号，落盘前替换掉。"""
    return path.replace(",", "_")


def registered_mappers():
    src = open(os.path.join(REPO, "src", "nes_mapper.c"), encoding="utf-8", errors="replace").read()
    always, heavy = set(), set()
    is_heavy = False
    for line in src.splitlines():
        s = line.strip()
        if s.startswith("#if NES_ENABLE_HEAVY_MAPPERS"):
            is_heavy = True
            continue
        if s.startswith("#endif") and is_heavy:
            is_heavy = False
            continue
        m = re.match(r"NES_LOAD_MAPPER_ENTRY\((\d+)\);|NES_CASE_LOAD_MAPPER\((\d+)\);", s)
        if m:
            value = int(m.group(1) or m.group(2))
            (heavy if is_heavy else always).add(value)
    return always, heavy


# ----------------------------------------------------------------------------
# 跑一个镜像
# ----------------------------------------------------------------------------

def run_one(exe, path, frames, timeout):
    cmd = [exe, "--rom", path, "--frames", str(frames)]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              timeout=timeout)
        out = (proc.stdout or b"").decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return {"verdict": "timeout", "first_render": "0", "effective_mapper": "-1", "status": "TIMEOUT"}
    except OSError as exc:
        return {"verdict": "exec_fail:%s" % exc, "first_render": "0",
                "effective_mapper": "-1", "status": "EXEC_FAIL"}
    for line in out.splitlines():
        parts = line.split(",")
        if len(parts) < 10 or parts[0] not in STATUSES:
            continue
        if len(parts[2]) != 64:
            continue
        row = dict(zip(FIELD_NAMES, parts))
        return row
    return {"verdict": "no_output", "first_render": "0", "effective_mapper": "-1", "status": "NO_OUTPUT"}


def main():
    ap = argparse.ArgumentParser(description="mapper 审核驱动（收藏 × Mesen 库 × nes-tests）")
    ap.add_argument("--rom-dir", action="append", default=None, help="可多次；默认 E:\\Game\\NES")
    ap.add_argument("--db", default=DEFAULT_DB, help="MesenNesDB.txt 路径")
    ap.add_argument("--exe", default=DEFAULT_EXE, help="nes-tests 可执行文件")
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--per-mapper", type=int, default=3, help="每个 mapper 抽样多少张 ROM")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=180, help="单个 ROM 的超时秒数")
    ap.add_argument("--out-dir", default=DEFAULT_OUT)
    ap.add_argument("--mappers", default="", help="只审这些 mapper（逗号分隔）")
    ap.add_argument("--no-resume", action="store_true", help="忽略缓存，全部重测")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    rom_dirs = args.rom_dir or DEFAULT_ROM_DIRS
    only = set(int(x) for x in args.mappers.split(",") if x.strip().isdigit())

    if not os.path.exists(args.exe):
        print("找不到 nes-tests：%s\n先执行  cd test; xmake build nes-tests" % args.exe)
        return 2
    db = load_db(args.db)
    print("Mesen 库条目 = %d" % len(db))

    csv_path = os.path.join(args.out_dir, "mapper_audit.csv")
    cached = {}
    if not args.no_resume and os.path.exists(csv_path):
        with open(csv_path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                parts = line.rstrip("\n").split(",")
                if len(parts) < 12 or parts[0] == "path":
                    continue
                cached[(parts[0], parts[1])] = parts

    # 1) 扫描收藏，按 Mesen 库的 mapper 归组
    per_mapper = collections.defaultdict(list)
    unmatched = 0
    broken = 0
    n_files = 0
    for root in rom_dirs:
        if not os.path.isdir(root):
            print("跳过不存在的目录：%s" % root)
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in filenames:
                if not name.lower().endswith(".nes"):
                    continue
                n_files += 1
                full = os.path.join(dirpath, name)
                got = scan_rom(full, db)
                if got is None:
                    unmatched += 1
                    continue
                db_mapper, db_system, header_mapper, crc = got
                if db_mapper < 0:
                    broken += 1
                    continue
                if only and db_mapper not in only:
                    continue
                per_mapper[db_mapper].append((full, db_system, header_mapper, crc))
    print("收藏镜像 = %d，命中 Mesen 库 = %d，未命中 = %d，库中 mapper 无效 = %d"
          % (n_files, sum(len(v) for v in per_mapper.values()), unmatched, broken))

    # 2) 每个 mapper 抽样
    jobs = []
    for mapper in sorted(per_mapper):
        entries = per_mapper[mapper]
        # 同名不同路径的重复 dump 只留一份：按 (大小, CRC) 去重，再按路径长度排序取最短的
        seen = set()
        unique = []
        for item in sorted(entries, key=lambda it: (len(it[0]), it[0])):
            key = (os.path.getsize(item[0]) if os.path.exists(item[0]) else 0, item[3])
            if key in seen:
                continue
            seen.add(key)
            unique.append(item)
        for item in unique[:args.per_mapper]:
            jobs.append((mapper, item))

    todo = []
    for mapper, (path, system, header_mapper, crc) in jobs:
        key = (csv_path_of(path), str(args.frames))
        if key in cached:
            continue
        todo.append((mapper, path, system, header_mapper, crc))
    print("待测 = %d（缓存命中 %d）" % (len(todo), len(jobs) - len(todo)))

    results = {}
    for key, parts in cached.items():
        results[key] = parts

    def work(item):
        mapper, path, system, header_mapper, crc = item
        row = run_one(args.exe, path, args.frames, args.timeout)
        return item, row

    done = 0
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(work, item) for item in todo]
        for fut in concurrent.futures.as_completed(futures):
            item, row = fut.result()
            mapper, path, system, header_mapper, crc = item
            parts = [csv_path_of(path), str(args.frames), str(mapper), system or "", str(header_mapper),
                     row.get("effective_mapper", "-1"), row.get("verdict", "?"),
                     row.get("status", "?"), row.get("first_render", "0"), row.get("sha256", ""),
                     row.get("hash_chain", "0"), row.get("dir_mapper", "-1")]
            results[(csv_path_of(path), str(args.frames))] = parts
            done += 1
            if not args.quiet and (done % 20 == 0 or done == len(todo)):
                rate = done / max(0.001, time.time() - started)
                print("  %d/%d  (%.1f/s)" % (done, len(todo), rate))
                sys.stdout.flush()

    # 3) 落盘 CSV
    os.makedirs(args.out_dir, exist_ok=True)
    header = "path,frames,db_mapper,db_system,header_mapper,effective_mapper,verdict,status,first_render,sha256,hash_chain,dir_mapper"
    with open(csv_path, "w", encoding="utf-8", newline="") as fh:
        fh.write(header + "\n")
        for key in sorted(results):
            fh.write(",".join(results[key]) + "\n")
    print("写出 %s（%d 行）" % (csv_path, len(results)))

    # 4) 生成报告
    always, heavy = registered_mappers()
    reg = always | heavy
    by_mapper = collections.defaultdict(lambda: collections.Counter())
    samples = collections.defaultdict(list)
    mismatch = collections.defaultdict(list)
    for key, parts in results.items():
        (path, frames, db_mapper, system, header_mapper, effective_mapper,
         verdict, status, first_render, sha256, hash_chain, dir_mapper) = parts
        mapper = int(db_mapper)
        by_mapper[mapper][verdict] += 1
        if len(samples[mapper]) < 3:
            samples[mapper].append((os.path.basename(path), verdict, effective_mapper, header_mapper))
        if effective_mapper not in ("-1", "") and int(effective_mapper) != mapper:
            mismatch[mapper].append((os.path.basename(path), header_mapper, effective_mapper))

    md = []
    md.append("## mapper 审核报告（收藏 × Mesen 库 × nes-tests）\n")
    md.append("生成时间：%s；每个 mapper 抽样 %d 张，%d 帧/张。"
              % (time.strftime("%Y-%m-%d %H:%M:%S"), args.per_mapper, args.frames))
    md.append("收藏镜像 %d 张，命中 Mesen 库并进入审核 %d 张。\n"
              % (n_files, len(results)))

    md.append("### ① 头部谎报候选（db_mapper != effective_mapper ⇒ romdb 条目）\n")
    md.append("| mapper | ROM 数 | 例子（文件 / 头部 / 实际）|")
    md.append("|---|---|---|")
    for mapper in sorted(mismatch, key=lambda m: -len(mismatch[m])):
        ex = "; ".join("%s: %s→%s" % (n, h, e) for n, h, e in mismatch[mapper][:3])
        md.append("| %d | %d | %s |" % (mapper, len(mismatch[mapper]), ex))
    if not mismatch:
        md.append("| - | 0 | 无 |")

    md.append("\n### ② 有 ROM 且 verdict 不可信（修复优先级，按收藏 ROM 数降序）\n")
    md.append("| mapper | ROM 数 | ok | suspect | blank | load_fail | 其它 | 样本 |")
    md.append("|---|---|---|---|---|---|---|---|")
    bad = []
    for mapper, counter in by_mapper.items():
        bad_count = sum(counter.get(v, 0) for v in VERDICT_BAD) + \
                    sum(n for v, n in counter.items() if v not in ("ok", "suspect", "blank", "load_fail"))
        if bad_count == 0:
            continue
        bad.append(mapper)
    for mapper in sorted(bad, key=lambda m: -sum(by_mapper[m].values())):
        c = by_mapper[mapper]
        other = sum(n for v, n in c.items() if v not in ("ok", "suspect", "blank", "load_fail"))
        ex = "; ".join("%s=%s" % (n, v) for n, v, _e, _h in samples[mapper][:2])
        md.append("| %d | %d | %d | %d | %d | %d | %d | %s |"
                  % (mapper, sum(c.values()), c.get("ok", 0), c.get("suspect", 0),
                     c.get("blank", 0), c.get("load_fail", 0), other, ex))
    if not bad:
        md.append("| - | - | - | - | - | - | - | 无 |")

    md.append("\n### ③ 收藏里没有 ROM 的已注册 mapper（只能静态审核 + 合成单测）\n")
    no_rom = sorted(m for m in reg if m not in by_mapper)
    md.append(", ".join(str(m) for m in no_rom) if no_rom else "（无）")

    md.append("\n### ④ 只有 ok 的 mapper（仍建议抽查画面）\n")
    clean = [m for m in by_mapper if m not in bad]
    md.append(", ".join(str(m) for m in sorted(clean)) if clean else "（无）")

    md.append("\n### ⑤ 收藏里有 ROM、但根本没实现的 mapper（判据：effective_mapper = -1 / load_fail）\n")
    unimpl = sorted(m for m in by_mapper if m not in reg)
    md.append(", ".join("%d(%d)" % (m, sum(by_mapper[m].values())) for m in unimpl) if unimpl else "（无）")
    md.append("")

    md_path = os.path.join(args.out_dir, "mapper_audit.md")
    with open(md_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(md) + "\n")
    print("写出 %s" % md_path)
    print("坏 verdict 的 mapper = %d 个：%s" % (len(bad), bad[:40]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
