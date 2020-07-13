#############################################################################
# Makefile for building MSI Keyboard Light Manager (MSIKLM)
#############################################################################

####### Compiler, tools and options
TARGET_C      = msiklm
TARGET_D      = msiklmd
CC            = gcc
CFLAGS        = -m64 -pipe -O3 -Wall -W -D_REENTRANT
LFLAGS        = -m64 -Wl,-O3
LIBS          = -lhidapi-libusb -lm
DEL_FILE      = rm -f
INSTALLPREFIX = /usr/local/bin

####### Files
INC_DIR       = src
INC_FILE      = msiklm.h

SRC_DIR       = src
SRC_FILE      = msiklm.c
SRC_FILE_C    = main-client.c $(SRC_FILE)
SRC_FILE_D    = main-daemon.c $(SRC_FILE)
OBJ_DIR       = .obj
OBJ_FILE_C    = $(SRC_FILE_C:.c=.o)
OBJ_FILE_D    = $(SRC_FILE_D:.c=.o)

CRT_DIR       = .

SRC           = $(addprefix $(SRC_DIR)/,$(SRC_FILE))
INC           = $(addprefix $(INC_DIR)/,$(INC_FILE))
OBJ_C         = $(addprefix $(OBJ_DIR)/,$(OBJ_FILE_C))
OBJ_D         = $(addprefix $(OBJ_DIR)/,$(OBJ_FILE_D))
CRT           = $(addprefix $(OBJ_DIR)/,$(CRT_DIR))

####### Build rules

all: $(TARGET_C) $(TARGET_D)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INC) Makefile
	@mkdir -p $(CRT) 2> /dev/null || true
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_C): $(OBJ_C)
	$(CC) $(LFLAGS) -o $@ $(OBJ_C) $(LIBS)

$(TARGET_D): $(OBJ_D)
	$(CC) $(LFLAGS) -o $@ $(OBJ_D) $(LIBS)

clean:
	$(DEL_FILE) $(OBJ_C) $(OBJ_D)
	$(DEL_FILE) -r $(OBJ_DIR)

delete: clean
	$(DEL_FILE) $(TARGET_C) $(TARGET_D)

install: all
	@cp -v $(TARGET_C) $(INSTALLPREFIX)/$(TARGET_C)
	@chmod 755 $(INSTALLPREFIX)/$(TARGET_C)
	@cp -v $(TARGET_D) $(INSTALLPREFIX)/$(TARGET_D)
	@chmod 755 $(INSTALLPREFIX)/$(TARGET_D)

re: delete all

.PHONY: all clean delete re
