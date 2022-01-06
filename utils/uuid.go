package utils

import (
	"errors"

	"github.com/google/uuid"
)

var (
	ErrUuidLength = errors.New("uuid length not equal 16")
)

func UUID_Bytes(ustr ...string) ([]byte, error) {
	var (
		uid uuid.UUID
		err error
	)

	if len(ustr) > 0 {
		if len(ustr[0]) != 36 {
			return nil, ErrUuidLength
		}

		uid, err = uuid.Parse(ustr[0])
	} else {
		uid, err = uuid.NewUUID()
	}

	if err != nil {
		return nil, err
	}

	tmp := [16]byte(uid)
	return tmp[:], nil
}

func UUID_String(udata ...[]byte) (string, error) {
	var (
		uid uuid.UUID
		err error
	)

	if len(udata) > 0 {
		if len(udata[0]) != 16 {
			return "", ErrUuidLength
		}

		uid, err = uuid.FromBytes(udata[0])
	} else {
		uid, err = uuid.NewUUID()
	}

	if err != nil {
		return "", err
	}

	return uid.String(), nil
}
