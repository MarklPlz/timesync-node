import socket

MCAST_GRP = '224.0.0.1'
MCAST_PORT = 12345
MULTICAST_TTL = 10   # time to travel: 2 Hops

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, MULTICAST_TTL)

sock.sendto(bytearray.fromhex('BE BA FE CA ED FE EF BE AD DE 43 5A'), (MCAST_GRP, MCAST_PORT))