ifdef OS
   RM = del /Q
   FIXPATH = $(subst /,\,$1)
else
   RM = rm -f
   FIXPATH = $1
endif

CXX = g++
CXXFLAGS = -std=c++20 -I. -Wall -Wextra -O3 -static-libgcc -static-libstdc++
TARGET = cicada_tools

SRCS = main.cpp core.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET).exe $(TARGET)

.PHONY: all clean
