package log

import (
	"fmt"
	"os"
	"runtime"
	"sync"
	"time"
)

type logType struct {
	Level   int
	Content string
	F       *os.File
}

const (
	levelDebug = iota
	levelInfo
	levelWarn
	levelError
	levelFatal
	levelExit
)

var (
	inited = false

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

	wg  = &sync.WaitGroup{}
	lch = make(chan *logType, 100)

	ltPool = sync.Pool{
		New: func() interface{} {
			return &logType{}
		},
	}
)

func Init(path ...string) {
	n := len(path)

	if n > 0 {
		if n > 1 {
			panic("path is invalid")
		}

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

	go _handleLog()
	wg.Add(1)
	inited = true
}

func Release() {
	close(lch)
	wg.Wait()
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
	if !inited {
		panic("log compotent has not inited")
	}

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
	lt := ltPool.Get().(*logType)
	lt.Content = fmt.Sprintf("[%s %s %s:%d]%v\n",
		lvMap[level], tn.Format("2006-01-02 15:04:05.000000"), file, line, content)
	lt.Level = level
	lt.F = getFile(level, tn)

	lch <- lt
}

func getFile(lv int, tn time.Time) *os.File {
	if len(logPath) == 0 {
		return nil
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

func _handleLog() {
	var lt *logType

	for lt = range lch {
		if lt.F != nil {
			fmt.Fprintf(lt.F, lt.Content)
			if lt.Level > levelDebug {
				fmt.Printf(lt.Content)
			}
		} else {
			fmt.Printf(lt.Content)
		}

		if lt.Level == levelFatal {
			os.Exit(1)
		}

		if lt.Level == levelExit {
			os.Exit(0)
		}

		ltPool.Put(lt)
	}

	wg.Done()
}
