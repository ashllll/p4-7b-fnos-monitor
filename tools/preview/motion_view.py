#!/usr/bin/env python3
"""Create an offline image-sequence viewer beside motion.csv."""
import argparse
import csv
import html
import json
import math
import os
from pathlib import Path
import re
import tempfile
from urllib.parse import quote


HEAD = r'''</title><meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#090d12;color:#e4e8ee}
*{box-sizing:border-box}body{margin:0;padding:clamp(16px,3vw,36px)}
main{max-width:1200px;margin:auto}h1{font-size:1.35rem;margin:0 0 8px}
p{color:#a8b3c2;line-height:1.6;margin:0 0 20px}h1,p{text-wrap:pretty}
.screen{background:#000;border:1px solid #263140;border-radius:12px;overflow:hidden}
.screen img{display:block;width:100%;height:auto;object-fit:contain}
.controls{display:flex;align-items:center;flex-wrap:wrap;gap:12px;margin:16px 0}
button,select{font:inherit;background:#17212e;color:inherit;border:1px solid #344255;
border-radius:8px;min-height:44px;padding:8px 14px;cursor:pointer}
button:focus-visible,select:focus-visible,input:focus-visible,a:focus-visible{
outline:2px solid #60a5fa;outline-offset:3px}button:hover{background:#243348}
label{display:flex;align-items:center;gap:8px}input{accent-color:#60a5fa}
.seek{width:100%;gap:14px}.seek input{min-width:0;flex:1;min-height:44px}
.status{font-variant-numeric:tabular-nums;overflow-wrap:anywhere;color:#b9c6d6}
#error{color:#fca5a5;margin:10px 0}#static{margin-top:28px}h2{font-size:1rem}
.gallery{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,240px),1fr));gap:16px}
figure{margin:0}figure img{width:100%;height:auto;border:1px solid #263140;border-radius:8px}
figcaption{margin:8px 0;color:#a8b3c2;overflow-wrap:anywhere}a{color:#93c5fd}
[hidden]{display:none!important}
</style></head><body><main>
<h1>LVGL 动效预览</h1>
<p>原生 LVGL 匿名 NAS 数据预览；非实机验收。播放使用 CSV 时间间隔；重复时间戳按至少 16 ms 展示。</p>
<div class="screen"><img id="frame" alt="原生 LVGL 当前预览帧"></div>
<div id="error" role="alert" hidden></div>
<div class="controls">
<button id="play" type="button" aria-pressed="false">播放</button>
<label>倍速 <select id="speed" aria-label="播放倍速">
<option value="0.25">0.25 倍</option><option value="0.5">0.5 倍</option>
<option value="1" selected>1 倍</option></select></label>
<span id="count" class="status"></span>
<label class="seek">帧位置 <input id="seek" type="range" min="0" step="1" value="0" aria-label="预览帧位置"></label>
</div><div id="status" class="status"></div>
<section id="static" hidden><h2>健康状态各页</h2><div id="gallery" class="gallery"></div></section>
</main><script id="data" type="application/json">'''

TAIL = r'''</script><script>
"use strict";
const data=JSON.parse(document.getElementById("data").textContent),frames=data.frames;
const byId=id=>document.getElementById(id),image=byId("frame"),play=byId("play");
const seek=byId("seek"),speed=byId("speed"),error=byId("error");
const timeline=[0];
for(let i=1;i<frames.length;i++)timeline.push(timeline[i-1]+Math.max(16,frames[i].time_ms-frames[i-1].time_ms));
let index=0,position=0,last=null,playing=false,raf=null;
seek.max=String(frames.length-1);
function show(i){
  index=i;const frame=frames[i];image.src=frame.file;seek.value=String(i);
  image.alt=`原生 LVGL 页面 ${frame.page}，${frame.phase}，${frame.time_ms} ms`;
  byId("count").textContent=`${i+1} / ${frames.length} 帧`;
  byId("status").textContent=`页面：${frame.page} · phase：${frame.phase} · timestamp：${frame.time_ms} ms`;
  seek.setAttribute("aria-valuetext",`${i+1} / ${frames.length} 帧`);error.hidden=true;
}
function pause(){
  playing=false;last=null;if(raf!==null)cancelAnimationFrame(raf);raf=null;
  play.textContent="播放";play.setAttribute("aria-pressed","false");
}
function tick(now){
  raf=null;if(!playing)return;
  if(last!==null)position+=Math.max(0,now-last)*Number(speed.value);last=now;
  let lo=0,hi=timeline.length;
  while(lo<hi){const mid=Math.floor((lo+hi)/2);if(timeline[mid]<=position)lo=mid+1;else hi=mid;}
  const next=Math.max(0,lo-1);if(next!==index)show(next);
  if(position>=timeline[timeline.length-1])pause();else raf=requestAnimationFrame(tick);
}
play.addEventListener("click",()=>{
  if(playing){pause();return;}if(index===frames.length-1){position=0;show(0);}
  playing=true;last=null;play.textContent="暂停";play.setAttribute("aria-pressed","true");
  raf=requestAnimationFrame(tick);
});
seek.addEventListener("input",()=>{pause();const i=Number(seek.value);position=timeline[i];show(i);});
speed.addEventListener("change",()=>{last=null;});
document.addEventListener("visibilitychange",()=>{if(document.hidden)pause();});
image.addEventListener("error",()=>{pause();error.hidden=false;error.textContent=`帧图片加载失败：${frames[index].file}`;});
for(const item of data.static){
  const figure=document.createElement("figure"),link=document.createElement("a");
  const img=document.createElement("img"),caption=document.createElement("figcaption");
  link.href=item.file;img.src=item.file;img.alt=item.label;img.loading="lazy";
  caption.textContent=item.label;link.append(img);figure.append(link,caption);byId("gallery").append(figure);
}
byId("static").hidden=data.static.length===0;show(0);
</script></body></html>'''


