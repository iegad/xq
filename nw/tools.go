package nw

import (
	"errors"
	"fmt"
	"net"
	"time"

	"github.com/iegad/xq/log"
	"github.com/tatsushid/go-fastping"
)

var (
	ErrNotIPV4IF = errors.New("no ipv4 address")
)

func GetLocalIPv4() (net.IP, error) {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return nil, err
	}

	for _, value := range addrs {
		if ipnet, ok := value.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
			ip := ipnet.IP.To4()
			if ip != nil {
				return ipnet.IP, nil
			}
		}
	}

	return nil, ErrNotIPV4IF
}

func GetMacAddrs() (macAddrs []string, err error) {
	netInterfaces, err := net.Interfaces()
	if err != nil {
		return nil, err
	}

	for _, netInterface := range netInterfaces {
		macAddr := netInterface.HardwareAddr.String()
		if len(macAddr) == 0 {
			continue
		}

		macAddrs = append(macAddrs, macAddr)
	}
	return macAddrs, nil
}

func GetLocalAcitveDeviceIPv4() ([]net.IP, error) {
	ip, err := GetLocalIPv4()
	if err != nil {
		return nil, err
	}

	ipv4 := ip.To4()

	arr1 := [4]uint16{}
	arr2 := [4]uint16{}
	mask := []byte(ip.To4().DefaultMask())

	for i, v := range mask {
		if v == 255 {
			arr1[i] = uint16(ipv4[i])
			arr2[i] = uint16(ipv4[i])
		} else {
			arr1[i] = 1
			arr2[i] = 255
		}
	}

	var (
		res = []net.IP{}
		fp  = fastping.NewPinger()
	)

	for v0 := arr1[0]; v0 <= arr2[0]; v0++ {
		for v1 := arr1[1]; v1 <= arr2[1]; v1++ {
			for v2 := arr1[2]; v2 <= arr2[2]; v2++ {
				for v3 := arr1[3]; v3 <= arr2[3]; v3++ {
					ra, err := net.ResolveIPAddr("ip4:icmp", fmt.Sprintf("%d.%d.%d.%d", v0, v1, v2, v3))
					if err != nil {
						log.Error(err)
						continue
					}
					fp.AddIPAddr(ra)
				}
			}
		}
	}

	fp.OnRecv = func(i *net.IPAddr, d time.Duration) {
		res = append(res, i.IP)
	}

	err = fp.Run()
	if err != nil {
		return nil, err
	}

	return res, nil
}
