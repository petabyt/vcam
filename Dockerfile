FROM debian:13-slim

RUN apt-get update && apt-get install -y --no-install-recommends bash meson cmake ninja-build gcc g++ pkg-config libusb-1.0-0-dev libglib2.0-dev libsoup-3.0-dev pandoc
RUN apt-get install -y --no-install-recommends libexif-dev hostapd iptables busybox docker.io iproute2 iw rfkill udhcpd

RUN mkdir -p /var/lib/misc

COPY scripts/wlanstart.sh /bin/wlanstart.sh
RUN chmod +x /bin/wlanstart.sh

RUN mkdir -p vcam
COPY . vcam
RUN rm -rf vcam/build vcam/upnp/build
RUN cp -r vcam ~/vcam

RUN <<EOR
cd ~/vcam
cmake -G Ninja -B build
cmake --build build
cmake --install build
EOR

RUN <<EOR
cd ~/vcam/upnp
cmake -G Ninja -B build
cmake --build build
cmake --install build
EOR

#ENTRYPOINT ["/bin/wlanstart.sh"]
