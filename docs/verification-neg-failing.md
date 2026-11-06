# 负向验收：失败检查应拒绝

本分支故意让 Firmware build 失败（main.cpp 追加 #error），用于运行级验证 merge gate 对失败检查的拒绝；不合并。
