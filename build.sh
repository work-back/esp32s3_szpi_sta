#!/bin/bash

set -e

# Docker Compose 文件所在目录
COMPOSE_DIR="/home/langyj/w2/zephyr/docker"

# Docker 容器
CONTAINER="zephyr"

# Docker 容器内的 Zephyr 工程目录
PROJECT_DIR="/home/langyj/zephyrproject/myprj"

echo "========================================"
echo "Zephyr build"
echo "========================================"
echo "Container : $CONTAINER"
echo "Project   : $PROJECT_DIR"
echo "Command   : west build -p always -d build-sim_rc -b native_sim sim_rc"
echo "========================================"

# 1. 如果容器不存在或没有运行，则启动
if ! docker inspect "$CONTAINER" >/dev/null 2>&1; then
    echo "[INFO] Container '$CONTAINER' does not exist."
    echo "[INFO] Starting container..."

    docker compose \
        -f "$COMPOSE_DIR/docker-compose.yml" \
        up -d
elif [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER")" != "true" ]; then
    echo "[INFO] Container '$CONTAINER' is not running."
    echo "[INFO] Starting container..."

    docker compose \
        -f "$COMPOSE_DIR/docker-compose.yml" \
        up -d
fi

# 2. 等待容器真正进入 running 状态
while [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER")" != "true" ]; do
    sleep 1
done

echo "[INFO] Container is running."

# 3. 在 Docker 容器中执行 west build
docker exec \
    -w "$PROJECT_DIR" \
    "$CONTAINER" \
    bash -c ' source /home/langyj/zephyrproject/.venv/bin/activate && west build -p always -d build-sim_rc -b native_sim sim_rc'

echo "========================================"
echo "Build successful"
echo "========================================"
