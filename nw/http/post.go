package http

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"strings"
)

func PostForm(u string, params map[string]interface{}) (map[string]interface{}, int, error) {
	sb := strings.Builder{}

	i := len(params)
	for k, v := range params {
		sb.WriteString(fmt.Sprintf("%v=%v", k, v))
		if i > 1 {
			sb.WriteString("&")
		}
		i--
	}

	rsp, err := http.Post(u, "application/x-www-form-urlencoded", strings.NewReader(sb.String()))
	if err != nil {
		return nil, -1, err
	}

	defer rsp.Body.Close()

	if rsp.StatusCode != http.StatusOK {
		return nil, rsp.StatusCode, nil
	}

	body, err := ioutil.ReadAll(rsp.Body)
	if err != nil {
		return nil, -1, err
	}

	if len(body) > 0 {
		res := map[string]interface{}{}
		err = json.Unmarshal(body, &res)
		if err != nil {
			return nil, -1, err
		}

		return res, rsp.StatusCode, nil
	}

	return nil, rsp.StatusCode, nil
}
