#!/usr/bin/env python3
"""向量生成脚本（§13.2：预期值要随工程可复现，生成脚本随工程保留）。

重新生成 tests/expected/hash_vectors.h 与 cipher_vectors.h 的全部向量值：
  - hash：Python hashlib（OpenSSL 后端；SM3 经 hashlib.new("sm3")，需
    Python 3.11+ 或带 SM3 的 OpenSSL）——短报文官方向量同步复核。
  - cipher：调 openssl enc（AES-128-CBC / SM4-ECB / SM4-CBC，-nopad）。
    SM4-CBC 参考实现正确性由"其 SM4-ECB 输出 == draft-ribose-cfrg-sm4-10
    A.2.1.1 官方向量"交叉证明。
  - ecdsa/rsa 向量为固定黄金值（来源注释见对应 .h 文件），本脚本不生成。

用法（在 tests/scripts/ 下）：
    python gen_vectors.py            # 默认就地覆盖 ../expected/{hash,cipher}_vectors.h
    python gen_vectors.py --check    # 只校验现有文件与计算值一致（diff 语义）
"""
import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = Path(__file__).resolve().parents[1] / "expected"

M56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
M64 = "a" * 64
MILLION_A = "a" * 1000000

HASH_MSGS = [("", 0), ("abc", 3), (M56, 56), (M64, 64), (None, 1000000)]


def h(name, msg):
    return hashlib.new(name, msg).hexdigest().upper()


def wrap(hexstr, width=64, indent="\t  ", cont=False):
    out = []
    for i in range(0, len(hexstr), width):
        out.append(f'"{hexstr[i:i + width]}"')
    # chunk 自带引号，joiner 只补换行缩进；#define 场景每行须带续行 \
    joiner = f' \\\n{indent}' if cont else f'\n{indent}'
    return joiner.join(out)


def gen_hash_header():
    lines = [
        "/*",
        " * SPDX-License-Identifier: Apache-2.0",
        " *",
        " * hash KAT 预期向量（§13.2 确定性测试预期数据）——本文件由",
        " * tests/scripts/gen_vectors.py 生成，勿手改（复现：python gen_vectors.py）。",
        " *",
        " * 来源：短报文 = FIPS 180-4 / GM/T 0004-2012 附录 A 官方向量；",
        " * 64B 与 1000000B 'a'（跨块长报文）= Python hashlib（OpenSSL 后端）计算，",
        " * 其中 SHA-256 百万 'a' = FIPS 180-4 已收录值 cdc76e5c...。",
        " */",
        "#ifndef HASH_VECTORS_H",
        "#define HASH_VECTORS_H",
        "",
        "#define MT_HASH_MILLION_A      ((const char *)0)",
        "#define MT_HASH_MILLION_A_LEN  1000000U",
        "",
        "struct mt_hash_vector {",
        "\tconst char *input;   /* NULL = 百万 'a' 流式；否则 strlen 为报文长度 */",
        "\tconst char *md_hex;  /* 期望摘要（大写 hex，固件 unhexify 后逐字节比对） */",
        "};",
        "",
    ]
    entries = [
        ("SHA-224（FIPS 180-4 + 跨块长报文）", "sha224", "mt_sha224_vecs", 28),
        ("SHA-256（FIPS 180-4 + 跨块长报文）", "sha256", "mt_sha256_vecs", 32),
        ("SHA-384（FIPS 180-4 + 跨块长报文）", "sha384", "mt_sha384_vecs", 48),
        ("SHA-512（FIPS 180-4 + 跨块长报文）", "sha512", "mt_sha512_vecs", 64),
        ("SM3（GM/T 0004-2012 附录 A + 跨块长报文）", "sm3", "mt_sm3_vecs", 32),
    ]
    for comment, algo, arr, mdlen in entries:
        lines.append(f"/* ---- {comment} ---- */")
        lines.append(f"static const struct mt_hash_vector {arr}[] = {{")
        for msg, mlen in HASH_MSGS:
            digest = h(algo, b"a" * 1000000 if msg is None else msg.encode())
            assert len(digest) == mdlen * 2, (algo, mlen)
            if msg is None:
                lines.append(f"\t{{ MT_HASH_MILLION_A,")
                lines.append(f"\t  {wrap(digest)} }},")
            else:
                esc = msg.replace("\\", "\\\\").replace('"', '\\"')
                if len(msg) <= 64:
                    lines.append(f'\t{{ "{esc}", {wrap(digest)} }},')
                else:
                    lines.append(f'\t{{ "{esc}",')
                    lines.append(f"\t  {wrap(digest)} }},")
        lines.append("};")
        lines.append("")
    lines.append("#endif /* HASH_VECTORS_H */")
    return "\n".join(lines) + "\n"


