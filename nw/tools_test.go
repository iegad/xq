package nw

import "testing"

func TestLocalIPs(t *testing.T) {
	res, err := GetLocalAcitveDeviceIPv4()
	if err != nil {
		t.Error(err)
		return
	}

	for _, ip := range res {
		t.Log(ip.String())
	}
}
