#!/usr/bin/env python3
"""
Safe PD / PPS Charger Unlock & Thermal Stepping (15°C - 40°C = 12.4A / 67W)
Device: POCO F4 (munch)
"""

import os
import re

def patch_pd_policy_manager():
    paths = [
        "drivers/power/supply/ti/pd_policy_manager_munch.c",
        "drivers/power/supply/ti/pd_policy_manager.c"
    ]
    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, "r") as f:
            content = f.read()

        # Hạ ngưỡng áp/dòng tối thiểu xuống 8.5V / 1.5A, giữ nguyên xác thực sạc zin 67W
        content, c1 = re.subn(
            r"(\.min_adapter_volt_required\s*=\s*)\d+,",
            r"\g<1>8500,",
            content
        )
        content, c2 = re.subn(
            r"(\.min_adapter_curr_required\s*=\s*)\d+,",
            r"\g<1>1500,",
            content
        )
        if c1 > 0 or c2 > 0:
            print(f"✅ [PPS Fast Charge] Patched min adapter requirements (8.5V, 1.5A) in {path}")

        with open(path, "w") as f:
            f.write(content)

def patch_smb5_lib():
    print("ℹ️ [Charging Boot Guard] smb5-lib USBIN_100MA preserved 100% stock to ensure stable boot while charging!")

def patch_jeita_dtsi():
    paths = [
        "arch/arm64/boot/dts/vendor/qcom/jeita-step-cfg-munch.dtsi",
        "arch/arm64/boot/dts/vendor/qcom/jeita-step-cfg.dtsi"
    ]
    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, "r") as f:
            content = f.read()

        # 6-tuple JEITA: 15.1°C - 40.0°C (151 - 400) = 12.4A (12400000 uA)
        new_ranges = """\tqcom,jeita-fcc-ranges = <(-100)  0   1000000
\t\t\t\t1    150 4000000
\t\t\t\t151  400 12400000
\t\t\t\t401  420 6600000
\t\t\t\t421  450 4500000
\t\t\t\t451  580 2200000>;"""

        pattern = r"qcom,jeita-fcc-ranges\s*=\s*<[^>]+>;"
        if re.search(pattern, content):
            content = re.sub(pattern, lambda m: new_ranges.strip(), content)
            with open(path, "w") as f:
                f.write(content)
            print(f"✅ [Thermal Guard DTSI] Updated jeita-fcc-ranges (15-40°C: 12.4A) in {path}")

def patch_step_chg_jeita_c():
    """
    Ép cứng bảng nhiệt 15°C - 40°C (12.4A) trực tiếp vào C-code (Image)
    để có hiệu lực ngay cả khi build.sh không đóng gói dtbo.img.
    """
    paths = [
        "drivers/power/supply/qcom/step-chg-jeita-munch.c",
        "drivers/power/supply/qcom/step-chg-jeita.c"
    ]
    override_code = """
\tif (!strcmp(prop_name, "qcom,jeita-fcc-ranges")) {
\t\tranges[0].low_threshold = -100; ranges[0].high_threshold = 0;   ranges[0].value = 1000000;
\t\tranges[1].low_threshold = 1;    ranges[1].high_threshold = 150; ranges[1].value = 4000000;
\t\tranges[2].low_threshold = 151;  ranges[2].high_threshold = 400; ranges[2].value = 12400000;
\t\tranges[3].low_threshold = 401;  ranges[3].high_threshold = 420; ranges[3].value = 6600000;
\t\tranges[4].low_threshold = 421;  ranges[4].high_threshold = 450; ranges[4].value = 4500000;
\t\tranges[5].low_threshold = 451;  ranges[5].high_threshold = 580; ranges[5].value = 2200000;
\t}"""

    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, "r") as f:
            content = f.read()

        if "ranges[2].high_threshold = 400; ranges[2].value = 12400000;" in content:
            continue

        pattern = r"(static int read_range_data_from_node\b[\s\S]*?)(\n\treturn (?:rc|0);\n\})"
        match = re.search(pattern, content)
        if match:
            content = content[:match.start(2)] + "\n" + override_code + match.group(2) + content[match.end(2):]
            with open(path, "w") as f:
                f.write(content)
            print(f"✅ [Thermal Guard C-Code] Injected 15-40°C (12.4A) JEITA curve into {path}")

if __name__ == "__main__":
    print("🚀 Running 67W Fast Charge Patcher (15°C - 40°C = 12.4A)...")
    patch_pd_policy_manager()
    patch_smb5_lib()
    patch_jeita_dtsi()
    patch_step_chg_jeita_c()
    print("✨ Safe charging patches complete.")
