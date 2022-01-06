package ex

import (
	"testing"
)

func TestEtcdBasic(t *testing.T) {
	ec, err := NewEtcd([]string{"127.0.0.1:2379"})
	if err != nil {
		t.Error(err)
		return
	}

	defer ec.Close()

	err = ec.Put("test1", "iegad")
	if err != nil {
		t.Error(err)
		return
	}

	rm, err := ec.Get("test1")
	if err != nil {
		t.Error(err)
		return
	}
	t.Log(rm)

	ks, err := ec.GetKeys("")
	if err != nil {
		t.Error(err)
		return
	}

	t.Log(ks)

	ec.Delete("test1")
}
