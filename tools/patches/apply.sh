#!/usr/bin/env bash
# 把 tools/patches/*.patch 套到当前工作树（幂等：先判定整棵树的身份，再决定动不动手）。
#
#   bash tools/patches/apply.sh          # 判定 + 套用
#   bash tools/patches/apply.sh --check  # 只判定，不改任何文件
#
# 退出码：0 = 已就位（本次套用成功，或本来就已经是套好的树）；1 = 树与补丁基线不一致。
#
# 判定为什么不能靠 `git apply --check 一份一份试`：补丁之间有先后依赖（后面的补丁改前面的补丁
# 写下的代码），而 --check 不写索引 ⇒ 不跨补丁保持状态。这里改成往一个**临时索引**里真套
# （GIT_INDEX_FILE 指向临时文件，不动工作树、不动真索引），状态才能累积。
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"

patches=("$here"/[0-9][0-9][0-9][0-9]-*.patch)
[ -e "${patches[0]}" ] || { echo "找不到补丁：$here/0001-*.patch" >&2; exit 1; }

mode=apply
case "${1:-}" in
    ""|--apply) mode=apply ;;
    --check|-c) mode=check ;;
    *) echo "用法: bash tools/patches/apply.sh [--check]" >&2; exit 2 ;;
esac

failed_forward=""
failed_reverse=""

# 往临时索引里从头（forward）或从尾（reverse）套一遍；成功 0，失败 1 并记下卡住的那份
try_stack() {
    local dir="$1" tmp i p rc=0
    tmp="$(mktemp -t fnos-patch-idx)"
    rm -f "$tmp"
    if ! GIT_INDEX_FILE="$tmp" git -C "$root" add -A -- . ':(exclude)tools/patches' >/dev/null 2>&1; then
        rm -f "$tmp"
        eval "failed_$dir='(当前工作树写不进临时索引)'"
        return 1
    fi
    if [ "$dir" = forward ]; then
        for p in "${patches[@]}"; do
            if ! GIT_INDEX_FILE="$tmp" git -C "$root" apply --cached "$p" >/dev/null 2>&1; then
                eval "failed_forward='$(basename "$p")'"; rc=1; break
            fi
        done
    else
        for ((i = ${#patches[@]} - 1; i >= 0; i--)); do
            p="${patches[$i]}"
            if ! GIT_INDEX_FILE="$tmp" git -C "$root" apply --cached --reverse "$p" >/dev/null 2>&1; then
                eval "failed_reverse='$(basename "$p")'"; rc=1; break
            fi
        done
    fi
    rm -f "$tmp"
    return $rc
}

if try_stack forward; then
    if [ "$mode" = check ]; then
        echo "待套用：${#patches[@]} 份补丁能从当前树上一路套到底（未改动任何文件）"
        exit 0
    fi
elif try_stack reverse; then
    echo "已就位：这套补丁的内容已经在工作树里（${#patches[@]} 份都能反向撤销）"
    exit 0
else
    echo "冲突：工作树既不接受正向套用，也不接受反向撤销。" >&2
    echo "  正向先卡在 ${failed_forward:-未知}；反向先卡在 ${failed_reverse:-未知}。" >&2
    echo "  常见原因：基线不是 1b8a239（本分支与 origin/main 的公共祖先，见 README）、只套了一半、或手工改过补丁覆盖的文件。" >&2
    echo "  自查：git -C $root status --short" >&2
    echo "        git -C $root apply --check $here/0001-*.patch" >&2
    exit 1
fi

# 逐份套：git apply 不跨文件保持状态，必须一份份来
for p in "${patches[@]}"; do
    if git -C "$root" apply "$p"; then
        printf '  套用 %s\n' "$(basename "$p")"
    else
        printf '失败 %s（已套的部分不回滚，请 git status 复核）\n' "$(basename "$p")" >&2
        exit 1
    fi
done

echo "完成：${#patches[@]} 份补丁全部套用。接着 ./idf.sh build；字库想自己生成就 bash tools/gen_fonts.sh（见 README）"
exit 0
