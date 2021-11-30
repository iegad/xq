package server

type Option struct {
	MaxConn int32  `json:"maxConn"`
	Timeout int    `json:"timeout"`
	Host    string `json:"host"`
}
