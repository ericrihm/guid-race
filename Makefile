CC = clang
CFLAGS = -O3 -march=native -Wall -Wextra -Wno-unused-parameter
LDFLAGS =

SRCS = impl_baseline.c impl_dave.c impl_lookup16.c impl_unrolled.c \
       impl_arithmetic.c impl_swar.c impl_neon.c harness.c

TARGET = guid_race

.PHONY: all clean run profile

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

run: $(TARGET)
	./$(TARGET)

profile: $(TARGET)
	xcrun xctrace record --template 'Time Profiler' --launch -- ./$(TARGET)

clean:
	rm -f $(TARGET) *.o
