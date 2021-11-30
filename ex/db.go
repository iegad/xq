package ex

import "database/sql"

func IF_STR_EMPTY(v string) *sql.NullString {
	return &sql.NullString{
		String: v,
		Valid:  len(v) > 0,
	}
}
