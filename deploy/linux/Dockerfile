# Linux (x86_64) image with the servers of the three versions, for a remote host.
#
#   docker buildx build --platform linux/amd64 -f deploy/linux/Dockerfile -t lastchaos-servers .
#
# Built from the repository root. Only the server sources go in; the image holds the binaries in
# /opt/lc/<version>/bin and deploy/linux/lc-servers.sh, which runs one version's servers. The
# configs and the GameServer maps come from the host (/srv/lc/<version>, see deploy/linux/README.md).

# The build runs on the build machine's own architecture and cross-compiles for x86_64 (under
# QEMU, gcc crashed on Connector/ProcConnMsg.cpp): Debian's cross gcc and the amd64 -dev packages.
FROM --platform=$BUILDPLATFORM debian:trixie AS deps
RUN dpkg --add-architecture amd64 && apt-get update && apt-get install -y --no-install-recommends \
      g++-x86-64-linux-gnu binutils-x86-64-linux-gnu cmake ninja-build pkg-config \
      libboost-thread-dev:amd64 libboost-system-dev:amd64 libmariadb-dev:amd64 libmariadb-dev-compat:amd64 \
      liblog4cxx-dev:amd64 libapr1-dev:amd64 libaprutil1-dev:amd64 libjsoncpp-dev:amd64 libexpat1-dev:amd64 \
      libbotan-3-dev:amd64 libcurl4-openssl-dev:amd64 libssl-dev:amd64 zlib1g-dev:amd64 \
    && rm -rf /var/lib/apt/lists/*

# (also used alone to iterate on the build: docker buildx build --target deps ...)
FROM deps AS build
COPY deploy/linux/x86_64-linux-gnu.cmake /src/toolchain.cmake
COPY ["v2009/Server Source", "/src/v2009/Server Source"]
COPY ["v2015/CMakeLists.txt", "/src/v2015/"]
COPY ["v2015/Server Source", "/src/v2015/Server Source"]
COPY ["v2018/CMakeLists.txt", "/src/v2018/"]
COPY ["v2018/Server Source", "/src/v2018/Server Source"]

# The same build types and regions as on the Mac (HANDOFF.md)
ENV TC=-DCMAKE_TOOLCHAIN_FILE=/src/toolchain.cmake
RUN cmake -S "/src/v2009/Server Source" -B /build/v2009 -G Ninja $TC -DLC_REGION=USA \
    && cmake --build /build/v2009
RUN cmake -S /src/v2015 -B /build/v2015 -G Ninja $TC -DCMAKE_BUILD_TYPE=Debug -DLC_REGION=BRZ \
    && cmake --build /build/v2015
RUN cmake -S /src/v2018 -B /build/v2018 -G Ninja $TC -DCMAKE_BUILD_TYPE=Debug -DLC_REGION=USA \
    && cmake --build /build/v2018

# The binaries, and the Debian packages of the libraries they need (installed in the runtime image;
# apt brings those libraries' own dependencies)
RUN set -e; \
    for v in v2009 v2015 v2018; do \
      mkdir -p /opt/lc/$v/bin; \
      d=/build/$v; [ -d "$d/Server Source" ] && d="$d/Server Source"; \
      for s in Messenger Connector Helper SubHelper GameServer LoginServer; do \
        [ -x "$d/$s" ] && cp "$d/$s" /opt/lc/$v/bin/; \
      done; \
    done; \
    for b in /opt/lc/*/bin/*; do x86_64-linux-gnu-readelf -d "$b"; done \
      | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort -u \
      | while read -r lib; do dpkg -S "/usr/lib/x86_64-linux-gnu/$lib" "/lib/x86_64-linux-gnu/$lib" 2>/dev/null || true; done \
      | cut -d: -f1 | sort -u > /opt/lc/packages.txt; \
    cat /opt/lc/packages.txt

FROM debian:trixie-slim
# --build-arg DEBIAN_MIRROR=debian.c3sl.ufpr.br when deb.debian.org answers 503 (it did, 2026-10-20)
ARG DEBIAN_MIRROR=deb.debian.org
COPY --from=build /opt/lc /opt/lc
RUN sed -i "s#http://deb.debian.org/debian\$#http://${DEBIAN_MIRROR}/debian#" /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get install -y --no-install-recommends ca-certificates procps \
      $(cat /opt/lc/packages.txt) \
    && rm -rf /var/lib/apt/lists/*
COPY deploy/linux/lc-servers.sh /opt/lc/lc-servers.sh
ENTRYPOINT ["/opt/lc/lc-servers.sh"]
