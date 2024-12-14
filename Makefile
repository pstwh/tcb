CC = gcc
CXX = g++

CUDA_PATH ?= /usr/local/cuda
CUDA_INCLUDE = $(CUDA_PATH)/include
CUDA_LIB = $(CUDA_PATH)/lib64

CFLAGS = -Wall -Wextra -std=c99 -fopenmp -DGGML_USE_CUDA -I$(CUDA_INCLUDE)
CXXFLAGS = -Wall -Wextra -std=c++11 -fopenmp -DGGML_USE_CUDA -I$(CUDA_INCLUDE)

INCLUDES = \
	-I../ \
	-Iwhisper.cpp/include/ \
	-Iwhisper.cpp/ggml/include \
	-I$(CUDA_INCLUDE) \
	-I$(CUDA_PATH)/targets/$(UNAME_M)-linux/include

LDFLAGS = \
	-lpthread \
	-lm \
	-ldl \
	-lsndfile \
	whisper.cpp/libwhisper.a \
	-lcuda \
	-lcublas \
	-lculibos \
	-lcudart \
	-lcublasLt \
	-lrt \
	-L$(CUDA_LIB) \
	-L/usr/lib64 \
	-L$(CUDA_PATH)/targets/$(UNAME_M)-linux/lib \
	-L$(CUDA_LIB)/stubs \
	-L/usr/lib/wsl/lib

TARGET = tcb
SRC = tcb.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $^ -o $@ $(CXXFLAGS) $(LDFLAGS) $(INCLUDES)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

install: $(TARGET)
	install -D $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)