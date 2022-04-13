package log

import (
	"fmt"
	"os"
	"runtime"
	"time"
)

const (
	levelDebug = iota
	levelInfo
	levelWarn
	levelError
	levelFatal
	levelExit
)

var (
	logPath = ""
	lvMap   = map[int]string{
		levelDebug: "DEBUG",
		levelInfo:  "INFO",
		levelWarn:  "WARN",
		levelError: "ERROR",
		levelFatal: "FATAL",
		levelExit:  "EXIT",
	}

	lvfnmap = map[int]string{}
	lvfmap  = map[int]*os.File{}
)

func SetPath(path ...string) {
	n := len(path)

	if n > 0 {
		n = len(path[0])
		if n > 0 {
			if path[0][n-1:n] == "/" {
				logPath = path[0][:n-1]
			} else {
				logPath = path[0]
			}

			_, err := os.Stat(logPath)
			if err != nil {
				if os.IsNotExist(err) {
					err = os.MkdirAll(logPath, 0755)
				}

				if err != nil {
					panic(err)
				}
			}
		}
	}
}

func Debug(args ...interface{}) {
	base(levelDebug, args...)
}

func Info(args ...interface{}) {
	base(levelInfo, args...)
}

func Warn(args ...interface{}) {
	base(levelWarn, args...)
}

func Error(args ...interface{}) {
	base(levelError, args...)
}

func Fatal(args ...interface{}) {
	base(levelFatal, args...)
}

func Exit(args ...interface{}) {
	base(levelExit, args...)
}

func base(level int, args ...interface{}) {
	var (
		_, file, line, _ = runtime.Caller(2)
		n                = len(args)
		content          = ""
	)

	if n == 1 {
		content = fmt.Sprintf("%v", args[0])
	} else if n > 1 {
		if v, ok := args[0].(string); ok {
			content = fmt.Sprintf(v, args[1:]...)
		} else {
			panic("args[0].(string) failed")
		}
	}

	tn := time.Now()
	content = fmt.Sprintf("[%s %s %s:%d]%v", lvMap[level], tn.Format("2006-01-02 15:04:05.000000"), file, line, content)
	fmt.Fprintln(getFile(level, tn), content)
}

func getFile(lv int, tn time.Time) *os.File {
	if len(logPath) == 0 {
		return os.Stdout
	}

	fname := fmt.Sprintf("%s/%s.%s", logPath, time.Now().Format("2006-01-02"), lvMap[lv])
	if fname != lvfnmap[lv] {
		if lvfmap[lv] != nil {
			lvfmap[lv].Sync()
			lvfmap[lv].Close()
		}
	}

	lvfnmap[lv] = fname
	var err error

	lvfmap[lv], err = os.OpenFile(fname, os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0755)
	if err != nil {
		fmt.Println(err)
		return nil
	}

	return lvfmap[lv]
}