def openssl_cipher(args, stdin=None):
    r = subprocess.run(["openssl", "enc"] + args + ["-nopad"],
                       input=stdin, capture_output=True)
    if r.returncode != 0:
        sys.exit(f"openssl {' '.join(args)} failed: {r.stderr.decode()}")
    return r.stdout.hex().upper()


def gen_cipher_header():
    aes_k = "2B7E151628AED2A6ABF7158809CF4F3C"
    aes_iv = "000102030405060708090A0B0C0D0E0F"
    aes_pt = ("6BC1BEE22E409F96E93D7E117393172A"
              "AE2D8A571E03AC9C9EB76FAC45AF8E51"
              "30C81C46A35CE411E5FBC1191A0A52EF"
              "F69F2445DF4F9B17AD2B417BE66C3710")
    aes_ct = openssl_cipher(["-aes-128-cbc", "-K", aes_k, "-iv", aes_iv],
                            bytes.fromhex(aes_pt))

    sm4_k = "0123456789ABCDEFFEDCBA9876543210"
    sm4_pt = ("AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDD"
              "EEEEEEEEFFFFFFFFAAAAAAAABBBBBBBB")
    sm4_ecb = openssl_cipher(["-sm4-ecb", "-K", sm4_k], bytes.fromhex(sm4_pt))
    # A.2.1.1 交叉验证：OpenSSL SM4-ECB 必须等于官方向量
    expect_ecb = ("5EC8143DE509CFF7B5179F8F474B8619"
                  "2F1D305A7FB17DF985F81C8482192304")
    if sm4_ecb != expect_ecb:
        sys.exit(f"OpenSSL SM4-ECB mismatch reference impl: {sm4_ecb}")
    sm4_cbc = openssl_cipher(["-sm4-cbc", "-K", sm4_k, "-iv", sm4_k],
                             bytes.fromhex(sm4_pt))

    lines = [
        "/*",
        " * SPDX-License-Identifier: Apache-2.0",
        " *",
        " * cipher KAT 预期向量（§13.2 确定性测试预期数据）——本文件由",
        " * tests/scripts/gen_vectors.py 生成，勿手改（复现：python gen_vectors.py）。",
        " *",
        " * 来源：AES-128-CBC = NIST SP800-38A F.2.1 官方向量（OpenSSL 复核一致）；",
        " * SM4-ECB = draft-ribose-cfrg-sm4-10 A.2.1.1（OpenSSL 输出与之一致，",
        " * 交叉证明参考实现正确）；SM4-CBC = GB/T 32907 组合（key=IV=A.2.1.1 key，",
        " * 同 32 字节报文，OpenSSL enc -sm4-cbc 生成）。",
        " */",
        "#ifndef CIPHER_VECTORS_H",
        "#define CIPHER_VECTORS_H",
        "",
        "/* ---- AES-128-CBC（NIST SP800-38A F.2.1，64 字节 = 4 块） ---- */",
        f'#define MT_AES128CBC_KEY  "{aes_k}"',
        f'#define MT_AES128CBC_IV   "{aes_iv}"',
        f'#define MT_AES128CBC_PT \\',
        f'\t{wrap(aes_pt, cont=True)}',
        f'#define MT_AES128CBC_CT \\',
        f'\t{wrap(aes_ct, cont=True)}',
        "",
        "/* ---- SM4-ECB（draft-ribose-cfrg-sm4-10 A.2.1.1，32 字节） ---- */",
        f'#define MT_SM4_KEY "{sm4_k}"',
        f'#define MT_SM4_PT \\',
        f'\t{wrap(sm4_pt, cont=True)}',
        f'#define MT_SM4_ECB_CT \\',
        f'\t{wrap(sm4_ecb, cont=True)}',
        "",
        "/* ---- SM4-CBC（key=IV=MT_SM4_KEY，GB/T 32907 组合，OpenSSL 复核） ---- */",
        f'#define MT_SM4_CBC_CT \\',
        f'\t{wrap(sm4_cbc, cont=True)}',
        "",
        "#endif /* CIPHER_VECTORS_H */",
    ]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="只校验现有文件与重新计算的向量一致")
    args = ap.parse_args()

    hash_h = gen_hash_header()
    cipher_h = gen_cipher_header()

    for name, content in (("hash_vectors.h", hash_h),
                          ("cipher_vectors.h", cipher_h)):
        path = EXPECTED / name
        if args.check:
            if path.read_text(encoding="utf-8") != content:
                print(f"[check] {name}: MISMATCH（文件与重新计算值不一致）")
                sys.exit(1)
            print(f"[check] {name}: OK")
        else:
            path.write_text(content, encoding="utf-8")
            print(f"[gen] wrote {path}")
    print("ecdsa/rsa 向量为固定黄金值（来源见对应 .h 头注释），不由本脚本生成。")


if __name__ == "__main__":
    main()
