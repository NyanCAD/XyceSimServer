# SPDX-FileCopyrightText: 2022 Pepijn de Vos
#
# SPDX-License-Identifier: GPL-3.0-or-later

docker build -f xyce.dockerfile -t pepijndevos/xycesimserver:xyce .
docker build -f server.dockerfile -t pepijndevos/xycesimserver:latest .