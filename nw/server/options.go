package server

type Option struct {
	MaxConn int    `json:"maxConn"`
	Timeout int    `json:"timeout"`
	Host    string `json:"host"`
}
