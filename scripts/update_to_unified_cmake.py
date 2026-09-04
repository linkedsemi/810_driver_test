#!/usr/bin/env python3
"""
批量更新测试工程：改用统一的common_sources.cmake包含方式
"""
import os
import re
from pathlib import Path

BASE_DIR = Path("d:/zephyr4_0/810_driver_test/projects")

def update_cmake_to_unified(cmake_file):
    """将CMakeLists.txt改为使用统一的common_sources.cmake"""
    with open(cmake_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    new_lines = []
    in_target_sources = False
    has_common_include = False
    modified = False

    i = 0
    while i < len(lines):
        line = lines[i]

        # 检查是否已经有include common_sources.cmake
        if 'common_sources.cmake' in line:
            has_common_include = True
            new_lines.append(line)
            i += 1
            continue

        # 检测target_sources块
        if 'target_sources(app PRIVATE' in line:
            in_target_sources = True
            new_lines.append(line)
            i += 1
            continue

        # 在target_sources块内
        if in_target_sources:
            # 检查是否是common的源文件行
            if any(x in line for x in ['test_results.c', 'ztest_glue.c', 'gpio_toggle.c']):
                # 跳过这些行，稍后用include替代
                i += 1
                modified = True
                continue
            elif ')' in line:
                # target_sources块结束
                in_target_sources = False
                new_lines.append(line)

                # 在target_sources后添加include common_sources.cmake
                if not has_common_include and modified:
                    new_lines.append('\n# Include common sources (test_results, ztest_glue, gpio_toggle)\n')
                    new_lines.append('include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)\n')
                    has_common_include = True
                i += 1
                continue

        # 检测target_include_directories中的common路径
        if 'target_include_directories' in line and 'common' in line:
            # 跳过，因为common_sources.cmake会处理
            i += 1
            # 继续跳过到块结束
            paren_count = 1
            while i < len(lines) and paren_count > 0:
                if '(' in lines[i]:
                    paren_count += lines[i].count('(')
                if ')' in lines[i]:
                    paren_count -= lines[i].count(')')
                i += 1
            modified = True
            continue

        new_lines.append(line)
        i += 1

    if modified:
        with open(cmake_file, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
        print(f"  [OK] Updated to unified: {cmake_file.parent.name}")
        return True
    else:
        print(f"  [SKIP] No changes needed: {cmake_file.parent.name}")
        return False

def main():
    print("=" * 70)
    print("批量更新：改用统一的 common_sources.cmake 包含方式")
    print("=" * 70)

    cpu_dirs = []
    for project_dir in BASE_DIR.iterdir():
        if project_dir.is_dir() and project_dir.name.endswith('_test'):
            for subdir in project_dir.iterdir():
                if subdir.is_dir() and subdir.name.startswith('cpu'):
                    cpu_dirs.append(subdir)

    cpu_dirs.sort()

    total_updated = 0
    total_skipped = 0

    for cpu_dir in cpu_dirs:
        print(f"\n处理: {cpu_dir.parent.name}/{cpu_dir.name}")
        cmake_file = cpu_dir / "CMakeLists.txt"

        if cmake_file.exists():
            if update_cmake_to_unified(cmake_file):
                total_updated += 1
            else:
                total_skipped += 1

    print("\n" + "=" * 70)
    print(f"完成！更新 {total_updated} 个，跳过 {total_skipped} 个")
    print("=" * 70)

if __name__ == "__main__":
    main()
