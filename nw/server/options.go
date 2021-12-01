package server

// Option 服务端选项
type Option struct {
	MaxConn int32  `json:"maxConn"` // 最大连接数
	Timeout int    `json:"timeout"` // 超时
	Host    string `json:"host"`    // 服务地址
}
