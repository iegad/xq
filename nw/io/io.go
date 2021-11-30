package io

import (
	"encoding/binary"
	"errors"
	"io"
	"math"
	"net"
)

const (
	HEAD_SIZE        = 4
	PACK_MAX_SIZE    = math.MaxInt32
	DEFAULT_MAX_RBUF = 1024 * 1024
	DEFAULT_MAX_WBUF = 1024 * 512
	_V_HEAD_KEY      = 0xFAFBFCFD
)

var (
	ErrReadHeadSize = errors.New("io.Readn read head size failed")
	ErrReadDataSize = errors.New("io.Readn invalid data size")
	ErrReadData     = errors.New("io.Readn read data failed")

	ErrWriteDataSize = errors.New("io.Writen invalid data size")
	ErrWriteData     = errors.New("io.Writen write data failed")

	_HeadKey = uint32(0)
)

func init() {
	tmp := make([]byte, 4)
	binary.BigEndian.PutUint32(tmp, _V_HEAD_KEY)
	_HeadKey = binary.BigEndian.Uint32(tmp)
}

func Readn(c net.Conn) ([]byte, error) {
	var (
		n   int
		err error
	)

	hbuf := make([]byte, HEAD_SIZE)

	for {
		n, err = io.ReadAtLeast(c, hbuf, HEAD_SIZE)
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() && !nerr.Timeout() {
				continue
			}

			return nil, err
		}
		break
	}

	if n != HEAD_SIZE {
		return nil, ErrReadHeadSize
	}

	tmp := binary.BigEndian.Uint32(hbuf)
	hlen := int(^tmp ^ _HeadKey)

	if hlen < 0 {
		return nil, ErrReadDataSize
	}

	if hlen == 0 {
		return []byte{}, nil
	}

	if hlen > PACK_MAX_SIZE {
		return nil, ErrReadDataSize
	}

	data := make([]byte, hlen)

	for {
		n, err = io.ReadAtLeast(c, data, hlen)
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() && !nerr.Timeout() {
				continue
			}

			return nil, err
		}
		break
	}

	if n != hlen {
		return nil, ErrReadData
	}

	return data, nil
}

func Writen(c net.Conn, data []byte) error {
	if data == nil {
		return nil
	}

	hlen := len(data)

	if hlen > PACK_MAX_SIZE {
		return ErrWriteDataSize
	}

	buflen := HEAD_SIZE + hlen
	buf := make([]byte, buflen)

	tmp := ^uint32(hlen) ^ _HeadKey
	binary.BigEndian.PutUint32(buf[:HEAD_SIZE], tmp)

	copy(buf[HEAD_SIZE:], data)

	var (
		n   int
		err error
	)

	for {
		n, err = c.Write(buf)
		if err != nil {
			if nerr, ok := err.(net.Error); ok && nerr.Temporary() && !nerr.Timeout() {
				continue
			}

			return err
		}
		break
	}

	if n != buflen {
		return ErrWriteData
	}

	return nil
}
