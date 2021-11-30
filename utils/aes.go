package utils

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"errors"
)

var (
	ErrDst = errors.New("ciphertext is not a multiple of the block size")
)

func pkcs7Padding(data []byte, blockSize int) []byte {
	padding := blockSize - len(data)%blockSize
	chs := bytes.Repeat([]byte{byte(padding)}, padding)
	return append(data, chs...)
}

func pkcs7UnPadding(data []byte) []byte {
	length := len(data)
	if length == 0 {
		return nil
	}

	unpadding := int(data[length-1])

	if length < unpadding {
		return nil
	}

	return data[:(length - unpadding)]
}

func AES128Encode(raw, key []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	data := pkcs7Padding(raw, block.BlockSize())
	mode := cipher.NewCBCEncrypter(block, MD5(key))
	mode.CryptBlocks(data, data)
	return data, nil
}

func AES128Decode(endata, key []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	bsize := block.BlockSize()
	if len(endata)%bsize != 0 {
		return nil, ErrDst
	}

	mode := cipher.NewCBCDecrypter(block, MD5(key))
	mode.CryptBlocks(endata, endata)
	src := pkcs7UnPadding(endata)
	if endata == nil {
		return nil, ErrDst
	}

	return src, nil
}
