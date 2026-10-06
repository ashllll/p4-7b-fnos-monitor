#!/usr/bin/env python3
"""向导表单校验：按官方 schema 检查 wizard/{install,config,uninstall,upgrade}。

为什么值得单独一个脚本：**向导是用户看到的第一屏**。JSON 语法错误 fnpack 会拦，
但"语义错了"它不管——`type` 写成了文档里没有的名字、选项字段忘了给 `options`、
`initValue` 通不过自己的 `rules`、字段名忘了 `wizard_` 前缀、两个步骤用了同一个
字段名……这些都会让安装对话框表现异常，而本地那 88 项断言一个都碰不到它们
（它们跑的是生命周期脚本，向导表单在那之前就已经被系统读完了）。

规则来自官方文档（developer.fnnas.com/docs/core-concepts/wizard）：
  - 文件是「步骤数组」；每步 stepTitle + items
  - type ∈ {text, password, radio, checkbox, select, switch, tips}
  - 选项类（radio/checkbox/select）必须有 options[{label,value}]
  - 字段名建议 wizard_ 前缀；**禁止 TRIM_ 前缀**（系统保留）
  - rules 支持 required / min / max / len / pattern，每条都要 message

额外做两条跨文件的一致性检查（官方文档没写、但错了必然出问题）：
  1. 同一个向导文件里字段名不能重复（重复会让后一个覆盖前一个的环境变量）；
  2. 生命周期脚本读的每个 `$wizard_*` 变量，必须至少在某份向导文件里定义过——
     否则用户永远没机会设置它，脚本只能一直取默认值。

用法：python3 nas/fpk/wizard_check.py [包目录]
退出码非 0 表示有问题。
"""

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PKG = os.path.join(HERE, "nasscreencompanion")

TYPES = {"text", "password", "radio", "checkbox", "select", "switch", "tips"}
OPTION_TYPES = {"radio", "checkbox", "select"}
RULE_KEYS = {"required", "min", "max", "len", "pattern"}
FILES = ["install", "config", "uninstall", "upgrade"]


def check_file(path, problems):
    name = os.path.basename(path)
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        problems.append(f"{name}: 读不了或 JSON 不合法：{e}")
        return set()

    if not isinstance(data, list):
        problems.append(f"{name}: 顶层必须是「步骤数组」，实际是 {type(data).__name__}")
        return set()

    fields, seen = set(), {}
    for si, step in enumerate(data):
        where = f"{name}[{si}]"
        if not isinstance(step, dict):
            problems.append(f"{where}: 步骤必须是对象")
            continue
        if not step.get("stepTitle"):
            problems.append(f"{where}: 缺 stepTitle")
        items = step.get("items")
        if not isinstance(items, list) or not items:
            problems.append(f"{where}: 缺 items 或为空")
            continue
        for ii, item in enumerate(items):
            w = f"{where}.items[{ii}]"
            if not isinstance(item, dict):
                problems.append(f"{w}: 表单项必须是对象")
                continue
            t = item.get("type")
            if t not in TYPES:
                problems.append(f"{w}: type={t!r} 不在 {sorted(TYPES)} 里")
                continue

            field = item.get("field")
            if t == "tips":
                if field:
                    problems.append(f"{w}: tips 是只读提示，不该带 field（{field}）")
                if not item.get("helpText"):
                    problems.append(f"{w}: tips 必须给 helpText，否则是一块空白")
            else:
                if not field:
                    problems.append(f"{w}: type={t} 缺 field")
                else:
                    if not str(field).startswith("wizard_"):
                        problems.append(f"{w}: 字段名 {field!r} 没有 wizard_ 前缀（官方建议）")
                    if str(field).startswith("TRIM_"):
                        problems.append(f"{w}: 字段名 {field!r} 用了系统保留的 TRIM_ 前缀")
                    if field in seen:
                        problems.append(f"{w}: 字段 {field!r} 重复（首次出现在 "
                                        f"{seen[field]}）—— 会互相覆盖环境变量")
                    else:
                        seen[field] = w
                    fields.add(field)
                if not item.get("label"):
                    problems.append(f"{w}: 缺 label（用户界面上是空的）")

            if t in OPTION_TYPES:
                opts = item.get("options")
                if not isinstance(opts, list) or not opts:
                    problems.append(f"{w}: type={t} 必须给 options")
                else:
                    values = []
                    for oi, o in enumerate(opts):
                        if not isinstance(o, dict) or "label" not in o or "value" not in o:
                            problems.append(f"{w}.options[{oi}]: 每项都要 label 和 value")
                        else:
                            values.append(str(o["value"]))
                    iv = item.get("initValue")
                    if iv is not None and values and str(iv) not in values:
                        problems.append(f"{w}: initValue={iv!r} 不在 options 里 {values}")

            if t == "switch":
                iv = item.get("initValue")
                if iv is not None and str(iv).lower() not in ("true", "false", "1", "0"):
                    problems.append(f"{w}: switch 的 initValue={iv!r} 不是布尔风格")

            rules = item.get("rules")
            if rules is not None:
                if not isinstance(rules, list):
                    problems.append(f"{w}: rules 必须是数组")
                    continue
                for ri, r in enumerate(rules):
                    wr = f"{w}.rules[{ri}]"
                    if not isinstance(r, dict):
                        problems.append(f"{wr}: 规则必须是对象")
                        continue
                    unknown = set(r) - RULE_KEYS - {"message"}
                    if unknown:
                        problems.append(f"{wr}: 不认识的规则键 {sorted(unknown)}（官方只列了 "
                                        f"{sorted(RULE_KEYS)}）")
                    if not r.get("message"):
                        problems.append(f"{wr}: 缺 message（校验失败时用户看不到原因）")
                    if "pattern" in r:
                        try:
                            re.compile(r["pattern"])
                        except re.error as e:
                            problems.append(f"{wr}: pattern 不是合法正则：{e}")
                    # 默认值必须通过自己的校验 —— 否则用户"什么都不改直接下一步"就被拦
                    iv = item.get("initValue")
                    if iv is not None and not _rule_ok(r, str(iv)):
                        problems.append(f"{wr}: initValue={iv!r} 通不过这条规则 {r} "
                                        f"—— 用户不改任何东西就会被拦下")
    return fields


