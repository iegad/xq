package ex

import "github.com/google/uuid"

func UUID_Bytes() []byte {
	tmp := [16]byte(uuid.New())
	return tmp[:]
}

func UUID_String() string {
	return uuid.New().String()
}
