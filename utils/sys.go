package utils

import (
	"unsafe"
)

var (
	IsLittleEndian bool
	IsBigEndian    bool
)

func init() {
	v := int16(0x0201)
	p := unsafe.Pointer(&v)
	pb := (*byte)(p)
	if *pb == 1 {
		IsLittleEndian = true
	} else {
		IsBigEndian = true
	}
}

func Htons(x uint16) uint16 {
	return ((x & 0xff00) >> 8) | ((x & 0x00ff) << 8)
}

func Htonl(x uint32) uint32 {
	return ((x & 0xff000000) >> 24) | ((x & 0x00ff0000) >> 8) | ((x & 0x0000ff00) << 8) | ((x & 0x000000ff) << 24)
}

func Htonll(x uint64) uint64 {
	return ((x & 0xff00000000000000) >> 56) | ((x & 0x00ff000000000000) >> 40) | ((x & 0x0000ff0000000000) >> 24) | ((x & 0x000000ff00000000) >> 8) |
		((x & 0x00000000ff000000) << 8) | ((x & 0x0000000000ff0000) << 24) | ((x & 0x000000000000ff00) << 40) | ((x & 0x00000000000000ff) << 56)
}

func Uint16ToB(x uint16) uint16 {
	if IsBigEndian {
		return x
	}
	return Htons(x)
}

func Uint16ToL(x uint16) uint16 {
	if IsLittleEndian {
		return x
	}
	return Htons(x)
}

func Uint32ToB(x uint32) uint32 {
	if IsBigEndian {
		return x
	}
	return Htonl(x)
}

func Uint32ToL(x uint32) uint32 {
	if IsLittleEndian {
		return x
	}
	return Htonl(x)
}

func Uint64ToB(x uint64) uint64 {
	if IsBigEndian {
		return x
	}
	return Htonll(x)
}

func Uint64ToL(x uint64) uint64 {
	if IsLittleEndian {
		return x
	}
	return Htonll(x)
}
