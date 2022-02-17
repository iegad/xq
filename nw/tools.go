package nw

import (
	"errors"
	"net"
)

var (
	ErrNotIPV4IF = errors.New("no ipv4 address")
)

func GetLocalIPV4() (string, error) {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return "", err
	}

	for _, value := range addrs {
		if ipnet, ok := value.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
			if ipnet.IP.To4() != nil {
				return ipnet.IP.String(), nil
			}
		}
	}

	return "", ErrNotIPV4IF
}
