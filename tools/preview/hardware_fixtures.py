#!/usr/bin/env python3
"""Anonymous hardware matrix for the real C parser and LVGL renderer."""
import argparse
import json
from pathlib import Path
CASES = {"empty": (0,0,0,0,0,0), "compact": (1,0,1,4,1,1),
         "mixed": (7,3,14,72,32,5), "dense": (24,12,64,192,96,12)}
def fixture(counts):
    vols, raids, disks, temps, docker, nets = counts
    ports = [{"if":"enp%ds0-vlan-backup"%i,"rx_kbs":1250*(i+1),"tx_kbs":250*(i+1),
              "rx_total_gb":900*(i+1),"tx_total_gb":400*(i+1),"state":"up" if i%3 else "down",
              "speed_mbps":1000*(i+1),"physical":True} for i in range(nets)]
    return {"ready":True,"proto":2,"host":"anonymous-hardware-adaptive-NAS","ts":1800000000,"uptime_s":246810,
       "cpu":{"pct":36.8,"cores":max(1,temps//2),"load1":1.2,"load5":.9,"load15":.6,"runq":2,"procs":2300},
       "mem":{"total_mb":524288,"used_mb":170000,"avail_mb":354288,"pct":32.4,"swap_total_mb":8192,"swap_used_mb":100},
       "net":dict(ports[0],interfaces=ports) if ports else {},
       "vols":[{"mnt":("/存储池-龘/相册与影音归档-α" if i==0 else "/pool-%02d/backups-and-media-project-archive"%i),"fs":"btrfs","total_gb":16000*(i+1),"used_gb":11000*(i+1),"free_gb":5000*(i+1),"pct":68.75} for i in range(vols)],
       "raid":[{"dev":"storage-array-%02d"%i,"lvl":"raid6","state":"clean","ok":True,"have":12,"want":12,"sync_pct":100} for i in range(raids)],
       "disks":[{"dev":"nvme%dn1-long-stable-identity"%i,"rd_kbs":1500*(i+1),"wr_kbs":500*(i+1)} for i in range(disks)],
       "temps":[{"dev":"socket-%d"%(i//64) if i<128 else "nvme%dn1"%((i-128)//2),
                 "dn":"Processor with a long hardware model identifier and many CPU cores" if i<128 else "Storage model with a long identical display name",
                 "ch":"CPU core %03d temperature channel"%i if i<128 else "NVMe sensor channel %03d"%i,
                 "c":35+(i%20)*.6} for i in range(temps)],
       "docker":[{"n":"application-worker-%03d-with-a-long-unique-service-name"%i,"s":"Up 12 days (healthy)" if i%5 else "Exited (137) 3 hours ago","up":bool(i%5)} for i in range(docker)],
       "alerts":[],"modules":{name:{"status":"ok"} for name in ("cpu","mem","net","vols","raid","disks","temps","docker")},
       "trunc":{"limits":{},"dropped":{}}}
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("directory",type=Path)
    args=parser.parse_args()
    args.directory.mkdir(parents=True,exist_ok=True)
    for name, counts in CASES.items():
        path=args.directory/(name+".json")
        path.write_text(json.dumps(fixture(counts),ensure_ascii=False),encoding="utf-8")
        print("%s: %s, %d bytes"%(name,counts,path.stat().st_size))
if __name__ == "__main__":
    main()
