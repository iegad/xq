package utils

import "unsafe"

func Bytes2String(v []byte) string {
	return *(*string)(unsafe.Pointer(&v))
}

func String2Bytes(v string) []byte {
	x := (*[2]uintptr)(unsafe.Pointer(&v))
	b := [3]uintptr{x[0], x[1], x[1]}
	return *(*[]byte)(unsafe.Pointer(&b))
}
