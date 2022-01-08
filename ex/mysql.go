package ex

import (
	"database/sql"
	"errors"

	"github.com/go-sql-driver/mysql"
)

type Mysql struct {
	Host string `json:"host" yaml:"host"`
	User string `json:"user" yaml:"user"`
	Pass string `json:"pass" yaml:"pass"`
	DB   string `json:"db"   yaml:"db"`
}

var (
	errMysqlNil = errors.New("mysql config is nil")
)

func NewMysql(c *Mysql) (*sql.DB, error) {
	if c == nil {
		return nil, errMysqlNil
	}

	config := mysql.NewConfig()
	config.Net = "tcp"
	config.Addr = c.Host
	config.User = c.User
	config.Passwd = c.Pass
	config.DBName = c.DB
	config.Params = map[string]string{"charset": "utf8mb4"}

	return sql.Open("mysql", config.FormatDSN())
}
