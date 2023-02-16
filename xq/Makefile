######################################
#
######################################
#source file
#源文件，自动找所有.c和.cpp文件，并将目标定义为同名.o文件
SOURCE  := $(wildcard *.c third/*.c) $(wildcard *.cpp tools/*.cpp net/*.cpp)
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))
  
#target you can change test to what you want
#目标文件名，输入任意你想要的执行文件名
TARGET  := kcp_server
  
#compile and lib parameter
#编译参数
CC      := g++
LIBS    :=
LDFLAGS := -L/usr/local/lib -lpthread -ljemalloc
DEFINES :=
INCLUDE := -I.. -I/usr/local/include
CFLAGS  := -g -Wall -O3 $(DEFINES) $(INCLUDE)
CXXFLAGS:= $(CFLAGS) --std=gnu++2a -DHAVE_CONFIG_H
  
  
#i think you should do anything here
#下面的基本上不需要做任何改动了
.PHONY : everything objs tidy clean rebuild

everything: $(TARGET)
  
all: $(TARGET)
  
objs: $(OBJS)
  
rebuild: clean everything

tidy:
	rm -fr *.so
	rm -fr *.o
                
clean:
	rm -fr *.so
	rm -fr *.o
	rm -fr $(TARGET)
  
$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

