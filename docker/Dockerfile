# Build and test environment for qro-easy-controller-hamlib (Debian trixie, Qt 6.8, Hamlib 4.6).
FROM debian:trixie-slim
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build \
      qt6-base-dev qt6-websockets-dev qt6-qpa-plugins \
      libhamlib-utils iproute2 fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*
# Icon rendering: SVG to PNG, Windows .ico, macOS .icns.
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      librsvg2-bin icoutils icnsutils \
 && rm -rf /var/lib/apt/lists/*
