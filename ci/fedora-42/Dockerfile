FROM fedora:42

# A version field to invalidate Cirrus's build cache when needed, as suggested in
# https://github.com/cirruslabs/cirrus-ci-docs/issues/544#issuecomment-566066822
ENV DOCKERFILE_VERSION 20250508

RUN dnf -y install \
    bison \
    ccache \
    cmake \
    cppzmq-devel \
    diffutils \
    findutils \
    flex \
    gawk \
    gcc \
    gcc-c++ \
    git \
    jq \
    libpcap-devel \
    make \
    nodejs-devel \
    openssl \
    openssl-devel \
    procps-ng \
    python3 \
    python3-devel \
    python3-pip\
    sqlite \
    swig \
    which \
    zlib-devel \
    crypto-policies-scripts \
  && dnf clean all && rm -rf /var/cache/dnf

RUN pip3 install websockets junit2html
