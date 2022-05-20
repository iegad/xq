package utils

import (
	"testing"
)

func TestSys(t *testing.T) {
	t.Log(IsLittleEndian)
	t.Log(IsBigEndian)

	t.Logf("0x%04x\n", 0x0102)
	t.Logf("0x%04x\n", Htons(0x0102))
	t.Logf("0x%08x\n", 0x01020304)
	t.Logf("0x%08x\n", Htonl(0x01020304))
	t.Logf("0x%16x\n", 0x0102030405060708)
	t.Logf("0x%16x\n", Htonll(0x0102030405060708))

	t.Logf("l: %x => b: %x", Uint16ToL(0x0102), Uint16ToB(0x0102))
	t.Logf("l: %x => b: %x", Uint32ToL(0x01020304), Uint32ToB(0x01020304))
	t.Logf("l: %x => b: %x", Uint64ToL(0x0102030405060708), Uint64ToB(0x0102030405060708))
}