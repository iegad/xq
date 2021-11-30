package client

type EncodeHandler func(IClient, []byte) ([]byte, error)
type DecodeHandler EncodeHandler

type Option struct {
	Async   bool          `json:"async"`
	Timeout int           `json:"timeout"`
	Encoder EncodeHandler `json:"-"`
	Decoder DecodeHandler `json:"-"`
	Host    string        `json:"host"`
}
