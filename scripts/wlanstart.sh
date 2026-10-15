#!/bin/bash -e

# Check if running in privileged mode
if [ ! -w "/sys" ] ; then
    echo "[Error] Not running in privileged mode."
    exit 1
fi

# Default values
true ${INTERFACE:=wlan0}
true ${SUBNET:=192.168.254.0}
true ${AP_ADDR:=192.168.254.1}
true ${SSID:=docker-ap}
true ${CHANNEL:=11}
true ${WPA_PASSPHRASE:=passw0rd}
true ${HW_MODE:=g}
true ${DRIVER:=nl80211}
true ${HT_CAPAB:=[HT40-][SHORT-GI-20][SHORT-GI-40]}
true ${MODE:=host}

if [ ! -f "/etc/hostapd.conf" ] ; then
    cat > "/etc/hostapd.conf" <<EOF
interface=${INTERFACE}
driver=${DRIVER}
ssid=${SSID}
hw_mode=${HW_MODE}
channel=${CHANNEL}
wpa=2
wpa_passphrase=${WPA_PASSPHRASE}
wpa_key_mgmt=WPA-PSK
# TKIP is no secure anymore
#wpa_pairwise=TKIP CCMP
wpa_pairwise=CCMP
rsn_pairwise=CCMP
wpa_ptk_rekey=600
ieee80211n=1
ht_capab=${HT_CAPAB}
wmm_enabled=1 
EOF

fi

# unblock wlan
rfkill unblock wlan

echo "Setting interface ${INTERFACE}"

# Setup interface
ip link set ${INTERFACE} up
ip addr flush dev ${INTERFACE}
ip addr add ${AP_ADDR}/24 dev ${INTERFACE}

# NAT settings
echo "Setting up traffic redirection..."

## Clear existing rules
#iptables -t nat -F
#iptables -F FORWARD
#
## Redirect all DNS queries to AP
#iptables -t nat -A PREROUTING -i ${INTERFACE} -p udp --dport 53 -j DNAT --to ${AP_ADDR}
#iptables -t nat -A PREROUTING -i ${INTERFACE} -p tcp --dport 53 -j DNAT --to ${AP_ADDR}
#
## Redirect web traffic to port 8000
#iptables -t nat -A PREROUTING -i ${INTERFACE} -p tcp --dport 80 -j DNAT --to ${AP_ADDR}:8000
#iptables -t nat -A PREROUTING -i ${INTERFACE} -p tcp --dport 443 -j DNAT --to ${AP_ADDR}:8000
#
## Enable NAT for return traffic
#iptables -t nat -A POSTROUTING -j MASQUERADE

echo "Configuring DHCP server .."

# Setup DHCP
mkdir -p /var/lib/misc
touch /var/lib/misc/udhcpd.leases

cat > "/etc/udhcpd.conf" <<EOF
interface ${INTERFACE}
start ${SUBNET::-1}100
end ${SUBNET::-1}200
option subnet 255.255.255.0
option router ${AP_ADDR}
option dns ${AP_ADDR}
lease_file /var/lib/misc/udhcpd.leases
auto_time 0
decline_time 0
conflict_time 0
EOF

echo "Starting DHCP server .."
udhcpd -f /etc/udhcpd.conf &

echo "Starting HostAP daemon ..."
/usr/sbin/hostapd /etc/hostapd.conf