def safe_file(directory, name, extensions):
    if (len(name) > 255 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name)
            or ".." in name or Path(name).suffix.lower() not in extensions):
        raise ValueError(f"不安全或不支持的文件名：{name!r}")
    path = directory / name
    if not path.is_file() or path.resolve().parent != directory:
        raise ValueError(f"文件不存在或指向目录外：{name!r}")
    return path


def check_png(path):
    with path.open("rb") as stream:
        if stream.read(8) != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"不是 PNG 文件：{path.name!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path, help="已有 PNG 和 motion.csv 的目录")
    args = parser.parse_args()
    try:
        directory = args.output_dir.resolve(strict=True)
        if not directory.is_dir():
            raise ValueError("output_dir 必须是现有目录")
        source = safe_file(directory, "motion.csv", {".csv"})
        frames, checked, previous = [], set(), -1.0
        with source.open(encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream)
            fields = reader.fieldnames or []
            if len(fields) != len(set(fields)) or not {"file", "time_ms", "page", "phase"} <= set(fields):
                raise ValueError("CSV 须含唯一的 file,time_ms,page,phase 表头")
            for row_number, row in enumerate(reader, 2):
                if None in row or any(row.get(key) is None for key in fields):
                    raise ValueError(f"CSV 第 {row_number} 行列数不匹配")
                raw = row["file"]
                if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*\.(?:ppm|png)", raw) or ".." in raw:
                    raise ValueError(f"CSV 第 {row_number} 行文件名不安全")
                name = str(Path(raw).with_suffix(".png"))
                path = safe_file(directory, name, {".png"})
                if name not in checked:
                    check_png(path)
                    checked.add(name)
                timestamp = float(row["time_ms"])
                if not math.isfinite(timestamp) or timestamp < previous or timestamp < 0:
                    raise ValueError(f"CSV 第 {row_number} 行时间须有限、非负且不递减")
                page, phase = row["page"], row["phase"]
                if not page or not phase or len(page) > 80 or len(phase) > 160:
                    raise ValueError(f"CSV 第 {row_number} 行 page/phase 为空或过长")
                frames.append(dict(file=quote(name, safe=""), time_ms=timestamp, page=page, phase=phase))
                previous = timestamp
        if not frames:
            raise ValueError("motion.csv 没有预览帧")
        static = []
        static_dir = (directory.parent / "static").resolve()
        if static_dir.is_dir():
            for path in sorted(static_dir.iterdir()):
                if (path.suffix.lower() != ".png" or "healthy" not in path.name.lower()
                        or not re.search(r"(?:^|[_-])p[0-9]+(?:[_-]|\.)", path.name)):
                    continue
                path = safe_file(static_dir, path.name, {".png"})
                check_png(path)
                static.append(dict(file="../static/" + quote(path.name, safe=""), label=path.stem))
        payload = json.dumps(dict(frames=frames, static=static), ensure_ascii=False, allow_nan=False)
        for character, escape in (("&", "\\u0026"), ("<", "\\u003c"), (">", "\\u003e"),
                                  ("\u2028", "\\u2028"), ("\u2029", "\\u2029")):
            payload = payload.replace(character, escape)
        prefix = '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>'
        document = prefix + html.escape(directory.name + " · LVGL 动效预览") + HEAD + payload + TAIL
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=directory,
                                             prefix=".motion-view-", suffix=".tmp", delete=False) as stream:
                temp_name = stream.name
                stream.write(document)
            os.replace(temp_name, directory / "index.html")
        finally:
            if temp_name and os.path.exists(temp_name):
                os.unlink(temp_name)
        print(directory / "index.html")
    except (OSError, ValueError, csv.Error) as exc:
        parser.exit(1, f"错误：{exc}\n")


if __name__ == "__main__":
    main()
