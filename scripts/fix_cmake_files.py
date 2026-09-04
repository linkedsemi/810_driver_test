#!/usr/bin/env python3
"""
批量修复CMakeLists.txt：正确使用common_sources.cmake
"""
import os
import re
from pathlib import Path

BASE_DIR = Path("d:/zephyr4_0/810_driver_test/projects")

def fix_cmake(cmake_file, project_sources):
    """修复CMakeLists.txt为标准格式"""
    with open(cmake_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 提取project名称
    project_match = re.search(r'project\(([^)]+)\)', content)
    project_name = project_match.group(1) if project_match else cmake_file.parent.name

    # 检查是否有target_compile_definitions（如rtc_test）
    compile_defs_match = re.search(
        r'target_compile_definitions\(app PRIVATE.*?\)',
        content,
        re.DOTALL
    )
    compile_defs = compile_defs_match.group(0) if compile_defs_match else None

    # 构建新的CMakeLists.txt
    new_content = f"""# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{{ZEPHYR_BASE}})
project({project_name})

# 项目特定源文件
target_sources(app PRIVATE
{project_sources})

# 项目特定include目录
target_include_directories(app PRIVATE
    ${{CMAKE_CURRENT_SOURCE_DIR}}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${{CMAKE_CURRENT_SOURCE_DIR}}/../../../common/common_sources.cmake)
"""

    # 如果有compile_definitions，添加它
    if compile_defs:
        new_content += f"\n{compile_defs}\n"

    with open(cmake_file, 'w', encoding='utf-8') as f:
        f.write(new_content)

    print(f"  [OK] Fixed: {cmake_file.parent.name}")
    return True

def extract_project_sources(content):
    """从原CMakeLists.txt提取项目特定的源文件"""
    sources = []

    # 查找target_sources块
    match = re.search(r'target_sources\(app PRIVATE\s+(.*?)\)', content, re.DOTALL)
    if match:
        block = match.group(1)
        # 提取src/开头的文件
        for line in block.split('\n'):
            line = line.strip()
            if line and line.startswith('src/'):
                sources.append(f"    {line}")

    return '\n'.join(sources) if sources else "    src/main.c"

def main():
    print("=" * 70)
    print("批量修复CMakeLists.txt为标准格式")
    print("=" * 70)

    cpu_dirs = []
    for project_dir in BASE_DIR.iterdir():
        if project_dir.is_dir() and project_dir.name.endswith('_test'):
            for subdir in project_dir.iterdir():
                if subdir.is_dir() and subdir.name.startswith('cpu'):
                    cpu_dirs.append(subdir)

    cpu_dirs.sort()

    total_fixed = 0

    for cpu_dir in cpu_dirs:
        cmake_file = cpu_dir / "CMakeLists.txt"
        if not cmake_file.exists():
            continue

        print(f"\n处理: {cpu_dir.parent.name}/{cpu_dir.name}")

        # 读取原文件，提取项目特定源文件
        with open(cmake_file, 'r', encoding='utf-8') as f:
            content = f.read()

        project_sources = extract_project_sources(content)
        fix_cmake(cmake_file, project_sources)
        total_fixed += 1

    print("\n" + "=" * 70)
    print(f"完成！修复了 {total_fixed} 个工程")
    print("=" * 70)

if __name__ == "__main__":
    main()
