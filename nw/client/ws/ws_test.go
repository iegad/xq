package ws

import (
	"testing"
	"unsafe"
)

func TestSizeof(t *testing.T) {
	cli := Client{}
	ns := unsafe.Sizeof(cli)
	t.Logf("%T size: %v\n", cli, ns)
}
