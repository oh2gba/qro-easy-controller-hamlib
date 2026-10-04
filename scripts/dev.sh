#!/usr/bin/env bash
# Build and test qro-easy-controller-hamlib in Docker; nothing is installed on the host.
#   scripts/dev.sh image    build the toolchain image (Debian trixie, Qt 6, Hamlib)
#   scripts/dev.sh build    configure and build into build/
#   scripts/dev.sh test     run the tests in a container without network
#   scripts/dev.sh shots    screenshots of the windows (2x) into build/shots
#   scripts/dev.sh icons    render data/icons, the .ico and the .icns from the SVG
#   scripts/dev.sh shell    interactive shell in the toolchain image
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=ech-build:trixie
USER_IDS="$(id -u):$(id -g)"
EXTRA_ENV=()

docker_cmd() {
    if docker info >/dev/null 2>&1; then
        docker "$@"
    else
        sg docker -c "$(printf '%q ' docker "$@")" # member of docker, but not in this session yet
    fi
}

ensure_image() {
    docker_cmd image inspect "$IMAGE" >/dev/null 2>&1 || docker_cmd build -t "$IMAGE" docker/
}

# Root inside the container only to create the dummy interface, then the tests run as the caller.
run_isolated() {
    docker_cmd run --rm --network none --cap-add NET_ADMIN \
        --sysctl net.ipv4.ip_unprivileged_port_start=0 "${EXTRA_ENV[@]}" \
        -e HOME=/tmp -e LANG=C.UTF-8 -e QT_QPA_PLATFORM=offscreen -e ECH_NETNS_TEST=1 \
        -v "$PWD:/src" -w /src "$IMAGE" bash -c '
            set -e
            ip link add d0 type dummy
            ip addr add 10.99.0.1/24 dev d0
            ip addr add 10.99.0.7/24 dev d0
            if [ -n "${ECH_EXTRA_ADDR:-}" ]; then ip addr add "$ECH_EXTRA_ADDR" dev d0; fi
            ip link set d0 up
            exec setpriv --reuid='"${USER_IDS%:*}"' --regid='"${USER_IDS#*:}"' --clear-groups -- "$@"
        ' bash "$@"
}

case "${1:-build}" in
image)
    docker_cmd build -t "$IMAGE" docker/
    ;;
build)
    ensure_image
    docker_cmd run --rm -u "$USER_IDS" -e HOME=/tmp -e LANG=C.UTF-8 -v "$PWD:/src" -w /src "$IMAGE" bash -c '
        cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DECH_TESTS=ON >/dev/null
        cmake --build build'
    ;;
test)
    ensure_image
    run_isolated ctest --test-dir build --output-on-failure "${@:2}"
    ;;
shots)
    ensure_image
    mkdir -p build/shots
    EXTRA_ENV=(-e ECH_SHOTS=/src/build/shots -e QT_SCALE_FACTOR=2 -e ECH_EXTRA_ADDR=192.168.1.45/24)
    run_isolated build/tst_ui
    ;;
icons)
    ensure_image
    docker_cmd run --rm -u "$USER_IDS" -v "$PWD:/src" -w /src/data "$IMAGE" bash -c '
        set -e
        name=qro-easy-controller-hamlib
        mkdir -p icons
        for s in 16 24 32 48 64 128 256 512 1024; do rsvg-convert -w $s -h $s $name.svg -o icons/$s.png; done
        icotool -c -o $name.ico icons/{16,24,32,48,64,128}.png -r icons/256.png
        png2icns $name.icns icons/{16,32,48,128,256,512,1024}.png >/dev/null'
    ;;
shell)
    ensure_image
    docker_cmd run --rm -it -u "$USER_IDS" -e HOME=/tmp -e LANG=C.UTF-8 -v "$PWD:/src" -w /src "$IMAGE" bash
    ;;
*)
    sed -n '2,9p' "$0"
    exit 2
    ;;
esac
