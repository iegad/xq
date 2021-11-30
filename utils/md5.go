package utils

import (
	"crypto/md5"
	"encoding/hex"
	"strings"
)

func MD5(data []byte) []byte {
	if data == nil {
		return nil
	}

	h := md5.New()
	h.Write(data)
	return h.Sum(nil)
}

func MD5Ex(data string, cap ...bool) string {
	res := hex.EncodeToString(MD5(String2Bytes(data)))
	if len(cap) > 0 && cap[0] {
		res = strings.ToUpper(res)
	}

	return res
}
