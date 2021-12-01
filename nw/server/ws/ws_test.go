package ws

import (
	"testing"
	"unsafe"
)

func TestConn(t *testing.T) {
	sess := conn{}
	nsize := unsafe.Sizeof(sess)

	t.Logf("%T size: %v\n", sess, nsize)
}

func TestServer(t *testing.T) {
	serv := Server{}
	nsize := unsafe.Sizeof(serv)
	t.Logf("%T size: %v\n", serv, nsize)
}
