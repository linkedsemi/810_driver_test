#!/usr/bin/env python3
"""
批量为810_driver_test下所有测试工程添加GPIO toggle支持
"""
import os
import re
from pathlib import Path

BASE_DIR = Path("d:/zephyr4_0/810_driver_test/projects")

def update_cmake(cmake_file):
    """更新CMakeLists.txt，添加gpio_toggle.c"""
    with open(cmake_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 检查是否已经包含gpio_toggle.c
    if 'gpio_toggle.c' in content:
        print(f"  [SKIP] {cmake_file.parent.name} already has gpio_toggle.c")
        return False

    # 查找target_sources中的common文件引用模式
    pattern = r'(target_sources\(app PRIVATE[^)]+test_results\.c[^)]+ztest_glue\.c)'

    if re.search(pattern, content):
        # 在ztest_glue.c后面添加gpio_toggle.c
        new_content = re.sub(
            pattern,
            r'\1\n    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/gpio_toggle.c',
            content
        )

        with open(cmake_file, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"  [OK] Updated CMakeLists.txt: {cmake_file.parent.name}")
        return True
    else:
        print(f"  [WARN] Pattern not found in {cmake_file.parent.name}")
        return False

def update_prj_conf(conf_file):
    """更新prj.conf，添加CONFIG_GPIO=y"""
    if not conf_file.exists():
        print(f"  [SKIP] prj.conf not found: {conf_file.parent.name}")
        return False

    with open(conf_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 检查是否已经包含CONFIG_GPIO
    if re.search(r'^CONFIG_GPIO=y', content, re.MULTILINE):
        print(f"  [SKIP] {conf_file.parent.name} already has CONFIG_GPIO=y")
        return False

    # 在文件末尾添加
    new_content = content.rstrip() + "\n\n# GPIO support for PA05/PA07 toggle threads\nCONFIG_GPIO=y\n"

    with open(conf_file, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print(f"  [OK] Updated prj.conf: {conf_file.parent.name}")
    return True

def update_overlay(overlay_file):
    """更新app.overlay，添加gpio0配置"""
    if not overlay_file.exists():
        print(f"  [SKIP] app.overlay not found: {overlay_file.parent.name}")
        return False

    with open(overlay_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # 检查是否已经包含gpio0配置
    if '&gpio0' in content:
        print(f"  [SKIP] {overlay_file.parent.name} already has gpio0 config")
        return False

    # 在文件末尾添加
    new_content = content.rstrip() + """

/* 启用GPIO控制器，支持PA05/PA07翻转线程 */
&gpio0 {
\tstatus = "okay";
};
"""

    with open(overlay_file, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print(f"  [OK] Updated app.overlay: {overlay_file.parent.name}")
    return True

def main():
    print("=" * 60)
    print("批量更新测试工程，添加GPIO toggle支持")
    print("=" * 60)

    # 查找所有cpu*目录
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
        print(f"\n处理工程: {cpu_dir.parent.name}/{cpu_dir.name}")

        cmake_file = cpu_dir / "CMakeLists.txt"
        conf_file = cpu_dir / "prj.conf"
        overlay_file = cpu_dir / "app.overlay"

        updated = False

        # 更新CMakeLists.txt
        if cmake_file.exists():
            if update_cmake(cmake_file):
                updated = True

        # 更新prj.conf
        if update_prj_conf(conf_file):
            updated = True

        # 更新app.overlay
        if update_overlay(overlay_file):
            updated = True

        if updated:
            total_updated += 1
        else:
            total_skipped += 1

    print("\n" + "=" * 60)
    print(f"完成！更新了 {total_updated} 个工程，跳过 {total_skipped} 个工程")
    print("=" * 60)

if __name__ == "__main__":
    main()
