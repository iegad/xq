package security

import (
	"encoding/base64"
	"testing"
)

func TestAes(t *testing.T) {
	context := "{\"ScnCode\":\"A0001\",\"IP\":\"139.198.115.60\"}"
	key := "1t7YC67xVd6MJkX0"
	data, err := AesEncrypt([]byte(context), []byte(key))
	if err != nil {
		t.Error(err)
		return
	}

	t.Log(base64.StdEncoding.EncodeToString(data))

	raw, err := AesDecrypt(data, []byte(key))
	if err != nil {
		t.Error(err)
		return
	}

	t.Log(string(raw))
}
