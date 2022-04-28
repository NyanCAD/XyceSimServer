# SPDX-FileCopyrightText: 2022 Pepijn de Vos
#
# SPDX-License-Identifier: GPL-3.0-or-later

FROM pepijndevos/xycesimserver:xyce AS build

COPY . /tmp/build
WORKDIR /tmp/build

RUN DEBIAN_FRONTEND=noninteractive apt-get -y install --no-install-recommends libboost-all-dev

RUN mkdir -p build; \
cd build; \
cmake -DCMAKE_INSTALL_PREFIX="/usr/local" ..; \
make DESTDIR=/tmp -j$(nproc) install

FROM pepijndevos/xycesimserver:xyce

COPY --from=build /tmp/usr/local /usr/local

EXPOSE 5923
CMD ["XyceSimServer"]