def _rule_ok(rule, value):
    try:
        if rule.get("required") and not value:
            return False
        if "min" in rule and len(value) < int(rule["min"]):
            return False
        if "max" in rule and len(value) > int(rule["max"]):
            return False
        if "len" in rule and len(value) != int(rule["len"]):
            return False
        if "pattern" in rule and not re.search(rule["pattern"], value):
            return False
    except (TypeError, ValueError):
        return True
    return True


def script_vars(pkg):
    """生命周期脚本里读到的所有 $wizard_* 变量。"""
    found = set()
    cmd = os.path.join(pkg, "cmd")
    for fn in sorted(os.listdir(cmd)):
        p = os.path.join(cmd, fn)
        if not os.path.isfile(p):
            continue
        try:
            txt = open(p, encoding="utf-8").read()
        except OSError:
            continue
        found |= set(re.findall(r"\$\{?(wizard_[A-Za-z0-9_]+)", txt))
    return found


def main():
    pkg = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PKG
    if not os.path.isdir(pkg):
        print(f"找不到包目录 {pkg}")
        return 2

    problems = []
    defined = set()
    for name in FILES:
        path = os.path.join(pkg, "wizard", name)
        if not os.path.exists(path):
            # upgrade/uninstall 是可选的：没有就跳过，但要说一声
            print(f"  （没有 wizard/{name}，跳过）")
            continue
        got = check_file(path, problems)
        print(f"  wizard/{name}: {len(got)} 个字段 {sorted(got)}")
        defined |= got

    # 缺字段的向导文件也要能被脚本读到默认值：这里只检查"脚本读了但没人定义"
    read = script_vars(pkg)
    print(f"\n脚本读到的向导变量：{sorted(read)}")
    print(f"向导文件定义的字段：{sorted(defined)}")
    for v in sorted(read - defined):
        problems.append(f"脚本读了 ${v}，但没有任何向导文件定义这个字段 —— "
                        f"用户永远没机会设置它")
    for v in sorted(defined - read):
        problems.append(f"向导里问了 {v}，但没有脚本读它 —— 用户填了没用")

    if problems:
        print("\n发现问题：")
        for p in problems:
            print("  ✗", p)
        return 1
    print("\n向导表单通过校验：类型、选项、字段名、校验规则与脚本取用都对得上 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
