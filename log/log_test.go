package log

import "testing"

func TestLog(t *testing.T) {
	Debug("Hello world")
	Debug("Hello: %s", "iegad")

	Info("Hello world")
	Info("Hello: %s", "iegad")

	Warn("Hello world")
	Warn("Hello: %s", "iegad")

	Error("Hello world")
	Error("Hello: %s", "iegad")

	Fatal("Done....Fatal")
}
