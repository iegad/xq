package utils

import (
	"encoding/hex"
	"testing"
)

func TestUUID(t *testing.T) {
	uid, err := UUID_String()
	if err != nil {
		t.Error(err)
	} else {
		t.Log(uid)
	}

	udata, err := UUID_Bytes()
	if err != nil {
		t.Error(err)
	} else {
		t.Log(hex.EncodeToString(udata))
	}
}

func TestUUID1(t *testing.T) {
	uid, _ := UUID_Bytes()
	t.Log(hex.EncodeToString(uid))
	ustr, err := UUID_String(uid)
	if err != nil {
		t.Error(err)
	} else {
		t.Log(ustr)
	}
}

func TestUUID2(t *testing.T) {
	uid, _ := UUID_String()
	t.Log(uid)

	udata, err := UUID_Bytes(uid)
	if err != nil {
		t.Error(err)
	} else {
		t.Log(hex.EncodeToString(udata))
	}
}
