#!/bin/bash

# 源文件路径
SOURCE_FILE="./build/main"

# 检查源文件是否存在
if [ ! -f "$SOURCE_FILE" ]; then
    echo "错误：$SOURCE_FILE 不存在！"
    exit 1
fi

# 目标节点列表
NODES=("node1" "node2" "node3")  # 可以按需扩展，如 node4, node5...

# 遍历所有节点并拷贝
for NODE in "${NODES[@]}"; do
    DEST_DIR="./test/$NODE"
    DEST_FILE="$DEST_DIR/$NODE"  # 例如 /test/node1/node1

    # 创建目标目录（如果不存在）
    mkdir -p "$DEST_DIR"

    # 拷贝文件
    if cp "$SOURCE_FILE" "$DEST_FILE"; then
        echo "已拷贝到 $DEST_FILE"
    else
        echo "拷贝到 $DEST_FILE 失败！"
    fi
done
