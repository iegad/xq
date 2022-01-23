package utils

func EqualBytes(a1, a2 []byte) bool {
	n := len(a1)

	if n != len(a2) {
		return false
	}

	for i := 0; i < n; i++ {
		if a1[i] != a2[i] {
			return false
		}
	}

	return true
}
