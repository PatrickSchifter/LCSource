#!/bin/bash
# Host networking for the servers on a cloud host whose public IP is NAT (Oracle Cloud): the
# LoginServer and GameServer bind to the address they advertise, which is not on any interface.
# The public IP goes on lo, the game ports arriving at the private IP are DNATed to it, and the
# host firewall lets them in. Idempotent; run by lc-network.service at boot.
#
#   lc-network.sh <public IP> <private IP>
set -euo pipefail
PUBLIC=$1
PRIVATE=$2
PORTS=4001,4010,4201,4210,4301,4310   # LoginServer, GameServer of the 2015, 2009 and 2018

ip addr show dev lo | grep -q " $PUBLIC/32 " || ip addr add "$PUBLIC/32" dev lo

dnat=(PREROUTING -d "$PRIVATE" -p tcp -m multiport --dports "$PORTS" -j DNAT --to-destination "$PUBLIC")
iptables -t nat -C "${dnat[@]}" 2>/dev/null || iptables -t nat -I "${dnat[@]}"

# before the image's final REJECT in INPUT
accept=(INPUT -p tcp -m multiport --dports "$PORTS" -j ACCEPT)
iptables -C "${accept[@]}" 2>/dev/null || iptables -I "${accept[@]}"
