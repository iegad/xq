package security

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha1"
	"encoding/base32"
	"encoding/binary"
	"fmt"
	"strings"
	"time"
)

var (
	OptAuth *googleAuth
)

func init() {
	OptAuth = &googleAuth{}
}

type googleAuth struct {
}

func (this_ *googleAuth) un() int64 {
	return time.Now().UnixNano() / 1000 / 30
}

func (this_ *googleAuth) hmacSha1(key, data []byte) []byte {
	h := hmac.New(sha1.New, key)
	if total := len(data); total > 0 {
		h.Write(data)
	}
	return h.Sum(nil)
}

func (this_ *googleAuth) base32encode(src []byte) string {
	return base32.StdEncoding.EncodeToString(src)
}

func (this_ *googleAuth) base32decode(s string) ([]byte, error) {
	return base32.StdEncoding.DecodeString(s)
}

func (this_ *googleAuth) toBytes(value int64) []byte {
	var result []byte
	mask := int64(0xFF)
	shifts := [8]uint16{56, 48, 40, 32, 24, 16, 8, 0}
	for _, shift := range shifts {
		result = append(result, byte((value>>shift)&mask))
	}
	return result
}

func (this_ *googleAuth) toUint32(bts []byte) uint32 {
	return (uint32(bts[0]) << 24) + (uint32(bts[1]) << 16) +
		(uint32(bts[2]) << 8) + uint32(bts[3])
}

func (this_ *googleAuth) oneTimePassword(key []byte, data []byte) uint32 {
	hash := this_.hmacSha1(key, data)
	offset := hash[len(hash)-1] & 0x0F
	hashParts := hash[offset : offset+4]
	hashParts[0] = hashParts[0] & 0x7F
	number := this_.toUint32(hashParts)
	return number % 1000000
}

// 获取秘钥
func (this_ *googleAuth) GetSecret() string {
	var buf bytes.Buffer
	binary.Write(&buf, binary.BigEndian, this_.un())
	return strings.ToUpper(this_.base32encode(this_.hmacSha1(buf.Bytes(), nil)))
}

// 获取动态码
func (this_ *googleAuth) GetCode(secret string) (string, error) {
	secretUpper := strings.ToUpper(secret)
	secretKey, err := this_.base32decode(secretUpper)
	if err != nil {
		return "", err
	}
	number := this_.oneTimePassword(secretKey, this_.toBytes(time.Now().Unix()/30))
	return fmt.Sprintf("%06d", number), nil
}

// 获取动态码二维码内容
func (this_ *googleAuth) Url(user, secret string) string {
	return fmt.Sprintf("otpauth://totp/%s?secret=%s", user, secret)
}

// 验证动态码
func (this_ *googleAuth) Verify(secret, code string) (bool, error) {
	_code, err := this_.GetCode(secret)
	if err != nil {
		return false, err
	}
	return _code == code, nil
}
