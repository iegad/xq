package ex

import (
	"encoding/json"

	"github.com/iegad/xq/log"
	"github.com/iegad/xq/utils"
	"google.golang.org/protobuf/proto"
)

func Pb2Json(m proto.Message) string {
	data, err := json.Marshal(m)
	if err != nil {
		log.Fatal(err)
	}

	return utils.Bytes2String(data)
}

func Pb2Bytes(m proto.Message) []byte {
	data, err := proto.Marshal(m)
	if err != nil {
		log.Fatal(err)
	}

	return data
}
