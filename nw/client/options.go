package client

type Option struct {
	Async   bool   `json:"async"`
	Timeout int    `json:"timeout"`
	Host    string `json:"host"`
}
