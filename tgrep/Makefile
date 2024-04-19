CC=gcc
CFLAGS=-Wall -pthread
TARGET=tgrep

all: $(TARGET)

$(TARGET): multithreaded.c
	$(CC) $(CFLAGS) multithreaded.c -o $(TARGET)
	@echo "Build successful!"
	@echo "Usage: ./$(TARGET) num_workers root_path \"search_string\" [-options]"
	@echo "Options:"
	@echo "  -c      Print only a count of the lines that match a pattern."
	@echo "  -h      Display the matched lines, but do not display the filenames."
	@echo "  -i      Ignore case distinctions in both the pattern and the input files."
	@echo "  -l      Display list of filenames only."
	@echo "  -n      Display the matched lines and their line numbers."
	@echo "  -v      Invert the sense of matching, to select non-matching lines."
	@echo "  -help   Display this help and exit."

clean:
	rm -f $(TARGET)
