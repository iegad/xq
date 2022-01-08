package ex

import (
	"context"
	"encoding/json"

	"github.com/iegad/xq/log"
	"github.com/iegad/xq/utils"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/peer"
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

func GetRealAddr(ctx context.Context) string {
	md, ok := metadata.FromIncomingContext(ctx)

	if !ok {
		return ""
	}

	rips := md.Get("x-real-ip")
	if len(rips) == 0 {
		return ""
	}

	return rips[0]
}

func GetPeerAddr(ctx context.Context) string {
	if pr, ok := peer.FromContext(ctx); ok {
		return pr.Addr.String()
	}

	return ""
}
